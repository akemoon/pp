#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <ranges>

namespace
{
constexpr auto number_of_stages { 3 };
constexpr auto cars_number { 5 };
constexpr auto finish_line { 100 };
constexpr auto track_length { 50 };

// Флаг синхронизации через сигнал.
// atomic_bool - потокобезопасный bool, корректно работает в обработчике сигнала.
// start_flag: машина получила SIGUSR1 - можно стартовать следующий этап.
std::atomic_bool start_flag {};
}  // namespace

// Сообщение для передачи прогресса от машины арбитру через очередь сообщений.
struct ProgressMessage
{
  long mtype { 1 };   // Тип сообщения (обязательное поле SysV очереди)
  int id {};          // ID машины
  int progress {};    // Текущая позиция на трассе
  int finished {};    // 1 - машина финишировала
};

class Car
{
 public:
  void start(int id, int queue)
  {
    // Устанавливаем обработчик сигнала.
    // SIGUSR1 - старт каждого этапа (арбитр шлёт всем машинам одновременно).
    signal(SIGUSR1, [](int) { start_flag = true; });

    // Инициализация генераторов случайных чисел для каждой машины независимо.
    std::mt19937 generator(std::random_device {}());
    std::uniform_int_distribution<> step_dist(1, 10);   // шаг за тик
    std::uniform_int_distribution<> sleep_dist(100, 300); // задержка в мс

    for (int stage { 1 }; stage <= number_of_stages; ++stage)
    {
      // Барьер старта: машина спит в pause() до получения SIGUSR1.
      // pause() атомарно ждёт любого сигнала и возвращается после его обработки.
      while (!start_flag) pause();
      start_flag = false;

      progress = 0;
      finished = false;
      order = 0;

      // Основной цикл гонки: машина движется до финишной черты.
      while (progress < finish_line)
      {
        progress = std::min(progress + step_dist(generator), finish_line);

        // Отправляем арбитру текущую позицию (finished = 0).
        ProgressMessage message {
          .id = id,
          .progress = progress,
        };
        msgsnd(queue, &message, sizeof(message) - sizeof(long), 0);

        usleep(sleep_dist(generator) * 1000);
      }

      // Машина финишировала - отправляем финальное сообщение с finished = 1.
      ProgressMessage message {
        .id = id,
        .progress = progress,
        .finished = 1,
      };
      msgsnd(queue, &message, sizeof(message) - sizeof(long), 0);
    }
  }

  int progress {};
  int order {};    // Место, занятое на текущем этапе
  int points {};   // Накопленные очки
  bool finished {};
};

class Arbiter
{
 public:
  // При уничтожении арбитра удаляем очередь сообщений из ядра.
  ~Arbiter() { msgctl(progress_queue, IPC_RMID, nullptr); }

  void prepare()
  {
    for (auto i { 0 }; i < cars_number; ++i)
    {
      auto const process { fork() };

      if (process == 0)
      {
        // Дочерний процесс: объединяем все машины в одну группу процессов,
        // чтобы арбитр мог послать сигнал сразу всем через kill(-group, sig).
        if (i == 0) process_group = getpid();
        setpgid(0, process_group);

        cars.at(i).start(i, progress_queue);
        std::exit(0);
      }

      processes.at(i) = process;

      // Родительский процесс тоже добавляет каждый pid в группу.
      if (i == 0) process_group = process;
      setpgid(process, process_group);
    }
  }

  void start()
  {
    for (int stage { 1 }; stage <= number_of_stages; ++stage)
    {
      current_stage = stage;
      finish_order_counter = 0;

      // Сброс состояния машин перед новым этапом (очки сохраняем).
      for (auto& car : cars)
        car = {
          .progress = 0,
          .order = 0,
          .points = car.points,
          .finished = false,
        };

      std::cout << "Подготовка этапа " << stage << "\n";
      sleep(1);

      // Одновременный старт: посылаем SIGUSR1 всей группе процессов.
      // kill с отрицательным pid шлёт сигнал всей группе.
      kill(-process_group, SIGUSR1);

      unsigned int finished_count {};
      while (finished_count < cars.size())
      {
        ProgressMessage message {};

        // Вычитываем все накопившиеся сообщения из очереди (IPC_NOWAIT - не блокируемся).
        while (msgrcv(progress_queue, &message, sizeof(message) - sizeof(long),
                      0, IPC_NOWAIT) > 0)
        {
          auto& car { cars.at(message.id) };

          car.progress = message.progress;

          if (message.finished && !car.finished)
          {
            car.finished = true;
            car.order = ++finish_order_counter;
            car.points += car.order; // Очки = место финиша (1-е место = 1 очко и т.д.)
          }
        }

        finished_count = std::ranges::count_if(cars, &Car::finished);

        display_progress();

        usleep(200000);
      }

      display_points();

      if (stage == number_of_stages)
      {
        std::cout << "Гонка завершена\n";
        continue;
      }

      std::cout << "\nНажмите Enter для начала следующего этапа...\n";
      std::cin.ignore();
    }

    display_results();

    for (auto const& process : processes) waitpid(process, nullptr, 0);
  }

 private:
  void display_progress() const
  {
    std::cout << "\033[2J\033[1;1H";
    std::cout << "Прогресс этапа " << current_stage << ":\n";

    for (int i {}; auto const& car : cars)
    {
      std::string bar(track_length, '.');

      int pos { (car.progress * track_length) / finish_line };

      std::fill_n(bar.begin(), std::min(pos, track_length), '=');

      if (pos < track_length) bar.at(pos) = '>';

      std::cout << "Машина " << (++i) << " : [" << bar << "] "
                << car.progress << " / " << finish_line;

      if (car.finished)
        std::cout << " (Финишировала с местом: " << car.order << ")\n";
      else
        std::cout << "\n";
    }
  }

  void display_points() const
  {
    std::cout << "\nРезультаты этапа " << current_stage << ":\n";

    std::array<Car const*, cars_number> orders {};

    for (auto [ordered_car, car] : std::ranges::zip_view { orders, cars })
      ordered_car = &car;

    std::ranges::sort(orders, std::ranges::less {}, &Car::order);

    for (int i {}; auto order : orders)
      std::cout << "Место " << (++i) << ": Машина "
                << (std::distance(cars.begin(), order) + 1)
                << " (Очки: " << order->points << ")\n";
  }

  void display_results() const
  {
    std::cout << "\nИтоговые результаты:\n";

    std::array<Car const*, cars_number> scores {};

    for (auto [score, car] : std::ranges::zip_view { scores, cars })
      score = &car;

    std::ranges::sort(scores, std::ranges::less {}, &Car::points);

    for (int i {}; auto car : scores)
      std::cout << "Место " << (++i) << ": Машина "
                << (std::distance(cars.data(), car) + 1)
                << " (Всего очков: " << car->points << ")\n";
  }

  std::array<pid_t, cars_number> processes {};
  pid_t process_group {};

  int progress_queue { msgget(IPC_PRIVATE, IPC_CREAT | 0666) };

  int current_stage {};
  int finish_order_counter {};
  std::array<Car, cars_number> cars {};
};

int main()
{
  Arbiter arbiter {};
  arbiter.prepare();
  arbiter.start();
}

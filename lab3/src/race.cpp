#include <mpi.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <ranges>
#include <string>
#include <vector>

namespace
{
constexpr int number_of_stages { 3 };
constexpr int cars_number { 5 };
constexpr int finish_line { 100 };
constexpr int track_length { 50 };
}  // namespace

struct Car
{
    int progress {};
    int order {};
    int points {};
    bool finished {};
};

static void display_progress(std::array<Car, cars_number> const& cars, int stage)
{
    std::cout << "\033[2J\033[1;1H";
    std::cout << "Прогресс этапа " << stage << ":\n";

    for (int i {}; auto const& car : cars)
    {
        std::string bar(track_length, '.');
        int pos { (car.progress * track_length) / finish_line };
        std::fill_n(bar.begin(), std::min(pos, track_length), '=');
        if (pos < track_length) bar.at(pos) = '>';

        std::cout << "Машина " << (++i) << " : [" << bar << "] "
                  << car.progress << " / " << finish_line;

        if (car.finished)
            std::cout << " (Финишировала: место " << car.order << ")\n";
        else
            std::cout << "\n";
    }
}

static void display_points(std::array<Car, cars_number> const& cars, int stage)
{
    std::cout << "\nРезультаты этапа " << stage << ":\n";

    std::array<Car const*, cars_number> ordered {};
    for (auto [ptr, car] : std::ranges::zip_view { ordered, cars })
        ptr = &car;

    std::ranges::sort(ordered, std::ranges::less {}, &Car::order);

    for (int i {}; auto const* car : ordered)
        std::cout << "Место " << (++i) << ": Машина "
                  << (std::distance(cars.data(), car) + 1)
                  << " (Очки: " << car->points << ")\n";
}

static void display_results(std::array<Car, cars_number> const& cars)
{
    std::cout << "\nИтоговые результаты:\n";

    std::array<Car const*, cars_number> scores {};
    for (auto [ptr, car] : std::ranges::zip_view { scores, cars })
        ptr = &car;

    std::ranges::sort(scores, std::ranges::less {}, &Car::points);

    for (int i {}; auto const* car : scores)
        std::cout << "Место " << (++i) << ": Машина "
                  << (std::distance(cars.data(), car) + 1)
                  << " (Всего очков: " << car->points << ")\n";
}

int main(int argc, char** argv)
{
    // Инициализация MPI-среды. Должна быть вызвана до любых других MPI-функций.
    MPI_Init(&argc, &argv);

    int rank {};
    // Получаем ранг текущего процесса - 0 арбитр, 1..5 машины.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int size {};
    // Получаем общее количество процессов в коммуникаторе.
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // MPI_Comm_split
    // Создаёт отдельный коммуникатор только для машин (rank > 0).
    // Арбитр попадает в свою группу (color=0), машины в свою (color=1).
    // Нужно чтобы MPI_Barrier срабатывал только между машинами.
    // Параметры:
    //   1) MPI_COMM_WORLD - (IN)  исходный коммуникатор
    //   2) rank != 0      - (IN)  color - 0 для арбитра, 1 для машин
    //   3) rank           - (IN)  key - порядок рангов внутри новой группы
    //   4) &cars_comm     - (OUT) новый коммуникатор
    //
    MPI_Comm cars_comm {};
    MPI_Comm_split(MPI_COMM_WORLD, rank != 0, rank, &cars_comm);

    if (size != cars_number + 1)
    {
        if (rank == 0)
            std::cout << "Нужно ровно " << cars_number + 1
                      << " процессов (1 арбитр + " << cars_number << " машин)\n";
        MPI_Finalize();
        return 1;
    }

    if (rank == 0)
    {
        // Арбитр
        std::array<Car, cars_number> cars {};
        std::vector<int> positions(size, 0);

        for (int stage { 1 }; stage <= number_of_stages; ++stage)
        {
            for (auto& car : cars)
                car = { .progress = 0, .order = 0, .points = car.points, .finished = false };

            std::cout << "Подготовка этапа " << stage << "\n";
            sleep(1);

            // MPI_Bcast
            // Рассылает сигнал старта всем машинам - аналог kill(-group, SIGUSR1) из lab2.
            // Параметры:
            //   1) &signal      - (IN/OUT) буфер: на root - источник, на остальных - приёмник
            //   2) 1            - (IN)     количество элементов
            //   3) MPI_INT      - (IN)     тип элементов
            //   4) 0            - (IN)     root - кто рассылает
            //   5) MPI_COMM_WORLD - (IN)   коммуникатор
            //
            int signal { 1 };
            MPI_Bcast(&signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            int finish_order_counter {};
            int stop_flag {};

            while (!stop_flag)
            {
                int dummy {};

                // MPI_Gather
                // Собирает позиции от всех машин - аналог msgrcv из lab2.
                // После вызова positions[i+1] содержит позицию машины с rank i+1.
                // Параметры:
                //   1) &dummy          - (IN)  заглушка арбитра (он не машина)
                //   2) 1               - (IN)  сколько элементов отправляет каждый процесс
                //   3) MPI_INT         - (IN)  тип элементов
                //   4) positions.data()- (OUT) буфер приёма на root
                //   5) 1               - (IN)  сколько элементов принять от каждого
                //   6) MPI_INT         - (IN)  тип элементов
                //   7) 0               - (IN)  root - кто собирает
                //   8) MPI_COMM_WORLD  - (IN)  коммуникатор
                //
                MPI_Gather(&dummy, 1, MPI_INT,
                           positions.data(), 1, MPI_INT,
                           0, MPI_COMM_WORLD);

                for (int i {}; i < cars_number; ++i)
                {
                    cars[i].progress = positions[i + 1];

                    if (cars[i].progress >= finish_line && !cars[i].finished)
                    {
                        cars[i].finished = true;
                        cars[i].order    = ++finish_order_counter;
                        cars[i].points  += cars[i].order;
                    }
                }

                display_progress(cars, stage);

                stop_flag = (finish_order_counter == cars_number) ? 1 : 0;

                // Рассылаем флаг остановки - аналог проверки finished_count из lab2
                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

                usleep(200000);
            }

            display_points(cars, stage);

            // Арбитр не участвует в барьере - он управляет этапом через stop_flag

            if (stage < number_of_stages)
            {
                std::cout << "\nНажмите Enter для следующего этапа...\n";
                std::cin.ignore();
            }
        }

        display_results(cars);
    }
    else
    {
        // Машина
        std::mt19937 gen(std::random_device {}() ^ (unsigned)rank);
        std::uniform_int_distribution<> step_dist(1, 10);
        std::uniform_int_distribution<> sleep_dist(100, 300);

        for (int stage { 1 }; stage <= number_of_stages; ++stage)
        {
            // Ждём сигнала старта от арбитра - аналог pause() + SIGUSR1 из lab2
            int signal {};
            MPI_Bcast(&signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            int pos {};
            int stop_flag {};

            while (!stop_flag)
            {
                if (pos < finish_line)
                    pos = std::min(pos + step_dist(gen), finish_line);

                // Отправляем позицию арбитру - аналог msgsnd из lab2
                MPI_Gather(&pos, 1, MPI_INT,
                           nullptr, 0, MPI_INT,
                           0, MPI_COMM_WORLD);

                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

                if (!stop_flag)
                    usleep(sleep_dist(gen) * 1000);
            }

            // MPI_Barrier
            // Удерживает все машины до тех пор пока каждая не завершит этап.
            // Арбитр не участвует - он в отдельном коммуникаторе (cars_comm).
            // Параметры:
            //   1) cars_comm - (IN) коммуникатор только машин
            //
            MPI_Barrier(cars_comm);
        }
    }

    MPI_Comm_free(&cars_comm);
    MPI_Finalize();
    return 0;
}

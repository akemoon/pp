#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <ranges>
#include <thread>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string>
#include <numeric>
#include <iomanip>   
#include <array>

#include "mpi.h"

using namespace std::ranges;

// Функция для удобного составления строки из разных типов
template <typename... Args>
std::string MakeStr(Args&&... args)
{
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    return oss.str();
}

// Константы гонки
constexpr int number_parts = 3;   // Количество этапов гонки
constexpr int track_len = 50;     // Длина трассы
constexpr int max_ticks = 600;    // Максимальное количество тиков (итераций)

// Функция для выравнивания чисел до 2 символов (01, 02, …)
static std::string Pad2(int x)
{
    if (x < 10) return MakeStr("0", x);
    return MakeStr(x);
}

// форматируем время 123456 ms -> 02:03.456
static std::string FormatMs(int ms)
{
    int m = ms / 60000;
    int s = (ms / 1000) % 60;
    int r = ms % 1000;
    return MakeStr(Pad2(m), ":", Pad2(s), ".", std::string(3 - std::to_string(r).size(), '0'), r, " ms");
}

// вычисляем места по текущей позиции (больше позиция -> лучше место)
static std::vector<int> ComputePlaces(const std::vector<int>& pos)
{
    int n = (int)pos.size();
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);

    // сортируем только по позиции, root (0) тоже попадет, но мы его не печатаем
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return pos[a] > pos[b];
    });

    std::vector<int> place(n, 0);
    int cur_place = 1;
    for (int id : idx)
        place[id] = cur_place++;

    return place;
}

// Обновляем места в общем зачёте (Total place) по totalScore
static void UpdateTotalPlaces(const std::vector<int>& totalScore, std::vector<int>& totalPlace, int size)
{
    std::vector<int> cars;
    cars.reserve(std::max(0, size - 1));
    for (int car = 1; car < size; ++car) cars.push_back(car);

    std::sort(cars.begin(), cars.end(), [&](int a, int b) {
        if (totalScore[a] != totalScore[b]) return totalScore[a] < totalScore[b];
        return a < b;
    });

    for (int i = 0; i < (int)cars.size(); ++i)
        totalPlace[cars[i]] = i + 1;
}

// Стабильная таблица сверху
static void RenderResultsTable(
    int size,
    const std::vector<std::array<int, number_parts>>& placeByStage,
    const std::vector<std::array<int, number_parts>>& scoreByStage,
    const std::vector<int>& totalScore,
    const std::vector<int>& totalPlace
)
{
    constexpr int kCarW = 5;
    constexpr int kStageCellW = 11; 
    constexpr int kTotalCellW = 13; 

    const int totalWidth = kCarW + 1
                         + number_parts * (1 + kStageCellW)
                         + 1 + kTotalCellW;

    auto HR = [&]() { std::cout << std::string(totalWidth, '-') << "\n"; };

    HR();
    std::cout << "ТАБЛИЦА РЕЗУЛЬТАТОВ  (Оч = очки, М = место)\n";
    HR();

    std::cout << std::setw(kCarW) << std::left << "Маш" << " ";
    for (int st = 1; st <= number_parts; ++st)
    {
        std::cout << "|"
                  << std::setw(kStageCellW) << std::left
                  << ("Э" + std::to_string(st) + " (Оч/М)");
    }
    std::cout << "|"
              << std::setw(kTotalCellW) << std::left
              << "TOTAL (Оч/М)"
              << "\n";

    HR();

    for (int car = 1; car < size; ++car)
    {
        std::cout << std::setw(kCarW) << std::left << std::to_string(car) << " ";

        for (int st = 0; st < number_parts; ++st)
        {
            std::cout << "|";
            if (scoreByStage[car][st] == 0 && placeByStage[car][st] == 0)
            {
                std::cout << std::setw(kStageCellW) << std::left << "- / -";
            }
            else
            {
                std::ostringstream oss;
                oss << std::setw(3) << std::right << scoreByStage[car][st]
                    << " / "
                    << std::setw(2) << std::right << placeByStage[car][st];
                std::cout << std::setw(kStageCellW) << std::left << oss.str();
            }
        }

        std::cout << "|";
        {
            std::ostringstream oss;
            oss << std::setw(3) << std::right << totalScore[car]
                << " / "
                << std::setw(2) << std::right << totalPlace[car];
            std::cout << std::setw(kTotalCellW) << std::left << oss.str();
        }

        std::cout << "\n";
    }

    HR();
    std::cout << "\n";
    std::cout.flush();
}

static void RenderFrame(
    int stage,
    int size,
    const std::vector<int>& pos,
    const std::vector<std::array<int, number_parts>>& placeByStage,
    const std::vector<std::array<int, number_parts>>& scoreByStage,
    const std::vector<int>& totalScore,
    const std::vector<int>& totalPlace
)
{
    std::cout << "\033[2J\033[H";

    RenderResultsTable(size, placeByStage, scoreByStage, totalScore, totalPlace);

    std::cout << MakeStr("Этап ", stage, "/", number_parts, "\n\n");

    auto place_now = ComputePlaces(pos);

    for (int r = 1; r < (int)pos.size(); ++r)
    {
        int p = std::clamp(pos[r], 0, track_len);
        int progress = (int)((long long)p * 100 / track_len);

        std::string line;
        line.reserve(track_len + 2);
        line.append(p, '-');
        line.push_back('>');
        line.append(track_len - p, ' ');

        // стабилизируем числа, чтобы строки не дергались
        std::cout << "car " << std::setw(2) << std::right << r
                  << " |" << line << "| "
                  << std::setw(3) << std::right << progress << " / 100"
                  << "  (place: " << std::setw(2) << std::right << place_now[r] << ")\n";
    }
    std::cout.flush();
}

// Возвращает отсортированный по времени список (car, time_ms)
static std::vector<std::pair<int,int>> BuildStageResults(const std::vector<int>& finish_times, int size)
{
    std::vector<std::pair<int,int>> stage_res;
    stage_res.reserve(std::max(0, size - 1));

    for (int car = 1; car < size; ++car)
        stage_res.push_back({car, finish_times[car]});

    std::sort(stage_res.begin(), stage_res.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    return stage_res;
}

int main(int argc, char** argv)
{
    // ========================= MPI_Init =========================
    // Назначение:
    //   Инициализирует MPI-среду. Должна быть первой MPI-функцией в программе.
    // Возвращает:
    //   int - код ошибки (MPI_SUCCESS при успехе)
    // ============================================================
    MPI_Init(&argc, &argv);

    int rank{};

    // ====================== MPI_Comm_rank =======================
    // Назначение:
    //   Получить ранг текущего процесса внутри коммуникатора.
    // Параметры:
    //   1) MPI_COMM_WORLD (MPI_Comm) - (IN) коммуникатор "все процессы"
    //   2) &rank          (int*)     - (OUT) сюда запишется ранг процесса: 0..size-1
    // Возвращает:
    //   int - код ошибки
    // ============================================================
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int size{};

    // ====================== MPI_Comm_size =======================
    // Назначение:
    //   Узнать, сколько процессов участвует в данном коммуникаторе.
    // Параметры:
    //   1) MPI_COMM_WORLD (MPI_Comm) - (IN) коммуникатор
    //   2) &size          (int*)     - (OUT) сюда запишется количество процессов
    // Возвращает:
    //   int - код ошибки
    // ============================================================
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    MPI_Comm cars{};

     // ====================== MPI_Comm_split ======================
    // Назначение:
    //   Разделить исходный коммуникатор на под-группы (новые коммуникаторы).
    // Параметры:
    //   1) MPI_COMM_WORLD (MPI_Comm) - (IN) исходный коммуникатор
    //   2) color          (int)      - (IN) "цвет" группы: одинаковый color -> одна группа
    //       Здесь: (rank != 0)
    //         - rank 0  -> color = 0 (судья)
    //         - rank>0  -> color = 1 (машины)
    //   3) key            (int)      - (IN) порядок рангов внутри новой группы
    //       Здесь: key = rank (обычный вариант)
    //   4) &cars          (MPI_Comm*)- (OUT) новый коммуникатор, записывается сюда
    // Возвращает:
    //   int - код ошибки
    MPI_Comm_split(MPI_COMM_WORLD, rank != 0, rank, &cars);

    // эти ассивы создаются у всех, но нужны они только главному процессу
    // у судьи positions и finish_times будут заполняться данными из MPI_Gather
    std::vector<int> positions(size, 0);
    std::vector<int> finish_times(size, 0); 

    if (rank == 0)
    {
        // rank 0 = судья (собирает данные, рисует, начисляет очки)
        std::vector<std::array<int, number_parts>> placeByStage(size);
        std::vector<std::array<int, number_parts>> scoreByStage(size);
        std::vector<int> totalScore(size, 0);
        std::vector<int> totalPlace(size, 0);

        UpdateTotalPlaces(totalScore, totalPlace, size);

        // Цикл этапов
        for (auto stage : views::iota(1, number_parts + 1))
        {
            // сигнал для старта этапа
            int start_signal = 1;

            // ======================== MPI_Bcast =========================
            // Назначение:
            //   Рассылка данных от root-процесса всем процессам в comm.
            // Параметры:
            //   1) &start_signal (void*)    - данные
            //   2) 1             (int)      - количество элементов
            //   3) MPI_INT       (MPI_Datatype) - тип элементов
            //   4) 0             (int)      - root (кто рассылает)
            //   5) MPI_COMM_WORLD(MPI_Comm) - коммуникатор
            // Возвращает:
            //   int - код ошибки
            // ============================================================
            MPI_Bcast(&start_signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            // сброс позиций и печатает таблицу и полоску прогресса
            std::fill(positions.begin(), positions.end(), 0);
            RenderFrame(stage, size, positions, placeByStage, scoreByStage, totalScore, totalPlace);

            int stop_flag = 0;

            for (int tick = 0; tick < max_ticks; ++tick)
            {
                int root_dummy_pos = 0;

                // ======================== MPI_Gather ========================
                // Назначение:
                //   Собрать данные от ВСЕХ процессов на root-процессе.
                // Параметры (в ЭТОМ вызове на root=0):
                //   1) &root_dummy_pos (const void*) - (IN) что root "отправляет сам от себя"
                //       (root тоже участник Gather, но судья не машина, поэтому шлёт 0)
                //   2) 1               (int)        - (IN) sendcount: сколько элементов отправляем
                //   3) MPI_INT         (MPI_Datatype)- (IN) sendtype: тип отправляемых элементов
                //   4) positions.data()(void*)      - (OUT на root) куда собрать данные от всех процессов
                //       После вызова:
                //         positions[0] = данные rank 0 (dummy 0)
                //         positions[1] = позиция машины rank 1
                //         ...
                //         positions[size-1] = позиция машины rank size-1
                //   5) 1               (int)        - (IN) recvcount: сколько элементов приходит ОТ КАЖДОГО процесса
                //   6) MPI_INT         (MPI_Datatype)- (IN) recvtype
                //   7) 0               (int)        - (IN) root
                //   8) MPI_COMM_WORLD  (MPI_Comm)   - (IN) коммуникатор
                //
                // Возвращает:
                //   int - код ошибки
                MPI_Gather(&root_dummy_pos, 1, MPI_INT,
                           positions.data(), 1, MPI_INT,
                           0, MPI_COMM_WORLD);

                // обновляет прогресс
                RenderFrame(stage, size, positions, placeByStage, scoreByStage, totalScore, totalPlace);
                std::this_thread::sleep_for(std::chrono::milliseconds(80)); // частота обновления экрана

                // проверяем что все доехали
                bool all_finished = true;
                for (int r = 1; r < size; ++r)
                    all_finished &= (positions[r] >= track_len); // & - логическое и

                stop_flag = all_finished ? 1 : 0;

                // ======================== MPI_Bcast =========================
                // Рассылаем stop_flag всем:
                //   stop_flag = 1  -> все доехали, машины выходят из цикла
                //   stop_flag = 0  -> продолжаем
                // Возвращает int (код ошибки)
                // ============================================================
                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

                if (stop_flag) break;
            }

            // Собираем "время до финиша" каждой машины
            int root_time = 0;

            // ======================== MPI_Gather ==========================
            // Собираем времена финиша finish_ms от всех процессов на root.
            // На root:
            //   finish_times[car] будет содержать миллисекунды до финиша для машины car.
            // Параметры:
            //   sendbuf   = &root_time (root отправляет 0 как заглушку)
            //   recvbuf   = finish_times.data()
            // Остальное аналогично gather позиций.
            // Возвращает int (код ошибки)
            // ============================================================
            MPI_Gather(&root_time, 1, MPI_INT,
                       finish_times.data(), 1, MPI_INT,
                       0, MPI_COMM_WORLD);

            // Результаты этапа (по времени)
            auto stage_res = BuildStageResults(finish_times, size);

            // Заполняем таблицу: место на этапе + "очки" 
            const int sIdx = stage - 1;
            int place = 1;
            for (const auto& [car, t] : stage_res)
            {
                (void)t; 
                placeByStage[car][sIdx] = place;
                scoreByStage[car][sIdx] = place;
                totalScore[car] += place;
                ++place;
            }

            UpdateTotalPlaces(totalScore, totalPlace, size);

            // Рендерим кадр ещё раз, чтобы после этапа таблица точно обновилась
            RenderFrame(stage, size, positions, placeByStage, scoreByStage, totalScore, totalPlace);

            if (stage != number_parts)
            {
                std::cout << "\n----------------------------------------\n";
                std::cout << "Переход к следующему этапу...\n";
                std::cout.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            else
            {
                std::cout << "\nГонка завершена\n";
                std::cout.flush();
            }
        }
    }
    else
    {
        std::mt19937 gen(
            (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ (rank * 0x9e3779b9u)
        );

        std::uniform_int_distribution<int> step_dist(0, 3);       // рывки
        std::uniform_int_distribution<int> sleep_dist(60, 220);   // “явная” разница скоростей

        for (auto stage : views::iota(1, number_parts + 1))
        {
            int start_signal{};

            // ======================== MPI_Bcast =========================
            // Машина принимает start_signal, который разослал root (rank 0).
            // Здесь buffer=&start_signal является (OUT) буфером приёма.
            // Возвращает int (код ошибки).
            // ============================================================
            MPI_Bcast(&start_signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            // обнуляем позиции
            int pos = 0;
            int stop_flag = 0;

            auto started = std::chrono::steady_clock::now();
            int finish_ms = -1;

            for (int tick = 0; tick < max_ticks; ++tick)
            {
                if (pos < track_len)
                {
                    pos = std::min(track_len, pos + step_dist(gen));

                    // если достигли финиша то считаем время
                    if (pos >= track_len && finish_ms < 0)
                    {
                        finish_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started).count();
                    }
                }

                // ======================== MPI_Gather ========================
                // Машина отправляет свою позицию pos на root.
                // На НЕ-root процессе параметры recvbuf/recvcount/recvtype не используются.
                // Поэтому:
                //   recvbuf = nullptr
                //   recvcount = 0
                // Параметры:
                //   1) &pos        - sendbuf (IN): адрес отправляемого int
                //   2) 1           - sendcount
                //   3) MPI_INT     - sendtype
                //   4) nullptr     - recvbuf (не нужен на НЕ-root)
                //   5) 0           - recvcount (не нужен на НЕ-root)
                //   6) MPI_INT     - recvtype (формально указан)
                //   7) 0           - root
                //   8) MPI_COMM_WORLD - comm
                //
                // Возвращает:
                //   int - код ошибки
                // ============================================================
                MPI_Gather(&pos, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);

                // ======================== MPI_Bcast =========================
                // Машина получает stop_flag от root.
                // stop_flag=1 -> выходим из цикла этапа.
                // Возвращает int (код ошибки).
                // ============================================================
                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);
                if (stop_flag) break;

                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
            }

            if (finish_ms < 0)
            {
                finish_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started).count();
            }

            // ======================== MPI_Gather ==========================
            // Машина отправляет своё время финиша finish_ms на root.
            // На root данные соберутся в finish_times[].
            // Возвращает int (код ошибки).
            // ============================================================
            MPI_Gather(&finish_ms, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);

            // ======================== MPI_Barrier =========================
            // Назначение:
            //   Барьер синхронизации: все процессы в comm должны дойти сюда.
            // Сигнатура:
            //   int MPI_Barrier(MPI_Comm comm);
            // Параметры:
            //   1) cars (MPI_Comm) - (IN) коммуникатор "только машины"
            // Возвращает:
            //   int - код ошибки
            // ============================================================
            MPI_Barrier(cars);
        }
    }

     //   Освободить коммуникатор, созданный пользователем (MPI_Comm_split).
    MPI_Comm_free(&cars);

     //   Завершить MPI-среду. После этого MPI-вызовы делать нельзя.
    MPI_Finalize();
    return 0;
}

// краткое пояснение 

// MPI_Bcast (broadcast) что делает
// Один процесс (root) отправляет одно и то же значение всем остальным.
// Главная мысль: Bcast = “один сказал, все услышали”.

// MPI_Gather (gather) что делает
// Все процессы отправляют root’у свои данные, root складывает их в массив.

// Коротко: кто кому и зачем (в твоей программе)
// MPI_Bcast
// Судья → всем
// используется для команд управления:
// старт этапа
// “стоп, все финишировали”

// MPI_Gather
// все → судье
// используется для данных от машин:
// позиции для анимации
// времена финиша для результата

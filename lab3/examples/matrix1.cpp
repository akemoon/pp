#include <mpi.h>

#include <array>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
  // Инициализация MPI-среды. Должна быть вызвана ДО любых других MPI-функций.
  MPI_Init(&argc, &argv);

  int rank{}; 

  // Получаем ранг процесса в коммуникаторе MPI_COMM_WORLD.
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  int size{};

  // Получаем количество процессов в MPI_COMM_WORLD.
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // проверка что ровно 4 процесса
  if (size != 4)
  {
    if (rank == 0)
      std::cerr << "Ошибка: нужно ровно 4 процесса (по числу строк A=4). Сейчас: " << size << "\n";

    MPI_Finalize();
    return 1;
  }

  // A: 4x5  (20 элементов)
  // B: 5x6  (30 элементов)
  // C: 4x6  (24 элемента) – результат умножения A*B
  std::vector<double> A, B, C(4 * 6);    
  
  // Транспонированная В, чтобы удобнее считать (передавать не столбцы а строки)
  std::vector<double> transposed(6 * 5);    

  double t0 = 0.0;

  // rank 0 = "главный": он создаёт данные и печатает
  if (rank == 0) 
  {
    A.resize(4 * 5);
    B.resize(5 * 6);

    // Заполнение матриц данными
    for (int i = 0; i < 4 * 5; ++i) A[i] = i + 1;
    for (int i = 0; i < 5 * 6; ++i) B[i] = i + 1;

    // Транспонируем B (5x6) в transposed (6x5)
    for (int i = 0; i < 5; ++i)
      for (int j = 0; j < 6; ++j)
        transposed[j * 5 + i] = B[i * 6 + j];

    // Печать матрицы A (4x5)
    std::cout << "Матрица A (4x5):\n";
    for (int r = 0; r < 4; ++r)
    {
      for (int c = 0; c < 5; ++c) std::cout << A[r * 5 + c] << "\t";
      std::cout << "\n";
    }

    // Печать матрицы B (5x6)
    std::cout << "Матрица B (5x6):\n";
    for (int r = 0; r < 5; ++r)
    {
      for (int c = 0; c < 6; ++c) std::cout << B[r * 6 + c] << "\t";
      std::cout << "\n";
    }
  }

  
  // Каждый процесс получает одну строку A.
  std::array<double, 5> row{}; // Локальный буфер строки A для текущего процесса (5 элементов)

  // ======================== MPI_Scatter ========================
  // Раздаём куски массива A от root (rank 0) всем процессам.
  // Здесь каждому процессу достается ровно 5 double'ов (одна строка A).
  // Параметры:
  // 1) A.data()      – sendbuf: адрес буфера-источника (имеет смысл только на rank 0)
  // 2) 5             – sendcount: сколько элементов отправить КАЖДОМУ процессу
  // 3) MPI_DOUBLE    – sendtype: тип элементов для отправки
  // 4) row.data()    – recvbuf: куда принимать у каждого процесса
  // 5) 5             – recvcount: сколько элементов принимает каждый процесс
  // 6) MPI_DOUBLE    – recvtype: тип элементов при приёме
  // 7) 0             – root: ранг процесса, который рассылает данные
  // 8) MPI_COMM_WORLD– коммуникатор
  // =============================================================
  MPI_Scatter(A.data(), 5, MPI_DOUBLE,
              row.data(), 5, MPI_DOUBLE,
              0, MPI_COMM_WORLD);

  std::array<double, 6> local{}; // Локальная строка результата (6 элементов)
  local.fill(0.0);               // Явно забиваем нулями (чтобы не было мусора)

  MPI_Barrier(MPI_COMM_WORLD);        // чтобы все стартовали одновременно
  t0 = MPI_Wtime();
  // Каждый процесс считает свою строку C, то есть 6 чисел.
  for (int j = 0; j < 6; ++j) 
  {
    // Указатель на начало j-й строки B^T
    double* column_ptr = transposed.data() + j * 5;

    // ========================= MPI_Bcast =========================
    // Рассылаем один и тот же массив (5 double'ов) всем процессам.
    // ВНИМАНИЕ:
    // - root=0 означает: данные исходно "правильные" на rank 0
    // - на остальных процессах column_ptr указывает на их локальный transposed,
    //   который до этого мог быть не заполнен, но MPI_Bcast его перезапишет.
    // Параметры:
    // 1) column_ptr    – адрес буфера (на root источник, на остальных приёмник)
    // 2) 5             – количество элементов
    // 3) MPI_DOUBLE    – тип элементов
    // 4) 0             – root: кто рассылает
    // 5) MPI_COMM_WORLD– коммуникатор
    // =============================================================
    MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    double acc = 0.0; // Аккумулятор для скалярного произведения (сумма произведений)
    for (int k = 0; k < 5; ++k) // k — общий индекс по внутренней размерности
      acc += row[k] * column_ptr[k];

    local[j] = acc; // Записываем элемент C[rank][j]
  }

  // ======================== MPI_Gather =========================
  // Собираем локальные строки local (по 6 элементов) на root (rank 0) в матрицу C.
  // После gather на rank 0:
  // - C[0..5]     = строка от rank 0
  // - C[6..11]    = строка от rank 1
  // - C[12..17]   = строка от rank 2
  // - C[18..23]   = строка от rank 3
  // Параметры:
  // 1) local.data() – sendbuf: что отправляет каждый процесс
  // 2) 6            – sendcount: сколько элементов отправляет каждый процесс
  // 3) MPI_DOUBLE   – sendtype: тип элементов
  // 4) C.data()     – recvbuf: куда root собирает всё (значимо только на rank 0)
  // 5) 6            – recvcount: сколько элементов принимает root от каждого процесса
  // 6) MPI_DOUBLE   – recvtype: тип элементов при приёме
  // 7) 0            – root: кто собирает
  // 8) MPI_COMM_WORLD – коммуникатор
  // =============================================================
  MPI_Gather(local.data(), 6, MPI_DOUBLE,
             C.data(), 6, MPI_DOUBLE,
             0, MPI_COMM_WORLD);


  MPI_Barrier(MPI_COMM_WORLD);        // чтобы все закончили перед замером
  double t1 = MPI_Wtime();
  double local_time = t1 - t0;

  double max_time = 0.0;
  MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0)
    std::cout << "Время выполнения (max по процессам): " << max_time << " сек\n";

  if (rank == 0) // Печатаем результат только на root
  {
    std::cout << "Матрица C (4x6):\n";
    for (int r = 0; r < 4; ++r) // r — индекс строки C
    {
      for (int c = 0; c < 6; ++c) std::cout << C[r * 6 + c] << "\t";  // c — индекс столбца C
      std::cout << "\n";
    }
  }

  // Завершаем работу MPI. Освобождаются ресурсы MPI-рантайма.
  MPI_Finalize();
  return 0;
}
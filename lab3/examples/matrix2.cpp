#include <mpi.h>

#include <array>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
   // Инициализация среды MPI
  MPI_Init(&argc, &argv);

  int rank{};

  // Получаем ранг текущего процесса
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  int size{};

  // Получаем общее количество процессов
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // A: 4x5  (20 элементов)
  // B: 5x6  (30 элементов)
  // C: 4x6  (24 элемента) – результат умножения A*B
  std::vector<double> A, B, C(4 * 6);
  std::vector<double> transposed(6 * 5);

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

  // проверка что ровно 20 процессов
  if (size != 20)
  {
    if (rank == 0)
      std::cerr << "Ошибка: нужно ровно 20 процессов (4*5). Сейчас: " << size << "\n";
    MPI_Finalize();
    return 1;
  }

  MPI_Comm col_comm{}; // Коммуникатор для "колонок" сетки процессов (5 колонок, в каждой 4 процесса)
  MPI_Comm row_comm{}; // Коммуникатор для "строк"  сетки процессов (4 строки, в каждой 5 процессов)
  
  // Делим MPI_COMM_WORLD на группы по колонкам:
  // color = rank % 5  -> процессы с одинаковым rank%5 попадают в одну "колонку"
  // key   = rank      -> порядок рангов внутри нового коммуникатора
  // Результат: 5 коммуникаторов (колонок), в каждом по 4 процесса (так как 20/5=4)
  // Параметры MPI_Comm_split:
  // 1) MPI_COMM_WORLD – исходный коммуникатор
  // 2) color          – признак группы (rank % 5)
  // 3) key            – порядок процессов внутри группы (rank)
  // 4) &col_comm      – куда записать новый коммуникатор
  MPI_Comm_split(MPI_COMM_WORLD, rank % 5, rank, &col_comm); // 5 колонок по 4 процесса

  // Делим MPI_COMM_WORLD на группы по строкам:
  // color = rank / 5  -> процессы с одинаковым rank/5 попадают в одну "строку"
  // Получаем 4 коммуникатора (строки), в каждом по 5 процессов
  // Параметры MPI_Comm_split аналогичны выше
  MPI_Comm_split(MPI_COMM_WORLD, rank / 5, rank, &row_comm); // 4 строки по 5 процессов

  // процесс одновременно состоит и в одной строке, и в одной колонке.


  // Каждый процесс получает 1 элемент A
  double element{};

  // Рассылаем элементы A по одному на процесс.
  // Параметры MPI_Scatter:
  // 1) A.data()     – sendbuf (только у root=0 содержит валидные данные)
  // 2) 1            – sendcount (сколько элементов отправляет root каждому процессу)
  // 3) MPI_DOUBLE   – тип элементов sendbuf
  // 4) &element     – recvbuf (куда текущий процесс получает свой элемент)
  // 5) 1            – recvcount (сколько элементов получает каждый процесс)
  // 6) MPI_DOUBLE   – тип элементов recvbuf
  // 7) 0            – root (процесс, который раздаёт данные)
  // 8) MPI_COMM_WORLD – коммуникатор
  MPI_Scatter(A.data(), 1, MPI_DOUBLE, &element, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  // local[j] будет хранить одну строку результата (6 чисел) для процессов,
  // которые являются "корнями строк" в row_comm (локальный rank 0 в row_comm).
  std::array<double, 6> local{};
  local.fill(0.0); // Заполняем нулями на всякий случай

  MPI_Barrier(MPI_COMM_WORLD);
  double t0 = MPI_Wtime();
  // Основная идея вычисления C = A * B:
  // - A: 4x5
  // - B: 5x6
  // Здесь это делается так:
  // - каждый процесс знает один A[r][k] (переменная element)
  // - для каждого j (0..5) процессы получают соответствующий B[k][j] через Scatter по row_comm
  // - затем внутри строки делается Reduce (суммирование по k) до C[r][j]
  for (int j = 0; j < 6; ++j) // j – индекс столбца результата C (и индекс строки B^T)
  {
    // Берём j-ю строку B^T (длина 5)
    double* column_ptr = transposed.data() + j * 5;

    // Рассылка 5 элементов (строки B^T) внутри col_comm всем процессам данной колонки.
    // Параметры MPI_Bcast:
    // 1) column_ptr   – адрес буфера (у root уже должны быть корректные данные)
    // 2) 5            – количество элементов
    // 3) MPI_DOUBLE   – тип элементов
    // 4) 0            – root в рамках col_comm (локальный ранг 0 внутри колонки)
    // 5) col_comm     – коммуникатор колонки
    //
    // Примечание:
    // Ветка if/else тут бессмысленная: обе стороны вызывают одинаковый MPI_Bcast.
    // MPI_Bcast обязан вызываться всеми процессами коммуникатора, так что всё равно вызов общий.
    if (rank % 5 == 0)
      MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, col_comm);
    else
      MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, col_comm);

    // В каждой "строке" сетки (row_comm) раздаём по одному элементу столбца
    double other_element{};

    // Scatter внутри row_comm:
    // root (локальный rank 0 в row_comm) отправляет 5 элементов (column_ptr[0..4]),
    // каждый процесс строки получает 1 элемент.
    // Параметры MPI_Scatter:
    // 1) column_ptr   – sendbuf (у root строки должен быть корректный массив из 5 значений)
    // 2) 1            – sendcount (по 1 элементу каждому процессу)
    // 3) MPI_DOUBLE   – тип sendbuf
    // 4) &other_element – recvbuf (куда получаем B[k][j])
    // 5) 1            – recvcount
    // 6) MPI_DOUBLE   – тип recvbuf
    // 7) 0            – root в рамках row_comm
    // 8) row_comm     – коммуникатор строки
    MPI_Scatter(column_ptr, 1, MPI_DOUBLE, &other_element, 1, MPI_DOUBLE, 0, row_comm);

    // Локальное произведение одного элемента A[r][k] на соответствующий B[k][j]
    double prod = element * other_element;

    // Складываем 5 произведений в один результат на root каждой строки (локальный rank 0 в row_comm)
    double result{};

    // Параметры MPI_Reduce:
    // 1) &prod        – sendbuf (локальное значение каждого процесса)
    // 2) &result      – recvbuf (куда root положит результат суммы)
    // 3) 1            – количество элементов
    // 4) MPI_DOUBLE   – тип данных
    // 5) MPI_SUM      – операция редукции (сумма)
    // 6) 0            – root (локальный rank 0 в row_comm)
    // 7) row_comm     – коммуникатор строки
    MPI_Reduce(&prod, &result, 1, MPI_DOUBLE, MPI_SUM, 0, row_comm);

    // Определяем локальный ранг внутри row_comm, чтобы понять, кто root строки
    int row_rank{};

    // Параметры MPI_Comm_rank:
    // 1) row_comm – коммуникатор строки
    // 2) &row_rank – куда записать локальный ранг
    MPI_Comm_rank(row_comm, &row_rank);

    // Только root строки имеет валидный result после MPI_Reduce,
    // поэтому сохраняем результат только там.
    if (row_rank == 0)
      local[j] = result;
  }

  // Теперь нужно собрать итоговую матрицу C у "корней колонок".
  // Идея: в каждой колонке col_comm у нас 4 строки результата (по 6 чисел каждая),
  // и мы собираем их через MPI_Gather в массив C на root этой колонки.
  //
  // ВАЖНО:
  // Root в col_comm это локальный rank 0 (а не обязательно глобальный rank 0).
  // Но печать дальше делает глобальный rank 0, так что здесь легко получить несостыковку
  // (в зависимости от того, как распределены данные). Мы комментарии пишем, не спасаем архитектуру.

  // Сбор 4 массивов local[6] (от 4 процессов колонки) в C на root колонки.
  // Параметры MPI_Gather:
  // 1) local.data()  – sendbuf (каждый процесс отправляет 6 чисел)
  // 2) 6             – sendcount
  // 3) MPI_DOUBLE    – тип sendbuf
  // 4) C.data()      – recvbuf (только у root колонки будет заполнено)
  // 5) 6             – recvcount (root получает по 6 чисел от каждого процесса)
  // 6) MPI_DOUBLE    – тип recvbuf
  // 7) 0             – root в рамках col_comm
  // 8) col_comm      – коммуникатор колонки
  MPI_Gather(local.data(), 6, MPI_DOUBLE, C.data(), 6, MPI_DOUBLE, 0, col_comm);

  MPI_Barrier(MPI_COMM_WORLD);
  double t1 = MPI_Wtime();
  double local_time = t1 - t0;

  double max_time = 0.0;
  MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if (rank == 0)
    std::cout << "Время выполнения (max по процессам): " << max_time << " сек\n";
  
    // Освобождаем созданные коммуникаторы (хороший тон, чтобы не плодить ресурсы)
  MPI_Comm_free(&col_comm);
  MPI_Comm_free(&row_comm);

  // Печатает только глобальный rank 0
  if (rank == 0)
  {
    std::cout << "Матрица C (4x6):\n";
    for (int r = 0; r < 4; ++r)
    {
      for (int c = 0; c < 6; ++c) std::cout << C[r * 6 + c] << "\t";
      std::cout << "\n";
    }
  }

  // Завершение работы MPI 
  MPI_Finalize();
  return 0;
}
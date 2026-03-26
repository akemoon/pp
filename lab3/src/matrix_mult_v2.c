#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

// A: 4x5, B: 5x6, C: 4x6
// 20 процессов = 4 строки * 5 столбцов матрицы A
// каждый процесс отвечает за один элемент A[rank/5][rank%5]
#define ROWS_A 4
#define COLS_A 5
#define ROWS_B 5
#define COLS_B 6
#define NPROCS (ROWS_A * COLS_A)  // 20

void printMatrix(double* matrix, int rows, int cols, const char* name) {
    printf("%s:\n", name);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%8.1f ", matrix[i * cols + j]);
        }
        printf("\n");
    }
    printf("\n");
}

int main(int argc, char* argv[]) {
    int rank, size;

    // Инициализация MPI-среды. Должна быть вызвана до любых других MPI-функций.
    MPI_Init(&argc, &argv);

    // Получаем ранг текущего процесса - номер от 0 до size-1.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Получаем общее количество процессов в коммуникаторе.
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != NPROCS) {
        if (rank == 0)
            printf("Нужно ровно %d процессов (%d*%d).\n", NPROCS, ROWS_A, COLS_A);
        MPI_Finalize();
        return 1;
    }

    double *A = NULL, *B = NULL;
    double *transposed = (double*)malloc(COLS_B * COLS_A * sizeof(double));

    // C выделяем у всех - каждый root своей колонки пишет в него результат через MPI_Gather
    double *C = (double*)malloc(ROWS_A * COLS_B * sizeof(double));

    if (rank == 0) {
        A = (double*)malloc(ROWS_A * COLS_A * sizeof(double));
        B = (double*)malloc(ROWS_B * COLS_B * sizeof(double));

        for (int i = 0; i < ROWS_A * COLS_A; i++) A[i] = i + 1;
        for (int i = 0; i < ROWS_B * COLS_B; i++) B[i] = i + 1;

        // Транспонируем B (5x6) -> transposed (6x5)
        for (int i = 0; i < ROWS_B; i++)
            for (int j = 0; j < COLS_B; j++)
                transposed[j * COLS_A + i] = B[i * COLS_B + j];

        printMatrix(A, ROWS_A, COLS_A, "Matrix A");
        printMatrix(B, ROWS_B, COLS_B, "Matrix B");
    }

    // ======================= MPI_Comm_split =======================
    // Разделяет MPI_COMM_WORLD на подгруппы по значению color.
    // col_comm - вертикальные группы (одинаковый rank%5): {0,5,10,15}, {1,6,11,16} и т.д.
    // Параметры:
    //   1) MPI_COMM_WORLD - (IN)  исходный коммуникатор
    //   2) rank % COLS_A  - (IN)  color - процессы с одинаковым color попадают в одну группу
    //   3) rank           - (IN)  key - порядок рангов внутри новой группы
    //   4) &col_comm      - (OUT) новый коммуникатор
    // ==============================================================
    MPI_Comm col_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank % COLS_A, rank, &col_comm);

    // row_comm - горизонтальные группы (одинаковый rank/5): {0,1,2,3,4}, {5,6,7,8,9} и т.д.
    MPI_Comm row_comm;
    MPI_Comm_split(MPI_COMM_WORLD, rank / COLS_A, rank, &row_comm);

    // ======================= MPI_Scatter =======================
    // Каждый процесс получает один элемент матрицы A.
    // Параметры:
    //   1) A           - (IN)  буфер-источник на rank 0
    //   2) 1           - (IN)  сколько элементов отправить каждому процессу
    //   3) MPI_DOUBLE  - (IN)  тип отправляемых элементов
    //   4) &element    - (OUT) буфер приёма у каждого процесса
    //   5) 1           - (IN)  сколько элементов принять
    //   6) MPI_DOUBLE  - (IN)  тип принимаемых элементов
    //   7) 0           - (IN)  root - кто раздаёт данные
    //   8) MPI_COMM_WORLD - (IN) коммуникатор
    // ===========================================================
    double element = 0.0;
    MPI_Scatter(A, 1, MPI_DOUBLE, &element, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    double local[COLS_B];
    for (int j = 0; j < COLS_B; j++) local[j] = 0.0;

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    for (int j = 0; j < COLS_B; j++) {
        double* col_ptr = transposed + j * COLS_A;

        // ======================= MPI_Bcast =======================
        // Рассылает j-й столбец B внутри колонки процессов (col_comm).
        // Нужно чтобы каждый процесс знал свой элемент B[k][j].
        // Параметры:
        //   1) col_ptr  - (IN/OUT) буфер: на root - источник, на остальных - приёмник
        //   2) COLS_A   - (IN)     количество элементов
        //   3) MPI_DOUBLE - (IN)   тип элементов
        //   4) 0        - (IN)     root внутри col_comm
        //   5) col_comm - (IN)     коммуникатор колонки
        // =========================================================
        MPI_Bcast(col_ptr, COLS_A, MPI_DOUBLE, 0, col_comm);

        // ======================= MPI_Scatter =======================
        // Внутри строки процессов раздаёт по одному элементу столбца B.
        // После вызова каждый процесс строки имеет свой B[k][j].
        // Параметры:
        //   1) col_ptr      - (IN)  буфер-источник на root строки
        //   2) 1            - (IN)  сколько элементов отправить каждому
        //   3) MPI_DOUBLE   - (IN)  тип элементов
        //   4) &b_elem      - (OUT) буфер приёма у каждого процесса
        //   5) 1            - (IN)  сколько элементов принять
        //   6) MPI_DOUBLE   - (IN)  тип элементов
        //   7) 0            - (IN)  root внутри row_comm
        //   8) row_comm     - (IN)  коммуникатор строки
        // ===========================================================
        double b_elem = 0.0;
        MPI_Scatter(col_ptr, 1, MPI_DOUBLE, &b_elem, 1, MPI_DOUBLE, 0, row_comm);

        double prod = element * b_elem;

        // ======================= MPI_Reduce =======================
        // Суммирует произведения A[r][k]*B[k][j] по всей строке процессов.
        // Результат - элемент C[r][j] - записывается на root строки.
        // Параметры:
        //   1) &prod    - (IN)  локальное произведение каждого процесса
        //   2) &result  - (OUT) сумма на root строки
        //   3) 1        - (IN)  количество элементов
        //   4) MPI_DOUBLE - (IN) тип элементов
        //   5) MPI_SUM  - (IN)  операция - суммирование
        //   6) 0        - (IN)  root внутри row_comm
        //   7) row_comm - (IN)  коммуникатор строки
        // =========================================================
        double result = 0.0;
        MPI_Reduce(&prod, &result, 1, MPI_DOUBLE, MPI_SUM, 0, row_comm);

        int row_rank;
        MPI_Comm_rank(row_comm, &row_rank);
        if (row_rank == 0)
            local[j] = result;
    }

    // ======================= MPI_Gather =======================
    // Собирает строки результата от root-ов строк в матрицу C на root колонки.
    // Параметры:
    //   1) local    - (IN)  что отправляет каждый процесс (COLS_B элементов)
    //   2) COLS_B   - (IN)  сколько элементов отправляет каждый процесс
    //   3) MPI_DOUBLE - (IN) тип элементов
    //   4) C        - (OUT) буфер приёма на root колонки
    //   5) COLS_B   - (IN)  сколько элементов принять от каждого
    //   6) MPI_DOUBLE - (IN) тип элементов
    //   7) 0        - (IN)  root внутри col_comm
    //   8) col_comm - (IN)  коммуникатор колонки
    // =========================================================
    MPI_Gather(local, COLS_B, MPI_DOUBLE, C, COLS_B, MPI_DOUBLE, 0, col_comm);

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();
    double local_time = t1 - t0;

    // Берём максимальное время среди всех процессов
    double max_time = 0.0;
    MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Время выполнения (max по процессам): %f сек\n\n", max_time);
        printMatrix(C, ROWS_A, COLS_B, "Matrix C = A * B");
        free(A);
        free(B);
    }

    free(C);
    free(transposed);
    MPI_Comm_free(&col_comm);
    MPI_Comm_free(&row_comm);
    MPI_Finalize();
    return 0;
}

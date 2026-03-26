#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#define ROWS_A 4
#define COLS_A 5
#define ROWS_B 5
#define COLS_B 6

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

    if (size != ROWS_A) {
        if (rank == 0)
            printf("Нужно ровно %d процесса (по числу строк A).\n", ROWS_A);
        MPI_Finalize();
        return 1;
    }

    double *A = NULL, *B = NULL, *C = NULL;
    // Транспонированная B (COLS_B x COLS_A) - удобнее передавать столбцы как строки
    double *transposed = (double*)malloc(COLS_B * COLS_A * sizeof(double));

    if (rank == 0) {
        A = (double*)malloc(ROWS_A * COLS_A * sizeof(double));
        B = (double*)malloc(ROWS_B * COLS_B * sizeof(double));
        C = (double*)malloc(ROWS_A * COLS_B * sizeof(double));

        for (int i = 0; i < ROWS_A * COLS_A; i++) A[i] = i + 1;
        for (int i = 0; i < ROWS_B * COLS_B; i++) B[i] = i + 1;

        // Транспонируем B (5x6) -> transposed (6x5)
        for (int i = 0; i < ROWS_B; i++)
            for (int j = 0; j < COLS_B; j++)
                transposed[j * COLS_A + i] = B[i * COLS_B + j];

        printMatrix(A, ROWS_A, COLS_A, "Matrix A");
        printMatrix(B, ROWS_B, COLS_B, "Matrix B");
    }

    // ======================= MPI_Scatter =======================
    // Раздаёт части массива A от root (rank 0) всем процессам.
    // Каждому процессу достаётся ровно COLS_A элементов - одна строка A.
    // Параметры:
    //   1) A           - (IN)  буфер-источник, содержит все данные на rank 0
    //   2) COLS_A      - (IN)  сколько элементов отправить каждому процессу
    //   3) MPI_DOUBLE  - (IN)  тип отправляемых элементов
    //   4) row         - (OUT) буфер приёма у каждого процесса
    //   5) COLS_A      - (IN)  сколько элементов принять
    //   6) MPI_DOUBLE  - (IN)  тип принимаемых элементов
    //   7) 0           - (IN)  root - кто раздаёт данные
    //   8) MPI_COMM_WORLD - (IN) коммуникатор
    // ===========================================================
    double row[COLS_A];
    MPI_Scatter(A, COLS_A, MPI_DOUBLE,
                row, COLS_A, MPI_DOUBLE,
                0, MPI_COMM_WORLD);

    double local[COLS_B];

    // Барьер перед замером времени - все процессы стартуют одновременно
    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    for (int j = 0; j < COLS_B; j++) {
        double* col_ptr = transposed + j * COLS_A;

        // ======================= MPI_Bcast =======================
        // Рассылает j-й столбец B (строку транспонированной) всем процессам.
        // Параметры:
        //   1) col_ptr     - (IN/OUT) буфер: на root - источник, на остальных - приёмник
        //   2) COLS_A      - (IN)     количество элементов
        //   3) MPI_DOUBLE  - (IN)     тип элементов
        //   4) 0           - (IN)     root - кто рассылает
        //   5) MPI_COMM_WORLD - (IN)  коммуникатор
        // =========================================================
        MPI_Bcast(col_ptr, COLS_A, MPI_DOUBLE, 0, MPI_COMM_WORLD);

        local[j] = 0.0;
        for (int k = 0; k < COLS_A; k++)
            local[j] += row[k] * col_ptr[k];
    }

    // ======================= MPI_Gather =======================
    // Собирает локальные строки результата от всех процессов в матрицу C на root.
    // После вызова на rank 0: C[0..5] - строка от rank 0, C[6..11] - от rank 1, и т.д.
    // Параметры:
    //   1) local       - (IN)  что отправляет каждый процесс (COLS_B элементов)
    //   2) COLS_B      - (IN)  сколько элементов отправляет каждый процесс
    //   3) MPI_DOUBLE  - (IN)  тип отправляемых элементов
    //   4) C           - (OUT) буфер приёма на root
    //   5) COLS_B      - (IN)  сколько элементов принять от каждого процесса
    //   6) MPI_DOUBLE  - (IN)  тип принимаемых элементов
    //   7) 0           - (IN)  root - кто собирает
    //   8) MPI_COMM_WORLD - (IN) коммуникатор
    // =========================================================
    MPI_Gather(local, COLS_B, MPI_DOUBLE,
               C, COLS_B, MPI_DOUBLE,
               0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();
    double local_time = t1 - t0;

    // ======================= MPI_Reduce =======================
    // Берёт максимальное время среди всех процессов - реальное время выполнения.
    // Параметры:
    //   1) &local_time - (IN)  локальное значение каждого процесса
    //   2) &max_time   - (OUT) результат редукции (только на root)
    //   3) 1           - (IN)  количество элементов
    //   4) MPI_DOUBLE  - (IN)  тип элементов
    //   5) MPI_MAX     - (IN)  операция - берём максимум
    //   6) 0           - (IN)  root - кто получает результат
    //   7) MPI_COMM_WORLD - (IN) коммуникатор
    // =========================================================
    double max_time = 0.0;
    MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Время выполнения (max по процессам): %f сек\n\n", max_time);
        printMatrix(C, ROWS_A, COLS_B, "Matrix C = A * B");
        free(A);
        free(B);
        free(C);
    }

    free(transposed);
    MPI_Finalize();
    return 0;
}

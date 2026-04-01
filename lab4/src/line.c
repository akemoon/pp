#include <stdio.h>
#include <string.h>
#include <mpi.h>

#define MATRIX_SIZE 5

int main(int argc, char **argv) {
    int rank, size;
    MPI_Comm line_comm;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != MATRIX_SIZE) {
        if (rank == 0)
            printf("Нужно ровно %d процессов\n", MATRIX_SIZE);
        MPI_Finalize();
        return 1;
    }

    // Создаем топологию "линейка": 1D решетка, periods={0} - незамкнутая.
    // У крайних процессов MPI_Cart_shift вернет MPI_PROC_NULL для отсутствующего соседа.
    int dims[1]    = {MATRIX_SIZE};
    int periods[1] = {0};
    MPI_Cart_create(MPI_COMM_WORLD, 1, dims, periods, 0, &line_comm);

    // left_rank - сосед слева, right_rank - сосед справа (MPI_PROC_NULL если нет)
    int left_rank, right_rank;
    MPI_Cart_shift(line_comm, 0, 1, &left_rank, &right_rank);

    int matrix[MATRIX_SIZE * MATRIX_SIZE] = {0};
    int vector[MATRIX_SIZE] = {0};

    if (rank == 0) {
        printf("Матрица:\n");
        for (int i = 0; i < MATRIX_SIZE; i++) {
            for (int j = 0; j < MATRIX_SIZE; j++) {
                matrix[i * MATRIX_SIZE + j] = i * MATRIX_SIZE + j + 1;
                printf("%d ", matrix[i * MATRIX_SIZE + j]);
            }
            printf("\n");
        }
        printf("Вектор:\n");
        for (int i = 0; i < MATRIX_SIZE; i++) {
            vector[i] = i + 1;
            printf("%d ", vector[i]);
        }
        printf("\n");
    }

    // Раздача строк матрицы по цепочке слева направо.
    // P(0) отправляет все строки P(1), тег = номер строки.
    // Каждый процесс забирает свою строку и пробрасывает остальные дальше.
    int local_row[MATRIX_SIZE];
    if (rank == 0) {
        memcpy(local_row, &matrix[0], MATRIX_SIZE * sizeof(int));
        for (int i = 1; i < MATRIX_SIZE; i++)
            MPI_Send(&matrix[i * MATRIX_SIZE], MATRIX_SIZE, MPI_INT, right_rank, i, line_comm);
    } else {
        MPI_Recv(local_row, MATRIX_SIZE, MPI_INT, left_rank, rank, line_comm, MPI_STATUS_IGNORE);
        for (int i = rank + 1; i < MATRIX_SIZE; i++) {
            int tmp[MATRIX_SIZE];
            MPI_Recv(tmp, MATRIX_SIZE, MPI_INT, left_rank, i, line_comm, MPI_STATUS_IGNORE);
            if (right_rank != MPI_PROC_NULL)
                MPI_Send(tmp, MATRIX_SIZE, MPI_INT, right_rank, i, line_comm);
        }
    }

    // Умножение строки на вектор.
    // P(0) прокачивает элементы вектора по линейке слева направо.
    // На шаге i каждый процесс получает vector[i] от соседа слева,
    // умножает на local_row[i] и передает вправо.
    int local_result = 0;
    for (int i = 0; i < MATRIX_SIZE; i++) {
        int elem;
        if (rank == 0) {
            elem = vector[i];
        } else {
            MPI_Recv(&elem, 1, MPI_INT, left_rank, 0, line_comm, MPI_STATUS_IGNORE);
        }
        local_result += local_row[i] * elem;
        if (right_rank != MPI_PROC_NULL)
            MPI_Send(&elem, 1, MPI_INT, right_rank, 0, line_comm);
    }

    // Сбор результатов по линейке справа налево.
    // Последний процесс начинает: отправляет массив результатов P(3),
    // каждый процесс вписывает свой результат и передает P(rank-1),
    // P(0) получает итоговый массив.
    int result[MATRIX_SIZE] = {0};
    result[rank] = local_result;
    if (rank == MATRIX_SIZE - 1) {
        MPI_Send(result, MATRIX_SIZE, MPI_INT, left_rank, 2, line_comm);
    } else if (rank > 0) {
        MPI_Recv(result, MATRIX_SIZE, MPI_INT, right_rank, 2, line_comm, MPI_STATUS_IGNORE);
        result[rank] = local_result;
        MPI_Send(result, MATRIX_SIZE, MPI_INT, left_rank, 2, line_comm);
    } else {
        MPI_Recv(result, MATRIX_SIZE, MPI_INT, right_rank, 2, line_comm, MPI_STATUS_IGNORE);
        result[0] = local_result;
    }

    if (rank == 0) {
        printf("Результат (линейка):\n");
        for (int i = 0; i < MATRIX_SIZE; i++)
            printf("%d ", result[i]);
        printf("\n");
    }

    MPI_Comm_free(&line_comm);
    MPI_Finalize();
    return 0;
}

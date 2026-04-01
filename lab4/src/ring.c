#include <stdio.h>
#include <string.h>
#include <mpi.h>

#define MATRIX_SIZE 5

int main(int argc, char **argv) {
    int rank, size;
    MPI_Comm ring_comm;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size != MATRIX_SIZE) {
        if (rank == 0)
            printf("Нужно ровно %d процессов\n", MATRIX_SIZE);
        MPI_Finalize();
        return 1;
    }

    // Создаём топологию "кольцо" (замкнутая 1D решётка)
    int dims[1]    = {MATRIX_SIZE};
    int periods[1] = {1};
    MPI_Cart_create(MPI_COMM_WORLD, 1, dims, periods, 0, &ring_comm);

    int left_rank, right_rank;
    MPI_Cart_shift(ring_comm, 0, 1, &left_rank, &right_rank);

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

    // Раздаём строки матрицы вдоль кольца (pipeline: 0->1->2->3->4)
    // Все строки идут через right_rank с тегом = номер строки
    int local_row[MATRIX_SIZE];
    if (rank == 0) {
        memcpy(local_row, &matrix[0], MATRIX_SIZE * sizeof(int));
        for (int i = 1; i < MATRIX_SIZE; i++)
            MPI_Send(&matrix[i * MATRIX_SIZE], MATRIX_SIZE, MPI_INT, right_rank, i, ring_comm);
    } else {
        // Получаем свою строку
        MPI_Recv(local_row, MATRIX_SIZE, MPI_INT, left_rank, rank, ring_comm, MPI_STATUS_IGNORE);
        // Пробрасываем строки для следующих процессов
        for (int i = rank + 1; i < MATRIX_SIZE; i++) {
            int tmp[MATRIX_SIZE];
            MPI_Recv(tmp, MATRIX_SIZE, MPI_INT, left_rank, i, ring_comm, MPI_STATUS_IGNORE);
            MPI_Send(tmp, MATRIX_SIZE, MPI_INT, right_rank, i, ring_comm);
        }
    }

    // Раздаём элементы вектора вдоль кольца (pipeline, тег 10+i)
    int vector_elem;
    if (rank == 0) {
        vector_elem = vector[0];
        for (int i = 1; i < MATRIX_SIZE; i++)
            MPI_Send(&vector[i], 1, MPI_INT, right_rank, 10 + i, ring_comm);
    } else {
        MPI_Recv(&vector_elem, 1, MPI_INT, left_rank, 10 + rank, ring_comm, MPI_STATUS_IGNORE);
        for (int i = rank + 1; i < MATRIX_SIZE; i++) {
            int tmp;
            MPI_Recv(&tmp, 1, MPI_INT, left_rank, 10 + i, ring_comm, MPI_STATUS_IGNORE);
            MPI_Send(&tmp, 1, MPI_INT, right_rank, 10 + i, ring_comm);
        }
    }

    // Умножение: прокачиваем элементы вектора по кольцу
    // Каждый шаг: умножаем нужный элемент строки на текущий элемент вектора,
    // затем передаём элемент вектора вправо и получаем следующий слева
    int local_result = 0;
    for (int i = 0; i < MATRIX_SIZE; i++) {
        local_result += local_row[(rank + MATRIX_SIZE - i) % MATRIX_SIZE] * vector_elem;

        int next_elem;
        MPI_Sendrecv(&vector_elem, 1, MPI_INT, right_rank, 0,
                     &next_elem,   1, MPI_INT, left_rank,  0,
                     ring_comm, MPI_STATUS_IGNORE);
        vector_elem = next_elem;
    }

    // Сбор результатов через кольцо (relay в направлении возрастания рангов)
    // rank=1 отправляет rank=2, rank=2 пробрасывает + свой -> rank=3, ..., rank=4 -> rank=0
    int result[MATRIX_SIZE] = {0};
    if (rank == 0) {
        result[0] = local_result;
        for (int i = 1; i < MATRIX_SIZE; i++)
            MPI_Recv(&result[i], 1, MPI_INT, left_rank, 1, ring_comm, MPI_STATUS_IGNORE);
    } else {
        // Пробрасываем результаты процессов с меньшим рангом
        for (int i = 0; i < rank - 1; i++) {
            int fwd;
            MPI_Recv(&fwd, 1, MPI_INT, left_rank,  1, ring_comm, MPI_STATUS_IGNORE);
            MPI_Send(&fwd, 1, MPI_INT, right_rank, 1, ring_comm);
        }
        // Отправляем свой результат
        MPI_Send(&local_result, 1, MPI_INT, right_rank, 1, ring_comm);
    }

    if (rank == 0) {
        printf("Результат (кольцо):\n");
        for (int i = 0; i < MATRIX_SIZE; i++)
            printf("%d ", result[i]);
        printf("\n");
    }

    MPI_Comm_free(&ring_comm);
    MPI_Finalize();
    return 0;
}

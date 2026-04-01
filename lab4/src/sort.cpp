#include <stdio.h>
#include <mpi.h>
#include <limits.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <chrono>

using namespace std;

#define SIZE 5      // размер массива
#define DIMS_DG 2   // размерность решетки для ГЗ (2D)
#define DIMS_SFG 1  // размерность решетки для ГПС (1D)

int receive(int source, MPI_Comm comm);
void print_array(const char* label, int* array, int size);


// Граф зависимости: 2D решетка SIZE x SIZE.
// Работают только процессы где row <= col (верхний треугольник).
// max течет вниз по столбцам, min течет вправо по строкам.
// Результат собирается из последнего столбца.
int *sort_dependency_graph(int input_array[SIZE], int rank, MPI_Comm comm) {
    int min, max;
    int coords[2];
    // Узнаем координаты текущего процесса в 2D решетке по его рангу
    MPI_Cart_coords(comm, rank, DIMS_DG, coords);
    // coords[0] = row, coords[1] = col

    if (coords[0] <= coords[1]) {
        int y_source, y_dest;  // соседи по вертикали (вверх/вниз)
        int x_source, x_dest;  // соседи по горизонтали (влево/вправо)
        // Ось 0 - строки: y_source = сосед выше, y_dest = сосед ниже
        MPI_Cart_shift(comm, 0, 1, &y_source, &y_dest);
        // Ось 1 - столбцы: x_source = сосед слева, x_dest = сосед справа
        MPI_Cart_shift(comm, 1, 1, &x_source, &x_dest);

        // На диагонали нет левого соседа - min = INT_MAX (нейтраль)
        min = (coords[0] == coords[1]) ? INT_MAX : receive(x_source, comm);

        // Получаем max: первая строка берет из массива, остальные - от соседа сверху
        if (coords[0] == 0) {
            if (coords[1] == 0) {
                // P(0,0) раздает элементы всей первой строке
                max = input_array[0];
                for (int i = 1; i < SIZE; ++i)
                    MPI_Send(&input_array[i], 1, MPI_INT, i, 0, comm);
            } else {
                max = receive(0, comm);
            }
        } else {
            max = receive(y_source, comm);
        }

        // Основная операция: меньшее уходит вправо, большее - вниз
        if (min > max) swap(min, max);

        // min уходит вправо (последний столбец отправляет к P(0,0))
        if (x_dest < 0)
            MPI_Send(&min, 1, MPI_INT, 0, 0, comm);
        else
            MPI_Send(&min, 1, MPI_INT, x_dest, 0, comm);

        // max уходит вниз (на диагонали некуда - там конец пути)
        if (coords[0] != coords[1])
            MPI_Send(&max, 1, MPI_INT, y_dest, 0, comm);

        // P(0,0) собирает результаты из последнего столбца
        if (rank == 0) {
            int *result = new int[SIZE];
            int target = 0;
            for (int i = 0; i < SIZE; ++i) {
                if (i == 0)
                    target += SIZE - 1;
                else
                    target += SIZE;
                result[i] = receive(target, comm);
            }
            return result;
        }
    }
    return nullptr;
}

// Граф потока сигналов: 1D цепочка из SIZE процессов.
// Элементы массива поочередно проходят по цепочке P(0)->P(1)->...->P(4).
// Каждый процесс оставляет у себя минимум из всех прошедших через него значений.
int *sort_signal_flow(int input_array[SIZE], int rank, MPI_Comm comm) {
    int min = INT_MAX;
    int source, dest;
    // Ось 0, шаг 1: source = сосед слева (откуда получаем), dest = сосед справа (куда отправляем)
    MPI_Cart_shift(comm, 0, 1, &source, &dest);

    for (int i = 0; i < SIZE; ++i) {
        // P(0) берет очередной элемент, остальные получают от соседа слева
        int max = rank == 0 ? input_array[i] : receive(source, comm);

        // Если пришедшее значение меньше локального min - меняем местами
        if (min > max) swap(min, max);

        // Передаем max дальше по цепочке
        if (dest >= 0)
            MPI_Send(&max, 1, MPI_INT, dest, 0, comm);
    }

    // Сбор: каждый отправляет свой min к P(0)
    int *result = rank == 0 ? new int[SIZE] : nullptr;
    if (rank == 0) {
        result[0] = min;
        for (int i = 1; i < SIZE; i++)
            result[i] = receive(i, comm);
    } else {
        MPI_Send(&min, 1, MPI_INT, 0, 0, comm);
    }
    return result;
}

int main(int argc, char **argv) {
    int rank, size;
    MPI_Comm comm;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Выбор алгоритма по числу процессов:
    // size > SIZE -> ГЗ (2D решетка, нужно SIZE*SIZE процессов)
    // size == SIZE -> ГПС (1D цепочка)
    bool use_dependency_graph = (size > SIZE);
    int input_array[SIZE] = {2, 5, 3, 1, 4};
    int *result = nullptr;
    auto start_time = chrono::high_resolution_clock::now();

    if (rank == 0)
        print_array("Input array", input_array, SIZE);

    if (use_dependency_graph) {
        // {0, 0} - размеры 2D решетки, 0 = подобрать автоматически (-> {5,5} для 25 процессов)
        int dims[DIMS_DG] = {0, 0};
        // {0, 0} - незамкнутая по обоим измерениям (0=открытая, 1=замкнутая/кольцо)
        int periods[DIMS_DG] = {0, 0};
        // подбирает dims под число процессов: size=25, ndims=2 -> dims={5,5}
        MPI_Dims_create(size, DIMS_DG, dims);
        // создает виртуальную 2D топологию: каждому процессу назначаются координаты (row, col)
        MPI_Cart_create(MPI_COMM_WORLD, DIMS_DG, dims, periods, 1, &comm);

        if (rank == 0)
            cout << "Using dependency graph approach with " << dims[0] << "x" << dims[1] << " topology" << endl;

        result = sort_dependency_graph(input_array, rank, comm);
    } else {
        // {0} - размер 1D цепочки, 0 = подобрать автоматически (-> {5} для 5 процессов)
        int dims[DIMS_SFG] = {0};
        // {0} - незамкнутая цепочка (линейка); {1} дало бы кольцо: P(4) соединен с P(0)
        int periods[DIMS_SFG] = {0};
        // подбирает dims под число процессов: size=5, ndims=1 -> dims={5}
        MPI_Dims_create(size, DIMS_SFG, dims);
        // создает виртуальную 1D топологию: P(0)-P(1)-P(2)-P(3)-P(4)
        MPI_Cart_create(MPI_COMM_WORLD, DIMS_SFG, dims, periods, 1, &comm);

        if (rank == 0)
            cout << "Using signal flow graph approach with " << dims[0] << " processes" << endl;

        result = sort_signal_flow(input_array, rank, comm);
    }

    if (rank == 0) {
        auto end_time = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time);
        print_array("Sorted array", result, SIZE);
        cout << "Sorting completed in " << duration.count() << " microseconds" << endl;
        delete[] result;
    }

    MPI_Finalize();
    return 0;
}

void print_array(const char* label, int* array, int size) {
    cout << label << ": ";
    for (int i = 0; i < size; ++i)
        cout << setw(3) << array[i] << " ";
    cout << endl;
}

int receive(int source, MPI_Comm comm) {
    int value;
    MPI_Status status;
    MPI_Recv(&value, 1, MPI_INT, source, MPI_ANY_TAG, comm, &status);
    return value;
}

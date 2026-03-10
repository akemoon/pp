#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define NUM_CARS 5
#define NUM_STAGES 3
#define TRACK_LENGTH 70
#define MAX_SPEED 6

#define SEM_READY 0
#define SEM_START 1

// Сообщение для арбитра
struct msg_buffer {
    long msg_type;
    int car_id;
    int stage;
    int position;
    int stage_time;
    int finished;
};

// Структура для хранения состояния автомобиля 
typedef struct {
    int id;
    int position;
    int stage_times[NUM_STAGES];
    int total_time;
    int points;
    int rank;
} Car;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// Выполняет одну операцию над выбранным семафором:
// увеличение, уменьшение или ожидание.
static void sem_change(int semid, unsigned short sem_num, short sem_op) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = sem_op;
    op.sem_flg = 0;

    if (semop(semid, &op, 1) == -1) {
        die("semop");
    }
}

static void sem_wait_n(int semid, unsigned short sem_num, short n) {
    sem_change(semid, sem_num, -n);
}

static void sem_post_n(int semid, unsigned short sem_num, short n) {
    sem_change(semid, sem_num, n);
}

static void clear_screen(void) {
    printf("\033[H\033[J");
}

void display_race(Car cars[], int current_stage) {
    clear_screen();
    printf("Этап %d - Ход гонки\n", current_stage);
    printf("===================\n\n");

    for (int i = 0; i < NUM_CARS; i++) {
        printf("Автомобиль %d: ", cars[i].id);

        for (int j = 0; j < TRACK_LENGTH; j++) {
            if (j == cars[i].position && cars[i].position < TRACK_LENGTH) {
                printf(">");
            } else if (j < cars[i].position) {
                printf("-");
            } else {
                printf(" ");
            }
        }

        printf("| %3d%%\n", (cars[i].position * 100) / TRACK_LENGTH);
    }

    printf("\nТаблица результатов:\n");
    printf("+--------------+");
    for (int j = 1; j < current_stage; j++) {
        printf("----------+");
    }
    printf("----------+----------+\n");

    printf("| Автомобиль   |");
    for (int j = 1; j < current_stage; j++) {
        printf(" Этап %d   |", j);
    }
    printf(" Текущий  |  Очки   |\n");

    printf("+--------------+");
    for (int j = 1; j < current_stage; j++) {
        printf("----------+");
    }
    printf("----------+----------+\n");

    for (int i = 0; i < NUM_CARS; i++) {
        printf("| Автомобиль %d  |", cars[i].id);

        for (int j = 1; j < current_stage; j++) {
            printf(" %8d |", cars[i].stage_times[j - 1]);
        }

        printf(" %7d%% |", (cars[i].position * 100) / TRACK_LENGTH);
        printf(" %7d |\n", cars[i].points);
    }

    printf("+--------------+");
    for (int j = 1; j < current_stage; j++) {
        printf("----------+");
    }
    printf("----------+----------+\n");
}

static void calculate_points(Car cars[], int stage) {
    Car temp[NUM_CARS];

    // Копируем массив машин во временный массив,
    // чтобы сортировать его и не портить исходный порядок.
    memcpy(temp, cars, sizeof(temp));

    // Сортируем машины по времени текущего этапа:
    // меньшее stage_time => лучше результат.
    for (int i = 0; i < NUM_CARS - 1; i++) {
        for (int j = 0; j < NUM_CARS - i - 1; j++) {
            if (temp[j].stage_times[stage - 1] > temp[j + 1].stage_times[stage - 1]) {
                Car t = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = t;
            }
        }
    }

    // Очки за места:
    // 1 место -> 10, 2 -> 8, 3 -> 6, 4 -> 4, 5 -> 2.
    int points[NUM_CARS] = {10, 8, 6, 4, 2};

    // После сортировки temp[i] — это машина на i-м месте.
    // Находим её индекс в исходном массиве cars через id
    // и добавляем соответствующие очки.
    for (int i = 0; i < NUM_CARS; i++) {
        int idx = temp[i].id - 1;
        cars[idx].points += points[i];
    }
}

static void wait_for_stage_start(int semid) {
    // Машина сообщает, что готова к старту
    sem_post_n(semid, SEM_READY, 1);

    // И ждёт общего старта */
    sem_wait_n(semid, SEM_START, 1);
}

static void car_process(int car_id, int msg_queue_id, int sem_id) {
    struct msg_buffer message;
    int position;
    int stage_time;

    // Инициализация случайных чисел для машины.
    srand((unsigned int)(time(NULL) + car_id));

    for (int stage = 1; stage <= NUM_STAGES; stage++) {
        /* Барьер старта этапа */
        wait_for_stage_start(sem_id);

        position = 0;
        stage_time = 0;

        while (position < TRACK_LENGTH) {
            int speed = (rand() % MAX_SPEED) + 1;
            position += speed;
            if (position > TRACK_LENGTH) {
                position = TRACK_LENGTH;
            }

            stage_time++;

            message.msg_type = 1;
            message.car_id = car_id;
            message.stage = stage;
            message.position = position;
            message.stage_time = stage_time;
            message.finished = (position >= TRACK_LENGTH);

            if (msgsnd(msg_queue_id, &message, sizeof(message) - sizeof(long), 0) == -1) {
                die("msgsnd");
            }

            if (!message.finished) {
                int random_delay = 180000 + (rand() % 220000);
                usleep(random_delay);
            }
        }
    }

    _exit(0);
}

int main(void) {
    int msg_queue_id;
    int sem_id;
    pid_t car_pids[NUM_CARS];
    Car cars[NUM_CARS];
    struct msg_buffer message;

    /* Очередь сообщений только для прогресса */
    msg_queue_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msg_queue_id == -1) {
        die("msgget");
    }

    /* 2 семафора: READY и START */
    sem_id = semget(IPC_PRIVATE, 2, IPC_CREAT | 0600);
    if (sem_id == -1) {
        msgctl(msg_queue_id, IPC_RMID, NULL);
        die("semget");
    }

    // union - тип данных, в котором все поля используют одну и ту же область памяти.
    // Это нужно, когда функция может принимать аргументы разных типов.
    // Для semctl() в разных режимах нужны разные данные, поэтому используется union semun.
    // Здесь используется поле val, потому что мы задаём числовое значение семафора.
    union semun arg;
    arg.val = 0;
    if (semctl(sem_id, SEM_READY, SETVAL, arg) == -1) die("semctl READY");
    if (semctl(sem_id, SEM_START, SETVAL, arg) == -1) die("semctl START");

    for (int i = 0; i < NUM_CARS; i++) {
        cars[i].id = i + 1;
        cars[i].position = 0;
        cars[i].total_time = 0;
        cars[i].points = 0;
        cars[i].rank = 0;
        for (int j = 0; j < NUM_STAGES; j++) {
            cars[i].stage_times[j] = 0;
        }
    }

    for (int i = 0; i < NUM_CARS; i++) {
        car_pids[i] = fork();

        if (car_pids[i] < 0) {
            die("fork");
        } else if (car_pids[i] == 0) {
            car_process(i + 1, msg_queue_id, sem_id);
        }
    }

    for (int stage = 1; stage <= NUM_STAGES; stage++) {
        clear_screen();

        // Арбитр ждёт, пока все машины дойдут до стартового барьера.
        // Каждая машина перед этим увеличивает SEM_READY на 1.
        sem_wait_n(sem_id, SEM_READY, NUM_CARS);

        // После того как все машины готовы, арбитр по нажатию Enter
        // открывает общий старт этапа.
        printf("Этап %d. Нажмите Enter для старта...", stage);
        fflush(stdout);
        getchar();

        // Сбрасываем отображаемые позиции машин перед новым этапом.
        for (int i = 0; i < NUM_CARS; i++) {
            cars[i].position = 0;
        }

        // Открываем старт сразу для всех машин.
        // Каждая машина ждёт SEM_START на -1,
        // а арбитр добавляет сразу NUM_CARS "пропусков".
        sem_post_n(sem_id, SEM_START, NUM_CARS);

        int finished_cars = 0;

        while (finished_cars < NUM_CARS) {
            if (msgrcv(msg_queue_id, &message, sizeof(message) - sizeof(long), 0, 0) == -1) {
                if (errno == EINTR) continue;
                die("msgrcv");
            }

            // Игнорируем сообщения не от текущего этапа.
            if (message.stage != stage) {
                continue;
            }

            int car_idx = message.car_id - 1;

            // Обновляем текущую позицию машины для отображения.
            cars[car_idx].position = message.position;

            // Если машина финишировала впервые на этом этапе,
            // засчитываем её результат.
            if (message.finished && cars[car_idx].stage_times[stage - 1] == 0) {
                finished_cars++;
                cars[car_idx].stage_times[stage - 1] = message.stage_time;
                cars[car_idx].total_time += message.stage_time;
            }

            // Обновляем экран с текущим состоянием гонки.
            display_race(cars, stage);
            usleep(50000);
        }

        // После завершения этапа начисляем очки.
        calculate_points(cars, stage);

        // Показываем финальное состояние этапа.
        display_race(cars, stage);

        printf("\nЭтап %d завершен! Нажмите Enter для продолжения...", stage);
        fflush(stdout);
        getchar();
    }

    /* Расчёт итоговых мест */
    clear_screen();
    printf("ФИНАЛЬНЫЕ РЕЗУЛЬТАТЫ\n");
    printf("===================\n\n");

    for (int i = 0; i < NUM_CARS; i++) {
        cars[i].rank = 1;
        for (int j = 0; j < NUM_CARS; j++) {
            if (cars[j].points > cars[i].points) {
                cars[i].rank++;
            } else if (cars[j].points == cars[i].points &&
                       cars[j].total_time < cars[i].total_time && i != j) {
                cars[i].rank++;
            }
        }
    }

    printf("+--------------+");
    for (int j = 1; j <= NUM_STAGES; j++) {
        printf("----------+");
    }
    printf("----------+----------+----------+\n");

    printf("| Автомобиль   |");
    for (int j = 1; j <= NUM_STAGES; j++) {
        printf(" Этап %d   |", j);
    }
    printf(" Общее    |  Очки   |  Место  |\n");

    printf("+--------------+");
    for (int j = 1; j <= NUM_STAGES; j++) {
        printf("----------+");
    }
    printf("----------+----------+----------+\n");

    for (int rank = 1; rank <= NUM_CARS; rank++) {
        for (int i = 0; i < NUM_CARS; i++) {
            if (cars[i].rank == rank) {
                printf("| Автомобиль %d  |", cars[i].id);

                for (int j = 0; j < NUM_STAGES; j++) {
                    printf(" %8d |", cars[i].stage_times[j]);
                }

                printf(" %8d |", cars[i].total_time);
                printf(" %8d |", cars[i].points);
                printf(" %8d |\n", cars[i].rank);
                break;
            }
        }
    }

    printf("+--------------+");
    for (int j = 1; j <= NUM_STAGES; j++) {
        printf("----------+");
    }
    printf("----------+----------+----------+\n");

    for (int i = 0; i < NUM_CARS; i++) {
        waitpid(car_pids[i], NULL, 0);
    }

    msgctl(msg_queue_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    return 0;
}

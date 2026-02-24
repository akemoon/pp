#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#define GAS_76 0
#define GAS_92 1
#define GAS_95 2
#define GAS_TYPES_COUNT 3

#define COLUMNS_76 2
#define COLUMNS_92 2
#define COLUMNS_95 1
#define TOTAL_COLUMNS (COLUMNS_76 + COLUMNS_92 + COLUMNS_95)
#define DEFAULT_QUEUE_SIZE 10

#define MAX_SEM_NAME_LEN 32
#define POLL_INTERVAL_US 100000
#define LOG_DIR "log"

typedef struct {
    int id;
    int gas_type;
    bool is_served;
} Client;

typedef struct {
    int capacity;
    int count;
    int total_clients;
    int successful_clients;
    bool running;

    char mutex_name[MAX_SEM_NAME_LEN];
    char empty_name[MAX_SEM_NAME_LEN];
    char full_name[MAX_SEM_NAME_LEN];

    sem_t *mutex;
    sem_t *empty;
    sem_t *full;

    Client clients[];
} Queue;

typedef struct {
    int id;
    int gas_type;
    FILE *log_file;
    double mean_service_time;
    double stddev_service_time;
} Column;

Queue *queue = NULL;
Column columns[TOTAL_COLUMNS];
FILE *queue_log = NULL;
FILE *rejected_log = NULL;
int next_client_id = 1;
int shmid = -1;
pid_t pids[TOTAL_COLUMNS + 1];

void client_generator(void);
void column_worker(Column *column);
double generate_random_normal(double mean, double stddev);
void initialize_queue(void);
void initialize_columns(void);
void cleanup(void);
void signal_handler(int sig);
const char *gas_type_to_string(int gas_type);
double clamp_min(double value, double min_value);
void sleep_seconds(double seconds);
FILE *open_log_file(const char *filename);
int queue_capacity(void);
void init_columns_for_type(
    int gas_type,
    int count,
    const double *mean_service_times,
    const double *stddev_service_times,
    const char *filename_prefix,
    int *column_index
);
bool should_worker_continue(void);

int main(void) {
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("Не удалось установить обработчик SIGINT");
        return EXIT_FAILURE;
    }

    read_config();
    srand((unsigned int)time(NULL));

    initialize_queue();
    initialize_columns();

    queue_log = open_log_file("queue.log");
    rejected_log = open_log_file("rejected.log");

    pid_t pid = fork();
    if (pid < 0) {
        perror("Не удалось создать процесс генератора клиентов");
        cleanup();
        return EXIT_FAILURE;
    }
    if (pid == 0) {
        client_generator();
        _exit(EXIT_SUCCESS);
    }
    pids[0] = pid;

    for (int i = 0; i < TOTAL_COLUMNS; ++i) {
        pid = fork();
        if (pid < 0) {
            perror("Не удалось создать процесс работника колонки");
            cleanup();
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            column_worker(&columns[i]);
            _exit(EXIT_SUCCESS);
        }
        pids[i + 1] = pid;
    }

    int status = 0;
    waitpid(pids[0], &status, 0);

    sem_wait(queue->mutex);
    queue->running = false;
    sem_post(queue->mutex);

    for (int i = 0; i < TOTAL_COLUMNS; ++i) {
        waitpid(pids[i + 1], &status, 0);
    }

    printf("Моделирование завершено. Всего обслужено клиентов: %d\n", queue->successful_clients);

    cleanup();
    return EXIT_SUCCESS;
}

const char *gas_type_to_string(int gas_type) {
    switch (gas_type) {
        case GAS_76:
            return "АИ-76";
        case GAS_92:
            return "АИ-92";
        case GAS_95:
            return "АИ-95";
        default:
            return "Неизвестный";
    }
}

double clamp_min(double value, double min_value) {
    return (value < min_value) ? min_value : value;
}

void sleep_seconds(double seconds) {
    const double safe_seconds = clamp_min(seconds, 0.0);
    usleep((useconds_t)(safe_seconds * 1000000.0));
}

int queue_capacity(void) {
    if (max_queue_size <= 0) {
        return DEFAULT_QUEUE_SIZE;
    }
    return max_queue_size;
}

FILE *open_log_file(const char *filename) {
    if (mkdir(LOG_DIR, 0777) == -1 && errno != EEXIST) {
        perror("Не удалось создать директорию для журналов");
        cleanup();
        exit(EXIT_FAILURE);
    }

    char path[128];
    const int written = snprintf(path, sizeof(path), "%s/%s", LOG_DIR, filename);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "Слишком длинный путь к файлу журнала: %s\n", filename);
        cleanup();
        exit(EXIT_FAILURE);
    }

    FILE *log_file = fopen(path, "w");
    if (log_file == NULL) {
        perror("Не удалось открыть файл журнала");
        cleanup();
        exit(EXIT_FAILURE);
    }
    return log_file;
}

void initialize_queue(void) {
    const int capacity = queue_capacity();
    const size_t queue_size = sizeof(Queue) + (size_t)capacity * sizeof(Client);

    shmid = shmget(IPC_PRIVATE, queue_size, IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("Не удалось создать разделяемую память");
        exit(EXIT_FAILURE);
    }

    queue = (Queue *)shmat(shmid, NULL, 0);
    if (queue == (Queue *)-1) {
        queue = NULL;
        perror("Не удалось присоединить разделяемую память");
        exit(EXIT_FAILURE);
    }

    queue->capacity = capacity;
    queue->count = 0;
    queue->total_clients = 0;
    queue->successful_clients = 0;
    queue->running = true;

    // Имена должны быть уникальными между запусками, иначе sem_open вернет конфликт.
    snprintf(queue->mutex_name, sizeof(queue->mutex_name), "/gas_mutex_%d", getpid());
    snprintf(queue->empty_name, sizeof(queue->empty_name), "/gas_empty_%d", getpid());
    snprintf(queue->full_name, sizeof(queue->full_name), "/gas_full_%d", getpid());

    sem_unlink(queue->mutex_name);
    sem_unlink(queue->empty_name);
    sem_unlink(queue->full_name);

    queue->mutex = sem_open(queue->mutex_name, O_CREAT | O_EXCL, 0666, 1);
    queue->empty = sem_open(queue->empty_name, O_CREAT | O_EXCL, 0666, queue->capacity);
    queue->full = sem_open(queue->full_name, O_CREAT | O_EXCL, 0666, 0);
    if (queue->mutex == SEM_FAILED || queue->empty == SEM_FAILED || queue->full == SEM_FAILED) {
        perror("Не удалось инициализировать семафоры");
        exit(EXIT_FAILURE);
    }
}

void init_columns_for_type(
    int gas_type,
    int count,
    const double *mean_service_times,
    const double *stddev_service_times,
    const char *filename_prefix,
    int *column_index
) {
    for (int i = 0; i < count; ++i) {
        Column *column = &columns[*column_index];
        char filename[64];

        column->id = *column_index;
        column->gas_type = gas_type;
        column->mean_service_time = mean_service_times[i];
        column->stddev_service_time = stddev_service_times[i];

        snprintf(filename, sizeof(filename), "%s_%d.log", filename_prefix, i + 1);
        column->log_file = open_log_file(filename);

        ++(*column_index);
    }
}

void initialize_columns(void) {
    int column_index = 0;
    const double mean_service_times_76[COLUMNS_76] = {
        mean_service_time_column_76_1,
        mean_service_time_column_76_2
    };
    const double stddev_service_times_76[COLUMNS_76] = {
        stddev_service_time_column_76_1,
        stddev_service_time_column_76_2
    };
    const double mean_service_times_92[COLUMNS_92] = {
        mean_service_time_column_92_1,
        mean_service_time_column_92_2
    };
    const double stddev_service_times_92[COLUMNS_92] = {
        stddev_service_time_column_92_1,
        stddev_service_time_column_92_2
    };
    const double mean_service_times_95[COLUMNS_95] = {
        mean_service_time_column_95_1
    };
    const double stddev_service_times_95[COLUMNS_95] = {
        stddev_service_time_column_95_1
    };

    init_columns_for_type(
        GAS_76,
        COLUMNS_76,
        mean_service_times_76,
        stddev_service_times_76,
        "column_76",
        &column_index
    );
    init_columns_for_type(
        GAS_92,
        COLUMNS_92,
        mean_service_times_92,
        stddev_service_times_92,
        "column_92",
        &column_index
    );
    init_columns_for_type(
        GAS_95,
        COLUMNS_95,
        mean_service_times_95,
        stddev_service_times_95,
        "column_95",
        &column_index
    );
}

void cleanup(void) {
    if (queue_log != NULL) {
        fclose(queue_log);
        queue_log = NULL;
    }

    if (rejected_log != NULL) {
        fclose(rejected_log);
        rejected_log = NULL;
    }

    for (int i = 0; i < TOTAL_COLUMNS; ++i) {
        if (columns[i].log_file != NULL) {
            fclose(columns[i].log_file);
            columns[i].log_file = NULL;
        }
    }

    if (queue != NULL) {
        if (queue->mutex != NULL && queue->mutex != SEM_FAILED) {
            sem_close(queue->mutex);
            sem_unlink(queue->mutex_name);
        }
        if (queue->empty != NULL && queue->empty != SEM_FAILED) {
            sem_close(queue->empty);
            sem_unlink(queue->empty_name);
        }
        if (queue->full != NULL && queue->full != SEM_FAILED) {
            sem_close(queue->full);
            sem_unlink(queue->full_name);
        }

        shmdt(queue);
        queue = NULL;
    }

    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
        shmid = -1;
    }
}

void signal_handler(int sig) {
    printf("\nПолучен сигнал %d. Завершение...\n", sig);

    if (queue != NULL) {
        sem_wait(queue->mutex);
        queue->running = false;
        sem_post(queue->mutex);
    }

    for (int i = 0; i < TOTAL_COLUMNS + 1; ++i) {
        if (pids[i] > 0) {
            kill(pids[i], SIGTERM);
        }
    }
}

double generate_random_normal(double mean, double stddev) {
    // Box-Muller: генерируем N(0, 1), затем масштабируем к N(mean, stddev).
    const double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 1.0);
    const double u2 = ((double)rand() + 1.0) / ((double)RAND_MAX + 1.0);
    const double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return mean + stddev * z0;
}

void client_generator(void) {
    while (true) {
        sem_wait(queue->mutex);

        if (!queue->running || queue->total_clients >= max_clients) {
            sem_post(queue->mutex);
            break;
        }

        ++queue->total_clients;
        const int client_id = next_client_id++;
        sem_post(queue->mutex);

        const int gas_type = rand() % GAS_TYPES_COUNT;
        const char *gas_name = gas_type_to_string(gas_type);

        if (sem_trywait(queue->empty) != 0) {
            fprintf(rejected_log, "Клиент %d отклонен, запрашивал %s, очередь полна\n", client_id, gas_name);
            fflush(rejected_log);
            printf("Клиент %d отклонен, запрашивал %s, очередь полна\n", client_id, gas_name);

            sleep_seconds(clamp_min(generate_random_normal(mean_arrival_time, stddev_arrival_time), 0.1));
            continue;
        }

        sem_wait(queue->mutex);
        if (queue->count < queue->capacity) {
            queue->clients[queue->count].id = client_id;
            queue->clients[queue->count].gas_type = gas_type;
            queue->clients[queue->count].is_served = false;
            ++queue->count;

            fprintf(queue_log, "Клиент %d прибыл, запрашивает %s\n", client_id, gas_name);
            fflush(queue_log);
            printf("Клиент %d прибыл, запрашивает %s. Размер очереди: %d\n", client_id, gas_name, queue->count);

            sem_post(queue->mutex);
            sem_post(queue->full);
        } else {
            sem_post(queue->mutex);
            sem_post(queue->empty);
        }

        sleep_seconds(clamp_min(generate_random_normal(mean_arrival_time, stddev_arrival_time), 0.1));
    }

    printf("Генератор клиентов завершил работу. Всего сгенерировано клиентов: %d\n", next_client_id - 1);
}

bool should_worker_continue(void) {
    bool result = false;
    sem_wait(queue->mutex);
    result = queue->running || queue->count > 0;
    sem_post(queue->mutex);
    return result;
}

void column_worker(Column *column) {
    const char *gas_name = gas_type_to_string(column->gas_type);
    printf("Колонка %d (%s) запущена\n", column->id, gas_name);

    while (should_worker_continue()) {
        if (sem_trywait(queue->full) != 0) {
            if (errno != EAGAIN && errno != EINTR) {
                perror("Ошибка ожидания занятых мест");
            }
            usleep(POLL_INTERVAL_US);
            continue;
        }

        sem_wait(queue->mutex);

        int client_index = -1;
        for (int i = 0; i < queue->count; ++i) {
            if (queue->clients[i].gas_type == column->gas_type && !queue->clients[i].is_served) {
                client_index = i;
                queue->clients[i].is_served = true;
                break;
            }
        }

        if (client_index == -1) {
            sem_post(queue->mutex);
            // Мы уже уменьшили full, но никого не взяли: возвращаем токен обратно.
            sem_post(queue->full);
            usleep(POLL_INTERVAL_US);
            continue;
        }

        const Client client = queue->clients[client_index];
        for (int i = client_index; i < queue->count - 1; ++i) {
            queue->clients[i] = queue->clients[i + 1];
        }
        --queue->count;

        sem_post(queue->mutex);
        sem_post(queue->empty);

        fprintf(column->log_file, "Клиент %d обслужен с %s\n", client.id, gas_name);
        fflush(column->log_file);
        printf("Колонка %d (%s) обслуживает клиента %d\n", column->id, gas_name, client.id);

        sleep_seconds(clamp_min(generate_random_normal(column->mean_service_time, column->stddev_service_time), 0.5));
        printf("Колонка %d (%s) завершила обслуживание клиента %d\n", column->id, gas_name, client.id);

        sem_wait(queue->mutex);
        ++queue->successful_clients;
        sem_post(queue->mutex);
    }

    printf("Колонка %d (%s) завершила работу\n", column->id, gas_name);
}

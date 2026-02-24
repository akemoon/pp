#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

// Глобальные переменные для параметров
int max_clients = 150;
int max_queue_size = 10;
double mean_arrival_time = 1.0;
double stddev_arrival_time = 0.5;
double mean_service_time_column_76_1 = 3.0;
double stddev_service_time_column_76_1 = 1.0;
double mean_service_time_column_76_2 = 3.0;
double stddev_service_time_column_76_2 = 1.0;
double mean_service_time_column_92_1 = 4.0;
double stddev_service_time_column_92_1 = 1.5;
double mean_service_time_column_92_2 = 4.0;
double stddev_service_time_column_92_2 = 1.5;
double mean_service_time_column_95_1 = 5.0;
double stddev_service_time_column_95_1 = 2.0;

static void load_column_settings(
    const char *json_content,
    const char *column_name,
    double *mean_target,
    double *stddev_target
) {
    char key_pattern[64];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", column_name);

    char *column_ptr = strstr(json_content, key_pattern);
    if (column_ptr == NULL) {
        return;
    }

    char *object_start = strchr(column_ptr, '{');
    if (object_start == NULL) {
        return;
    }

    char *object_end = strchr(object_start, '}');
    if (object_end == NULL) {
        return;
    }

    char *mean_ptr = strstr(object_start, "\"mean_service_time\"");
    if (mean_ptr != NULL && mean_ptr < object_end) {
        char *colon = strchr(mean_ptr, ':');
        if (colon != NULL && colon < object_end) {
            *mean_target = atof(colon + 1);
            printf("Загружено из конфигурации: %s.mean_service_time = %.2f\n", column_name, *mean_target);
        }
    }

    char *stddev_ptr = strstr(object_start, "\"stddev_service_time\"");
    if (stddev_ptr != NULL && stddev_ptr < object_end) {
        char *colon = strchr(stddev_ptr, ':');
        if (colon != NULL && colon < object_end) {
            *stddev_target = atof(colon + 1);
            printf("Загружено из конфигурации: %s.stddev_service_time = %.2f\n", column_name, *stddev_target);
        }
    }
}

// Функция для чтения JSON конфигурации
void read_config() {
    FILE *config_file = fopen("config.json", "r");
    if (config_file == NULL) {
        printf("Не удалось открыть файл конфигурации. Используются значения по умолчанию.\n");
        return;
    }
    
    // Определяем размер файла
    fseek(config_file, 0, SEEK_END);
    long file_size = ftell(config_file);
    rewind(config_file);
    
    // Выделяем память для содержимого файла
    char *json_content = (char *)malloc(file_size + 1);
    if (json_content == NULL) {
        perror("Не удалось выделить память для файла конфигурации");
        fclose(config_file);
        return;
    }
    
    // Читаем файл в буфер
    size_t read_size = fread(json_content, 1, file_size, config_file);
    fclose(config_file);
    
    if (read_size != file_size) {
        perror("Ошибка при чтении файла конфигурации");
        free(json_content);
        return;
    }
    
    json_content[file_size] = '\0';
    
    // Очень простой парсер JSON - ищем ключи и значения
    char *ptr = json_content;
    
    // Ищем max_clients
    ptr = strstr(json_content, "\"max_clients\"");
    if (ptr) {
        ptr = strchr(ptr, ':');
        if (ptr) {
            max_clients = atoi(ptr + 1);
            printf("Загружено из конфигурации: max_clients = %d\n", max_clients);
        }
    }

    ptr = strstr(json_content, "\"max_queue_size\"");
    if (ptr) {
        ptr = strchr(ptr, ':');
        if (ptr) {
            max_queue_size = atoi(ptr + 1);
            printf("Загружено из конфигурации: max_queue_size = %d\n", max_queue_size);
        }
    }
    
    // Ищем mean_arrival_time
    ptr = strstr(json_content, "\"mean_arrival_time\"");
    if (ptr) {
        ptr = strchr(ptr, ':');
        if (ptr) {
            mean_arrival_time = atof(ptr + 1);
            printf("Загружено из конфигурации: mean_arrival_time = %.2f\n", mean_arrival_time);
        }
    }
    
    // Ищем stddev_arrival_time
    ptr = strstr(json_content, "\"stddev_arrival_time\"");
    if (ptr) {
        ptr = strchr(ptr, ':');
        if (ptr) {
            stddev_arrival_time = atof(ptr + 1);
            printf("Загружено из конфигурации: stddev_arrival_time = %.2f\n", stddev_arrival_time);
        }
    }
    
    load_column_settings(
        json_content,
        "column_76_1",
        &mean_service_time_column_76_1,
        &stddev_service_time_column_76_1
    );
    load_column_settings(
        json_content,
        "column_76_2",
        &mean_service_time_column_76_2,
        &stddev_service_time_column_76_2
    );
    load_column_settings(
        json_content,
        "column_92_1",
        &mean_service_time_column_92_1,
        &stddev_service_time_column_92_1
    );
    load_column_settings(
        json_content,
        "column_92_2",
        &mean_service_time_column_92_2,
        &stddev_service_time_column_92_2
    );
    load_column_settings(
        json_content,
        "column_95_1",
        &mean_service_time_column_95_1,
        &stddev_service_time_column_95_1
    );
    
    free(json_content);
}

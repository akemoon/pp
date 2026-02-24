#ifndef CONFIG_H
#define CONFIG_H

// Функция для чтения JSON конфигурации
void read_config();

// Глобальные переменные для параметров
extern int max_clients;
extern int max_queue_size;
extern double mean_arrival_time;
extern double stddev_arrival_time;
extern double mean_service_time_column_76_1;
extern double stddev_service_time_column_76_1;
extern double mean_service_time_column_76_2;
extern double stddev_service_time_column_76_2;
extern double mean_service_time_column_92_1;
extern double stddev_service_time_column_92_1;
extern double mean_service_time_column_92_2;
extern double stddev_service_time_column_92_2;
extern double mean_service_time_column_95_1;
extern double stddev_service_time_column_95_1;

#endif // CONFIG_H

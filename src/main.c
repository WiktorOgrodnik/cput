#include "ring_buffer.h"

#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>   
#include <signal.h>                                                           

#define READER_BUFFER_SIZE 20
#define ANALYZER_BUFFER_SIZE 20

#define WATCHDOG_TIMEOUT 3
#define ROUND_TIME 1

bool to_kill_program = false;

/** Threads **/
pthread_t reader, analyzer, printer, watchdog;

/** /proc/stat file **/
FILE* procstat;

/** Transfering data to analyzer **/
ring_buffer* reader_buffer;

sem_t reader_buffer_empty;
sem_t reader_buffer_full;

pthread_mutex_t reader_buffer_mutex;

/** Transfering data to printer **/
ring_buffer* analyzer_buffer;

sem_t analyzer_buffer_empty;
sem_t analyzer_buffer_full;

pthread_mutex_t analyzer_buffer_mutex;

/* Debug attribute */

pthread_attr_t detachedThread;
pthread_mutex_t debug_mutex;

/* Debug file */
FILE* logfile;

/* Declarations */

/** Main threads functions **/

void* read_stats();
void* analyze_stats();
void* print_data();
void* watchdog_function();

/** Logger functions **/

void* print_debug(void* args);
void to_log(const char* function, const char* message);

void clean();

/** Functions bodies **/ 

void sigterm_handler() {
    printf("Received SIGTERM signal. Closing program safety...\n");
    exit(EXIT_SUCCESS);
}

void* read_stats() {
    
    // Producer
    struct timespec timeout;

    uint64_t idles[512];
    uint64_t non_idles[512];

    for (;;) {

        char* line = NULL;
        size_t length = 0;
        ssize_t read = 0;
        size_t size = 0;
        bool first = false;

        if ((procstat = fopen("/proc/stat", "r")) == NULL) exit(EXIT_FAILURE);
        to_log("read_stats", "Read raw data from /proc/stat");

        while((read = getline(&line, &length, procstat)) != -1) {

            if (strncmp(line, "cpu", 3) == 0) {

                if (!first) {
                    first = true;
                    continue;
                }

                char* lineorigin = line;
                uint64_t idle = 0, non_idle = 0;
                size_t it = 0;

                while(it++ < 11) {
                    char* space = strchr(line, ' ');
                    if (*line == ' ') {
                        line++;
                        continue;
                    }
                    else if (space != NULL)
                        *space = '\0';
                    
                    if (it == 5 || it == 6) {
                        idle += atol(line);
                    } else {
                        non_idle += atol(line);
                    }

                    if (space != NULL)
                        line = space + 1;
                }

                idles[size] = idle;
                non_idles[size++] = non_idle;

                line = lineorigin;
            }
        }

        cpu_raw_data_set* data_set = NULL;
        if ((data_set = (cpu_raw_data_set*)malloc(sizeof(cpu_raw_data_set) + size * sizeof(cpu_raw_data))) == NULL)
            exit(EXIT_FAILURE);

        data_set->size = size;

        for (size_t i = 0; i < size; i++) {
            data_set->proc[i].idle = idles[i];
            data_set->proc[i].non_idle = non_idles[i];
        }

        fclose(procstat);
        free(line);
        procstat = NULL;

        timespec_get(&timeout, TIME_UTC);
        timeout.tv_sec += WATCHDOG_TIMEOUT;
        if (sem_timedwait(&reader_buffer_empty, &timeout) < 0)
            to_kill_program = true;
        pthread_mutex_lock(&reader_buffer_mutex);

        ring_buffer_push(reader_buffer, (void*) data_set);

        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_full);

        sleep(ROUND_TIME);
    }

    return NULL;
}

void* analyze_stats() {

    struct timespec timeout;

    for (;;) {

        timespec_get(&timeout, TIME_UTC);
        timeout.tv_sec += WATCHDOG_TIMEOUT;

        if(sem_timedwait(&reader_buffer_full, &timeout) < 0)
            to_kill_program = true;
        pthread_mutex_lock(&reader_buffer_mutex);

        cpu_raw_data_set* data_set_last = (cpu_raw_data_set*)ring_buffer_pop(reader_buffer);
        cpu_raw_data_set* data_set_top = (cpu_raw_data_set*)ring_buffer_top(reader_buffer);

        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_empty);

        cpu_analyzed_data_set* analyzed_data_set = NULL;
        if ((analyzed_data_set = (cpu_analyzed_data_set*)malloc(sizeof(cpu_analyzed_data_set) + data_set_last->size * sizeof(float))) == NULL)
            exit(EXIT_FAILURE);

        analyzed_data_set->size = data_set_last->size;

        for (size_t i = 0; i < data_set_last->size; i++) {

            cpu_raw_data data_last = data_set_last->proc[i];
            cpu_raw_data data_top = data_set_top->proc[i];

            uint64_t prev_total = data_last.idle + data_last.non_idle;
            uint64_t total = data_top.idle + data_top.non_idle;

            uint64_t total_delta = total - prev_total;
            uint64_t idle_delta = data_top.idle - data_last.idle;

            float percentage = (float)(total_delta - idle_delta) * 100.0f / total_delta;

            analyzed_data_set->percentage[i] = percentage;
        }

        free(data_set_last);
        to_log("analyze_stats", "New data analyzed!");

        timespec_get(&timeout, TIME_UTC);
        timeout.tv_sec += WATCHDOG_TIMEOUT;
        if (sem_timedwait(&analyzer_buffer_empty, &timeout) < 0) 
            to_kill_program = true;
        pthread_mutex_lock(&analyzer_buffer_mutex);

        ring_buffer_push(analyzer_buffer, (void*)analyzed_data_set);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_full);

        sleep(ROUND_TIME);
    }

    return NULL;
}

void* print_data() {

    struct timespec timeout;

    for (;;) {

        timespec_get(&timeout, TIME_UTC);
        timeout.tv_sec += WATCHDOG_TIMEOUT;
        if (sem_timedwait(&analyzer_buffer_full, &timeout) < 0)
            to_kill_program = true;
        pthread_mutex_lock(&analyzer_buffer_mutex);

        cpu_analyzed_data_set* data_set = ring_buffer_pop(analyzer_buffer);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_empty);

        if (system("clear") < 0) exit(EXIT_FAILURE);

        for (size_t i = 0; i < data_set->size; i++) {
            printf("cpu%ld: %.1f%%\n", i, data_set->percentage[i]);
        }
        free(data_set);

        sleep(ROUND_TIME);
    }

    return NULL;
}

void* watchdog_function() {

    // select here
    for (;;) {

        sleep(WATCHDOG_TIMEOUT);
        to_log("watchdog", "watchdog is still running");

        if (to_kill_program) {
            printf("The program is stuck! Closing...\n");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}

void* print_debug(void* args) {

    log_message* log = (log_message*)args;

    pthread_mutex_lock(&debug_mutex);
    if ((logfile = fopen("debug.log", "a")) == NULL) {
        printf("Failed to open log file!\n");
    } else {
        fprintf(logfile, "From %s: %s\n", log->function, log->message);
        fclose(logfile);
        logfile = NULL;
    }
    
    pthread_mutex_unlock(&debug_mutex);
    free(log);

    return NULL;
}

void to_log(const char* function, const char* message) {
    pthread_t debug;
    log_message* log = NULL;

    if((log = (log_message*)malloc(sizeof(log_message))) == NULL) {
        clean();
        exit(EXIT_FAILURE);
    }

    sprintf(log->function, "%s", function);
    sprintf(log->message, "%s", message);

    int error = pthread_create(&debug, &detachedThread, &print_debug, log);
    if (error) {
        printf("Failed to log debug info\n");
    }
}  

int main() {

    signal(SIGTERM, sigterm_handler);

    int error = 0;

    // Initialize reader buffer
    sem_init(&reader_buffer_empty, 0, READER_BUFFER_SIZE);
    sem_init(&reader_buffer_full, 0, 0);

    pthread_mutex_init(&reader_buffer_mutex, NULL);

    reader_buffer = ring_buffer_new(READER_BUFFER_SIZE + 1);
    
    cpu_raw_data_set* val;
    if ((val = (cpu_raw_data_set*)malloc(sizeof(cpu_raw_data_set))) == NULL)
        return EXIT_FAILURE;

    val->size = 0;

    ring_buffer_push(reader_buffer, val);

    // Initialize analyzer buffer
    sem_init(&analyzer_buffer_empty, 0, ANALYZER_BUFFER_SIZE);
    sem_init(&analyzer_buffer_full, 0, 0);

    pthread_mutex_init(&analyzer_buffer_mutex, NULL);

    analyzer_buffer = ring_buffer_new(ANALYZER_BUFFER_SIZE);

    // Initialize detached attribute

    pthread_attr_init(&detachedThread);
    pthread_attr_setdetachstate(&detachedThread, PTHREAD_CREATE_DETACHED);

    // Initialize debug mutex

    pthread_mutex_init(&debug_mutex, NULL);

    error |= pthread_create(&reader, NULL, &read_stats, NULL);
    error |= pthread_create(&analyzer, NULL, &analyze_stats, NULL);
    error |= pthread_create(&printer, NULL, &print_data, NULL);
    error |= pthread_create(&watchdog, NULL, &watchdog_function, NULL);

    if (error) {
        printf("Problems with creating treads!\n");
        clean();
        return EXIT_FAILURE;
    }

    pthread_join(reader, NULL);
    pthread_join(analyzer, NULL);
    pthread_join(printer, NULL);
    pthread_join(watchdog, NULL);

    clean();

    return EXIT_SUCCESS;
}

void clean() {

    pthread_mutex_unlock(&reader_buffer_mutex);
    pthread_mutex_unlock(&analyzer_buffer_mutex);
    sem_post(&reader_buffer_full);
    sem_post(&reader_buffer_full);
    sem_post(&reader_buffer_empty);
    sem_post(&analyzer_buffer_empty);
    sem_post(&analyzer_buffer_full);

    pthread_join(reader, NULL);
    pthread_join(analyzer, NULL);
    pthread_join(printer, NULL);
    pthread_join(watchdog, NULL);
    
    ring_buffer_destroy(reader_buffer);
    ring_buffer_destroy(analyzer_buffer);

    sem_destroy(&reader_buffer_empty);
    sem_destroy(&reader_buffer_full);

    sem_destroy(&analyzer_buffer_empty);
    sem_destroy(&analyzer_buffer_full);

    if (logfile)
        fclose(logfile);
    if (procstat)
        fclose(procstat);

    pthread_mutex_destroy(&reader_buffer_mutex);
    pthread_mutex_destroy(&analyzer_buffer_mutex);
    pthread_mutex_destroy(&debug_mutex);

    pthread_attr_destroy(&detachedThread);
}

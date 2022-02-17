#include "ring_buffer.h"

#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>
#include <errno.h>   
#include <signal.h>                                                           

#define READER_BUFFER_SIZE 20
#define ANALYZER_BUFFER_SIZE 20
#define STATE_MAX 102

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

/* States for watchdog thread */

ulong reader_state;
ulong analyzer_state;
ulong printer_state;

/* Debug attribute */

pthread_attr_t detachedThread;
pthread_mutex_t debug_mutex;

/* Debug file */
FILE* logfile;

bool reader_running, analyzer_running, printer_running, watchdog_running;

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
    clean();
    exit(EXIT_SUCCESS);
}

void* read_stats() {
    
    // Producer

    while (reader_running) {
        reader_state = (reader_state + 1) % STATE_MAX;
        char* line = NULL;
        size_t length = 0;
        ssize_t read;

        if ((procstat = fopen("/proc/stat", "r")) == NULL) {
            clean();
            exit(EXIT_FAILURE);
        }

        to_log("read_stats", "Read raw data from /proc/stat");

        cpu_raw_data_set* data_set = (cpu_raw_data_set*)malloc(sizeof(cpu_raw_data_set));
        data_set->size = 0;
        bool first = false;

        while((read = getline(&line, &length, procstat)) != -1) {

            if (strncmp(line, "cpu", 3) == 0) {

                if (!first) {
                    first = true;
                    continue;
                }

                char* lineorigin = line;

                cpu_raw_data* cpu_entry = NULL;
                if((cpu_entry = (cpu_raw_data*)malloc(sizeof(cpu_raw_data))) == NULL) {
                    clean();
                    exit(EXIT_FAILURE);
                }

                cpu_entry->idle = 0;
                cpu_entry->non_idle = 0;

                int it = 0;
                while(it++ < 11) {
                    char* space = strchr(line, ' ');
                    if (*line == ' ') {
                        line++;
                        continue;
                    }
                    else if (space != NULL)
                        *space = '\0';
                    
                    if (it == 5 || it == 6) {
                        cpu_entry->idle += atol(line);
                    } else {
                        cpu_entry->non_idle += atol(line);
                    }

                    if (space != NULL)
                        line = space + 1;
                }

                data_set->proc[data_set->size++] = cpu_entry;

                line = lineorigin;
            }
        }

        fclose(procstat);
        free(line);
        procstat = NULL;

        sem_wait(&reader_buffer_empty);
        pthread_mutex_lock(&reader_buffer_mutex);

        ring_buffer_push(reader_buffer, (void*) data_set);

        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_full);

        reader_state = (reader_state + 1) % STATE_MAX;
        usleep(1000000);
    }

    return NULL;
}

void* analyze_stats() {

    while (analyzer_running) {
        analyzer_state = (analyzer_state + 1) % STATE_MAX;
        sem_wait(&reader_buffer_full);
        sem_wait(&reader_buffer_full);
        pthread_mutex_lock(&reader_buffer_mutex);

        if (!analyzer_running) {
            return NULL;
        }

        cpu_raw_data_set* data_set1 = (cpu_raw_data_set*) ring_buffer_pop(reader_buffer);
        cpu_raw_data_set* data_set2 = (cpu_raw_data_set*) ring_buffer_pop(reader_buffer);

        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_empty);
        sem_post(&reader_buffer_empty);

        if (data_set1 == NULL || data_set2 == NULL) {
            return NULL;
        }

        cpu_analyzed_data_set* analyzed_data_set = (cpu_analyzed_data_set*)malloc(sizeof(cpu_analyzed_data_set));
        analyzed_data_set->size = 0;

        for (size_t i = 0; i < data_set1->size; i++) {

            cpu_raw_data* data1 = data_set1->proc[i];
            cpu_raw_data* data2 = data_set2->proc[i];

            ulong prev_total = data1->idle + data1->non_idle;
            ulong total = data2->idle + data2->non_idle;

            ulong total_delta = total - prev_total;
            ulong idle_delta = data2->idle - data1->idle;

            float percentage = (float) (total_delta - idle_delta) * 100.0f / total_delta;

            analyzed_data_set->percentage[analyzed_data_set->size++] = percentage;

            free(data1);
            free(data2);
        }

        to_log("analyze_stats", "New data analyzed!");

        free(data_set1);
        free(data_set2);

        sem_wait(&analyzer_buffer_empty);
        pthread_mutex_lock(&analyzer_buffer_mutex);

        ring_buffer_push(analyzer_buffer, (void*) analyzed_data_set);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_full);

        analyzer_state = (analyzer_state + 1) % STATE_MAX;

        sleep(1);
    }

    return NULL;
}

void* print_data() {

    while (printer_running) {
        printer_state = (printer_state + 1) % STATE_MAX;
        sem_wait(&analyzer_buffer_full);
        pthread_mutex_lock(&analyzer_buffer_mutex);

        if (!printer_running) {
            return NULL;
        }

        cpu_analyzed_data_set* data_set = ring_buffer_pop(analyzer_buffer);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_empty);

        if (data_set == NULL) {
            return NULL;
        }

        system("clear");

        for (size_t i = 0; i < data_set->size; i++) {
            printf("cpu%ld: %.1f%%\n", i, data_set->percentage[i]);
        }
        free(data_set);

        printer_state = (printer_state + 1) % STATE_MAX;
        sleep(1);
    }

    return NULL;
}

void* watchdog_function() {

    while (watchdog_running) {
        ulong reader_state_old = reader_state;
        ulong analyzer_state_old = analyzer_state;
        ulong printer_state_old = printer_state;

        sleep(2);
        to_log("watchdog", "watchdog sill is running");

        if (reader_state == reader_state_old || analyzer_state == analyzer_state_old || printer_state == printer_state_old) {
            printf("The program is stuck! Closing...\n");
            if (reader_running) clean();
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}

void* print_debug(void* args) {

    log_message* log = (log_message*) args;

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

    // Initialize states
    reader_state = analyzer_state = printer_state = 0;

    // Initialize runnings

    reader_running = analyzer_running = printer_running = watchdog_running = true;

    // Initialize reader buffer
    sem_init(&reader_buffer_empty, 0, READER_BUFFER_SIZE);
    sem_init(&reader_buffer_full, 0, 0);

    pthread_mutex_init(&reader_buffer_mutex, NULL);

    reader_buffer = ring_buffer_new(READER_BUFFER_SIZE);

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

    reader_running = analyzer_running = printer_running = watchdog_running = false;

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

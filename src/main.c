#include "ring_buffer.h"

#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>

#define READER_BUFFER_SIZE 20
#define ANALYZER_BUFFER_SIZE 20

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

void* readstats() {
    
    // Producer
    FILE* procstat;

    while (true) {
        char* line = NULL;
        size_t length = 0;
        ssize_t read;

        if ((procstat = fopen("/proc/stat", "r")) == NULL) {
            exit(EXIT_FAILURE);
        }

        cpu_raw_data_set* data_set = (cpu_raw_data_set*)malloc(sizeof(cpu_raw_data_set));
        data_set->size = 0;

        while((read = getline(&line, &length, procstat)) != -1) {
            if (strncmp(line, "cpu", 3) == 0) {

                char* lineorigin = line;

                cpu_raw_data* cpu_entry = NULL;
                if((cpu_entry = (cpu_raw_data*)malloc(sizeof(cpu_raw_data))) == NULL) {
                    exit(EXIT_FAILURE);
                }

                int it = 0;
                while(it < 11) {
                    char* space = strchr(line, ' ');
                    if (*line == ' ') {
                        line++;
                        continue;
                    }
                    else if (space != NULL)
                        *space = '\0';
                    

                    if (it++) {
                        cpu_entry->data[it - 2] = atol(line);
                    } else {
                        strcpy(cpu_entry->cpu_name, line);
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

        sem_wait(&reader_buffer_empty);
        pthread_mutex_lock(&reader_buffer_mutex);
        ring_buffer_push(reader_buffer, data_set);
        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_full);

        usleep(1000000);
    }

    return NULL;
}

void* analyze_stats() {

    while(true) {

        sem_wait(&reader_buffer_full);
        sem_wait(&reader_buffer_full);
        pthread_mutex_lock(&reader_buffer_mutex);

        cpu_raw_data_set* data_set1 = (cpu_raw_data_set*) ring_buffer_pop(reader_buffer);
        cpu_raw_data_set* data_set2 = (cpu_raw_data_set*) ring_buffer_pop(reader_buffer);

        pthread_mutex_unlock(&reader_buffer_mutex);
        sem_post(&reader_buffer_empty);
        sem_post(&reader_buffer_empty);

        if (data_set1 == NULL || data_set2 == NULL) {
            printf("Buffer was empty, but it shouldn't happen");
            exit(EXIT_FAILURE);
        }

        cpu_analyzed_data_set* analyzed_data_set = (cpu_analyzed_data_set*)malloc(sizeof(cpu_analyzed_data_set));
        analyzed_data_set->size = 0;

        for (size_t i = 0; i < data_set1->size; i++) {
            cpu_analyzed_data* cpu_rows = (cpu_analyzed_data*)malloc(sizeof(cpu_analyzed_data));

            ulong* data1 = data_set1->proc[i]->data;
            ulong* data2 = data_set2->proc[i]->data;

            ulong prev_idle = data1[3] + data1[4];
            ulong idle = data2[3] + data2[4];

            ulong prev_none_idle = data1[0] + data1[1] + data1[2] + data1[5] + data1[6] + data1[7];
            ulong none_idle = data2[0] + data2[1] + data2[2] + data2[5] + data2[6] + data2[7];

            ulong prev_total = prev_idle + prev_none_idle;
            ulong total = idle + none_idle;

            ulong total_delta = total - prev_total;
            ulong idle_delta = idle - prev_idle;

            strcpy(cpu_rows->cpu_name, data_set1->proc[i]->cpu_name);
            cpu_rows->percentage = (float) (total_delta - idle_delta) * 100.0f / total_delta;

            analyzed_data_set->proc[analyzed_data_set->size++] = cpu_rows;
        }

        for (size_t i = 0; i < data_set1->size; i++) {

            free(data_set1->proc[i]);
            free(data_set2->proc[i]);
        }

        free(data_set1);
        free(data_set2);

        sem_wait(&analyzer_buffer_empty);
        pthread_mutex_lock(&analyzer_buffer_mutex);

        ring_buffer_push(analyzer_buffer, (void*) analyzed_data_set);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_full);

        /*for (size_t i = 0; i < analyzed_data_set->size; i++) {
            printf("Proc: %s, usage: %.2f%%\n", analyzed_data_set->proc[i]->cpu_name, analyzed_data_set->proc[i]->percentage * 100.0f);
            free(analyzed_data_set->proc[i]);
        }*/

        //free(analyzed_data_set);

        sleep(1);
    }

    return NULL;
}

void* print_data() {

    while (true) {

        sem_wait(&analyzer_buffer_full);
        pthread_mutex_lock(&analyzer_buffer_mutex);

        cpu_analyzed_data_set* data_set = ring_buffer_pop(analyzer_buffer);

        pthread_mutex_unlock(&analyzer_buffer_mutex);
        sem_post(&analyzer_buffer_empty);

        system("clear");

        if (data_set == NULL) {
            printf("Buffer was empty, but it shouldn't happen");
            exit(EXIT_FAILURE);
        }

        for (size_t i = 1; i < data_set->size; i++) {
            printf("%s: %.1f%%\n", data_set->proc[i]->cpu_name, data_set->proc[i]->percentage);
            free(data_set->proc[i]);
        }
        free(data_set);

        sleep(1);
    }

    return NULL;
}

int main() {

    int error = 0;

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

    pthread_t reader, analyzer, printer;
    error |= pthread_create(&reader, NULL, &readstats, NULL);
    error |= pthread_create(&analyzer, NULL, &analyze_stats, NULL);
    error |= pthread_create(&printer, NULL, &print_data, NULL);

    if (error) {
        printf("Problems with creating treads!\n");
        return EXIT_FAILURE;
    }

    pthread_join(reader, NULL);
    pthread_join(analyzer, NULL);

    ring_buffer_destroy(reader_buffer);
    ring_buffer_destroy(analyzer_buffer);

    sem_destroy(&reader_buffer_empty);
    sem_destroy(&reader_buffer_full);

    sem_destroy(&analyzer_buffer_empty);
    sem_destroy(&analyzer_buffer_full);

    pthread_mutex_destroy(&reader_buffer_mutex);
    pthread_mutex_destroy(&analyzer_buffer_mutex);

    return EXIT_SUCCESS;
}
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>

#define ulong unsigned long

#define READER_BUFFER_SIZE 20

typedef struct cpu_raw_data {
    char cpu_name [6];
    ulong data [10];
} cpu_raw_data;

typedef struct cpu_raw_data_set {
    size_t size;
    cpu_raw_data* proc [100];
} cpu_raw_data_set;

cpu_raw_data_set* reader_buffer[READER_BUFFER_SIZE];
size_t reader_buffer_size;

sem_t reader_buffer_empty;
sem_t reader_buffer_full;

void* readstats() {
    
    // Producer

    /* TO_DO 
        Add to queue
        If queue is full not commit any new rows
    */
    FILE* procstat;

    int debug_counter = 0;
    
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
                        *space = '\0'; // I will check later for memory leaks
                    

                    if (it++) {
                        cpu_entry->data[it - 2] = atol(line);
                    } else {
                        strcpy(cpu_entry->cpu_name, line);
                    }

                    if (space != NULL)
                        line = space + 1;
                }

                // printf("Retrieved data: %s ", cpu_entry->cpu_name);
                // for (int i = 0; i < 10; i++) {
                //     printf("%lu ", cpu_entry->data[i]);
                // }
                // printf("\n");

                data_set->proc[data_set->size++] = cpu_entry;

                line = lineorigin;
            }
        }

        fclose(procstat);
        free(line);

        sem_wait(&reader_buffer_empty);
        reader_buffer[reader_buffer_size++] = data_set;
        sem_post(&reader_buffer_full);

        printf("DEBUG: %d\n", debug_counter++);

    }

    return NULL;
}

int main() {

    int error = 0;

    sem_init(&reader_buffer_empty, 0, READER_BUFFER_SIZE);
    sem_init(&reader_buffer_full, 0, 0);

    reader_buffer_size = 0;

    pthread_t reader;
    error |= pthread_create(&reader, NULL, &readstats, NULL);

    if (error) {
        printf("Problems with creating treads!\n");
        return EXIT_FAILURE;
    }

    pthread_join(reader, NULL);

    sem_destroy(&reader_buffer_empty);
    sem_destroy(&reader_buffer_full);

    return EXIT_SUCCESS;
}
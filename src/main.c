#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#define ulong unsigned long

typedef struct cpu_raw_data {
    char* cpu_name;
    ulong data [10];
} cpu_raw_data;


void* readstats() {
    
    // Producer

    /* TO_DO 
        Add to queue
        If queue is full not commit any new rows
    */
    FILE* procstat;
    

    while (true) {
        char* line = NULL;
        size_t length = 0;
        ssize_t read;

        if ((procstat = fopen("/proc/stat", "r")) == NULL) {
            exit(EXIT_FAILURE);
        }

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
                    if (space != NULL)
                        *space = '\0';

                    if (it++) {
                        cpu_entry->data[it - 2] = atoi(line);
                    } else {
                        cpu_entry->cpu_name = (char*)malloc(sizeof(char)*6);
                        cpu_entry->cpu_name = strcpy(cpu_entry->cpu_name, line);
                    }

                    if (space != NULL)
                        line = space + 1;
                }

                printf("Retrieved data: %s ", cpu_entry->cpu_name);
                for (int i = 0; i < 10; i++) {
                    printf("%lu ", cpu_entry->data[i]);
                }
                printf("\n");

                free(cpu_entry->cpu_name);
                free(cpu_entry);

                line = lineorigin;
            }
        }
        free(line);
        fclose(procstat);
    }

    return NULL;
}

int main() {

    int error = 0;

    pthread_t reader;
    error |= pthread_create(&reader, NULL, &readstats, NULL);

    if (error) {
        printf("Problems with creating treads!\n");
        return EXIT_FAILURE;
    }

    pthread_join(reader, NULL);

    return EXIT_SUCCESS;
}
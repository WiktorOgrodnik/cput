#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct cpu_raw_data {
    uint64_t idle;
    uint64_t non_idle;
} cpu_raw_data;

typedef struct cpu_raw_data_set {
    size_t size;
    cpu_raw_data proc[];
} cpu_raw_data_set;

typedef struct cpu_analyzed_data_set {
    size_t size;
    float percentage[];
} cpu_analyzed_data_set;

typedef struct log_message {
    char function[30];
    char message[255];
} log_message;

#endif

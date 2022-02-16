#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#define ulong unsigned long

typedef struct cpu_raw_data {
    char cpu_name [6];
    ulong data [10];
} cpu_raw_data;

typedef struct cpu_raw_data_set {
    size_t size;
    cpu_raw_data* proc [100];
} cpu_raw_data_set;

typedef struct cpu_analyzed_data {
    char cpu_name[6];
    float percentage;
} cpu_analyzed_data;

typedef struct cpu_analyzed_data_set {
    size_t size;
    cpu_analyzed_data* proc [100];
} cpu_analyzed_data_set;

#endif
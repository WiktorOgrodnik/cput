#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "types.h"

#define T cpu_raw_data_set*
#define NO_VALUE NULL

typedef struct ring_buffer {
    T* values;
    size_t head, tail, length, size;
} ring_buffer;

ring_buffer* ring_buffer_new(size_t size);
bool ring_buffer_empty(ring_buffer* r);
bool ring_buffer_full(ring_buffer* r);
void ring_buffer_destroy(ring_buffer* r);
void ring_buffer_push(ring_buffer* r, T val);
T ring_buffer_pop(ring_buffer* r);

#endif
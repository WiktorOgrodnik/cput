#include "ring_buffer.h"

ring_buffer* ring_buffer_new(size_t size) {
    ring_buffer* buffer = NULL;
    if ((buffer = (ring_buffer*)malloc(sizeof(ring_buffer))) == NULL)
        exit(EXIT_FAILURE);
    
    buffer->values = (T*)malloc(sizeof(void*)*size);
    buffer->head = 0;
    buffer->tail = 0;
    buffer->length = 0;
    buffer->size = size;

    return buffer;
}

bool ring_buffer_empty(ring_buffer* r) {
    return r->length == 0;
}

bool ring_buffer_full(ring_buffer* r) {
    return r->length == r->size;
}

void ring_buffer_destroy(ring_buffer* r) {
    free(r->values);
    free(r);
}

void ring_buffer_push(ring_buffer* r, T val) {
    if (ring_buffer_full(r))
        return;

    r->values[r->tail] = val;
    r->length++;
    r->tail = (r->tail + 1) % r->size;
}

T ring_buffer_pop(ring_buffer* r) {
    if (ring_buffer_empty(r))
        return NO_VALUE;

    T val = r->values[r->head];
    r->length--;
    r->head = (r->head + 1) % r->size;

    return val;
}


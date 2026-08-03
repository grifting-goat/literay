#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// must exceed CHUNK_STREAM_COUNT (world.c: WINDOW_XZ * WINDOW_XZ * WINDOW_Y, currently
// 64*64*16 = 65536) or a full streaming-window rebuild silently drops chunk loads once the
// queue fills (circular buffer usable capacity is size-1) -- keep comfortable headroom above
// that if the window size ever changes again.
#define MAX_QUEUE_SIZE 131072

typedef struct {
    void* buffer;
    uint32_t size;
    uint32_t elem_size;
    int32_t head;
    int32_t tail;
} Queue_t;

static Queue_t q_create(uint32_t size, uint32_t element_size) {
    Queue_t q = {0};
    if (size > MAX_QUEUE_SIZE) {size = MAX_QUEUE_SIZE;}
    q.size = size;
    q.elem_size = element_size;
    q.head = -1;
    q.tail = -1;

    q.buffer = calloc(size, element_size);

    return q;
}

static void q_unalloc(Queue_t* q) {
    q->head = -1;
    q->tail = -1;
    q->size = 0;
    free(q->buffer);
    q->buffer = NULL;
}

static void q_clear(Queue_t* q) {
    q->head = -1;
    q->tail = -1;
}

static bool q_full(Queue_t* q) {
    return (q->tail + 1) % (int32_t)q->size == q->head;
}

static bool q_emtpy(Queue_t* q) {
    return q->head == -1;
}

static void q_enque(Queue_t* q, const void* element) {

    if (q_full(q)) {printf("queue failed\n"); return;}

    if (q->head == -1) {q->head = 0;}

    q->tail = (q->tail + 1) % (int32_t)q->size;
    memcpy((uint8_t*)q->buffer + (size_t)q->tail * q->elem_size, element, q->elem_size);
}

static void q_pop(Queue_t* q, void* out) {
    if (q_emtpy(q)) {memset(out, 0, q->elem_size); return;}
    memcpy(out, (uint8_t*)q->buffer + (size_t)q->head * q->elem_size, q->elem_size);

    if (q->head == q->tail) {
        q->head = q->tail = -1;
    }
    else {
        q->head = (q->head + 1) % (int32_t)q->size;
    }
}

static void q_peek(Queue_t* q, void* out) {
    if (q_emtpy(q)) {return;}
    memcpy(out, (uint8_t*)q->buffer + (size_t)q->head * q->elem_size, q->elem_size);
}

#endif //QUEUE_H

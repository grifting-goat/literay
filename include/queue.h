#ifndef QUEUE_H
#define QUEUE_H

#include "vector.h"
#include <limits.h>

#define MAX_QUEUE_SIZE 32768

typedef struct {
    iVector_t* buffer;
    uint32_t size;
    int32_t head;
    int32_t tail;
} Queue_t;

static Queue_t q_create(uint32_t size) {
    Queue_t q = {0};
    if (size > MAX_QUEUE_SIZE) {size = MAX_QUEUE_SIZE;}
    q.size = size;
    q.head = -1;
    q.tail = -1;

    q.buffer = calloc(size, sizeof(iVector_t));

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
    return (q->tail + 1) % q->size == q->head;
}

static bool q_emtpy(Queue_t* q) {
    return q->head == -1;
}

static void q_enque(Queue_t* q, iVector_t* vec) {

    if (q_full(q)) {/*printf("queue failed\n");*/ return;}

    if (q->head == -1) {q->head = 0;}

    q->tail = (q->tail + 1) % q->size;
    q->buffer[q->tail] = *vec;
}

static iVector_t q_pop(Queue_t* q) {
    if(q_emtpy(q)) {return iVector_create(INT32_MIN,INT32_MIN,INT32_MIN);}
    iVector_t data = q->buffer[q->head];

    if (q->head == q->tail) {
        q->head = q->tail = -1;
    } 
    else {
        q->head = (q->head + 1) % q->size;
    }
    return data;
}

static iVector_t q_peek(Queue_t* q) {
    if(q_emtpy(q)) {return iVector_create(INT32_MIN,INT32_MIN,INT32_MIN);}
    return q->buffer[q->head];

}





#endif //QUEUE_H
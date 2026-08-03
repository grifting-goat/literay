#ifndef CHUNK_THREADS_H
#define CHUNK_THREADS_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#undef IN
#undef OUT
#undef OPTIONAL

#include "vector.h"
#include "queue.h"

#define CHUNK_GEN_MAX_THREADS 8
// same headroom rationale as MAX_QUEUE_SIZE in queue.h -- must stay able to hold a full
// streaming-window's worth of pending results
#define CHUNK_GEN_RESULT_QUEUE_CAP 131072

typedef struct {
    iVector_t coord;
    uint64_t brickMask;
    uint64_t brickDirtyMask;
    bool loaded;

    uint8_t* voxels;
    uint32_t* voxelMask;
} Chunk;

void chunk_free(Chunk* chunk);

typedef Chunk (*ChunkGenerateFn)(void* userData, iVector_t coord);

typedef struct {
    ChunkGenerateFn genFn;
    void* genUserData;

    HANDLE threads[CHUNK_GEN_MAX_THREADS];
    uint32_t threadCount;
    volatile LONG shutdown;

    CRITICAL_SECTION jobLock;
    CONDITION_VARIABLE jobCond;
    Queue_t jobQueue;

    CRITICAL_SECTION resultLock;
    CONDITION_VARIABLE resultCond;
    Queue_t resultQueue;
} ChunkThreadPool_t;

void chunk_thread_pool_start(ChunkThreadPool_t* pool, ChunkGenerateFn genFn, void* userData);
void chunk_thread_pool_stop(ChunkThreadPool_t* pool);

void chunk_thread_pool_submit(ChunkThreadPool_t* pool, iVector_t coord);

void chunk_thread_pool_lock_jobs(ChunkThreadPool_t* pool);
void chunk_thread_pool_enqueue_locked(ChunkThreadPool_t* pool, iVector_t coord);
void chunk_thread_pool_unlock_jobs_and_wake(ChunkThreadPool_t* pool);

bool chunk_thread_pool_poll(ChunkThreadPool_t* pool, Chunk* outChunk);
void chunk_thread_pool_poll_blocking(ChunkThreadPool_t* pool, Chunk* outChunk);

#endif //CHUNK_THREADS_H

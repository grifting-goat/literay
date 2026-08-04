#include "chunk_threads.h"

#include <stdlib.h>

static DWORD WINAPI chunk_thread_pool_worker_main(LPVOID param) {
    ChunkThreadPool_t* pool = (ChunkThreadPool_t*)param;

    for (;;) {
        EnterCriticalSection(&pool->jobLock);
        while (q_emtpy(&pool->jobQueue) && !pool->shutdown) {
            SleepConditionVariableCS(&pool->jobCond, &pool->jobLock, INFINITE);
        }
        if (pool->shutdown && q_emtpy(&pool->jobQueue)) {
            LeaveCriticalSection(&pool->jobLock);
            break;
        }
        iVector_t coord;
        q_pop(&pool->jobQueue, &coord);
        LeaveCriticalSection(&pool->jobLock);

        Chunk chunk = pool->genFn(pool->genUserData, coord);

        EnterCriticalSection(&pool->resultLock);
        if (q_full(&pool->resultQueue)) { chunk_free(&chunk); }
        else { q_enque(&pool->resultQueue, &chunk); }
        LeaveCriticalSection(&pool->resultLock);
        WakeConditionVariable(&pool->resultCond);
    }
    return 0;
}

void chunk_thread_pool_start(ChunkThreadPool_t* pool, ChunkGenerateFn genFn, void* userData) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    uint32_t count = sysInfo.dwNumberOfProcessors > 1 ? sysInfo.dwNumberOfProcessors - 1 : 1;
    if (count > CHUNK_GEN_MAX_THREADS) { count = CHUNK_GEN_MAX_THREADS; }

    pool->genFn = genFn;
    pool->genUserData = userData;

    InitializeCriticalSection(&pool->jobLock);
    InitializeConditionVariable(&pool->jobCond);
    pool->jobQueue = q_create(MAX_QUEUE_SIZE, sizeof(iVector_t));

    InitializeCriticalSection(&pool->resultLock);
    InitializeConditionVariable(&pool->resultCond);
    pool->resultQueue = q_create(CHUNK_GEN_RESULT_QUEUE_CAP, sizeof(Chunk));

    pool->shutdown = 0;
    pool->threadCount = count;
    for (uint32_t i = 0; i < count; i++) {
        pool->threads[i] = CreateThread(NULL, 0, chunk_thread_pool_worker_main, pool, 0, NULL);
    }
}

void chunk_thread_pool_stop(ChunkThreadPool_t* pool) {
    EnterCriticalSection(&pool->jobLock);
    pool->shutdown = 1;
    LeaveCriticalSection(&pool->jobLock);
    WakeAllConditionVariable(&pool->jobCond);

    WaitForMultipleObjects(pool->threadCount, pool->threads, TRUE, INFINITE);
    for (uint32_t i = 0; i < pool->threadCount; i++) {
        CloseHandle(pool->threads[i]);
    }
    DeleteCriticalSection(&pool->jobLock);
    q_unalloc(&pool->jobQueue);

    while (!q_emtpy(&pool->resultQueue)) {
        Chunk chunk;
        q_pop(&pool->resultQueue, &chunk);
        chunk_free(&chunk);
    }
    DeleteCriticalSection(&pool->resultLock);
    q_unalloc(&pool->resultQueue);
}

void chunk_thread_pool_submit(ChunkThreadPool_t* pool, iVector_t coord) {
    EnterCriticalSection(&pool->jobLock);
    q_enque(&pool->jobQueue, &coord);
    LeaveCriticalSection(&pool->jobLock);
    WakeConditionVariable(&pool->jobCond);
}

void chunk_thread_pool_lock_jobs(ChunkThreadPool_t* pool) {
    EnterCriticalSection(&pool->jobLock);
}

void chunk_thread_pool_enqueue_locked(ChunkThreadPool_t* pool, iVector_t coord) {
    q_enque(&pool->jobQueue, &coord);
}

void chunk_thread_pool_unlock_jobs_and_wake(ChunkThreadPool_t* pool) {
    LeaveCriticalSection(&pool->jobLock);
    WakeAllConditionVariable(&pool->jobCond);
}

bool chunk_thread_pool_poll(ChunkThreadPool_t* pool, Chunk* outChunk) {
    bool got = false;
    EnterCriticalSection(&pool->resultLock);
    if (!q_emtpy(&pool->resultQueue)) {
        q_pop(&pool->resultQueue, outChunk);
        got = true;
    }
    LeaveCriticalSection(&pool->resultLock);
    return got;
}

void chunk_thread_pool_poll_blocking(ChunkThreadPool_t* pool, Chunk* outChunk) {
    EnterCriticalSection(&pool->resultLock);
    while (q_emtpy(&pool->resultQueue)) {SleepConditionVariableCS(&pool->resultCond, &pool->resultLock, INFINITE);}
    q_pop(&pool->resultQueue, outChunk);
    LeaveCriticalSection(&pool->resultLock);
}

void chunk_free(Chunk* chunk) {
    free(chunk->voxelMask);
    chunk->voxelMask = NULL;

    free(chunk->voxels);
    chunk->voxels = NULL;

    chunk->loaded = false;
}

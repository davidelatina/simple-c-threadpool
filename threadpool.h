/**
 * @file threadpool.h
 * @brief Declarations for the clients of a thread pool.
 */
#pragma once

#include <threads.h>

/**
 * @brief Threadpool type, opaque to the client.
 * It is created by threadpool_create() and must be passed
 * unmodified to the remainder of the interfaces.
 */
typedef struct threadpool threadpool;

/**
 * @brief Create a thread pool.
 *
 * @param num_threads Number of threads kept in the pool, available to
 *    perform work requests. Must be greater than zero.
 * @return threadpool*
 *
 * On error, returns NULL.
 */
extern threadpool* threadpool_create(size_t const num_threads);

/**
 * @brief Add a work request to the thread pool task queue.
 *
 * If there are idle worker threads, awaken one to perform the task;
 * else just return after adding the task to the queue.
 * An existing worker thread will perform the task after
 * it finishes its current task.
 * NOTE: Do not enqueue a function which detaches the thread.
 * Threads will join back just before deletion to avoid leaks.
 * @param pool
 * @param (*func)(void*)
 * @param arg
 * @return int
 *
 * On error, @threadpool_queue() returns EXIT_FAILURE.
 */
extern int threadpool_queue(threadpool* pool, void (*func)(void*), void* arg);

/**
 * @brief Wait for all queued tasks to complete.
 *
 * @param pool Pointer to the thread pool to wait for.
 */
extern void threadpool_wait(threadpool* pool);

// TODO: implement a timed wait

// Cancel all queued tasks and destroy the pool.
/**
 * @brief Destroy a thread pool created by @threadpool_create
 *
 * It is necessary to call @threadpool_queue() first
 * if all queued tasks must be completed before deletion and/or program
 * exit.
 *
 * The function will attempt to cancel any queued tasks it can,
 * but cannot be relied upon to immediately prevent performing
 * any more of them.
 *
 * @param pool Pointer to the thread pool to be deleted.
 */
extern void threadpool_destroy(threadpool* pool);

/**
 * @brief Read number of active threads.
 *
 * Threads are counted as
 * - active: since initialization and when retrieving and performing work
 * - idle: when waiting for more work
 * - neither: shortly before initialization and during deletion
 *
 * @param pool Pointer to thread pool
 * @return size_t
 */
extern size_t threadpool_active_num(threadpool* pool);


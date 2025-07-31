/** @file
 * @brief Very basic and limited thread pool implementation.
 *
 * Interface declarations are in @<threadpool.h>
 */

#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

/** Macro enabled by compiler flag, for added messages to stderr.
 *  With gcc, add "-D DEBUG_MSG"
 */


#ifdef DEBUG_MSG_ON
  #define DEBUG_MSG(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define DEBUG_MSG(...) do {} while(0);
#endif

/** Macro for printing detailed error location. */
#ifndef PRINT_ERROR_LOCATION
  #define PRINT_ERROR_LOCATION \
    do { \
      fprintf(stderr, "%s -> %s -> line %i\n" ,\
    __FILE_NAME__, __func__, __LINE__); \
    } while (0);
#endif

typedef struct task_queue task_queue;
/**
 * @brief Member struct of a linked list of tasks for the thread pool.
 *
 * @param task_next* Pointer to the next task_queue struct.
 * @param (*task_func)(void*) function to call.
 * @param task_arg void pointer to function argument.
 */
struct task_queue {
  task_queue* task_next;
  void        (*task_func)(void*);   /**  */
  void*       task_arg;              /** its argument */
};

/**
 * @brief Thread pool structure, opaque to the clients.
 *
 * Initialized by @threadpool_create(). , freed by
 * @threadpool_destroy.
 *
 * These variables do not require mutex lock:
 * - Counters incremented and decremented by each worker
 *   thread by itself, only when they hold the lock; they're only
 *   atomic so that their value can be read at any time.
 * @param pool_threads_active Number of active threads.
 * @param pool_threads_idle Number of idle threads.
 * @see @threadpool_active_num
 * @param pool_threads_num Total number of threads. Set once by
 *  threadpool_create, stays constant.
 * @param pool_to_be_deleted Flag: initialized as false by
 *  @threadpool_create() and set to true by @threadpool_delete() without
 *  waiting for mutex lock.
 *  Each @worker_thread() will cease executing tasks and exit as soon as
 *  its current task is over, or when @threadpool_delete wakes it
 *  from its idle state.
 *
 * ---
 *
 * @param pool_lock Protects thread pool data.
 *
 * Variables which require mutex lock:
 *
 * @param pool_work_finished Used by the last active @worker_thread()
 * to signal @threadpool_wait that work is done.
 * @param pool_new_work_available Used by @threadpool_queue to wake a
 *  @worker_thread() if at least one is idle, and by @threadpool_delete()
 *  to wake idle threads for deletion.
 * @param pool_threads_deleted Used by the last @worker_thread()
 *  being deleted to signal @threadpool_delete it can finish deleting
 *  the thread pool, as all threads have been shut down.
 *
 * @param task_head Head of task queue.
 * @param task_tail Tail of task queue.
 *
 * @param thread_id[] Array of thread IDs, initialized to
 * @pool_threads_num size.
 * Flexible struct member: must be the last element.
 */
struct threadpool {
  // Variables which do not require mutex
  //
  _Atomic(size_t)   pool_threads_active;
  _Atomic(size_t)   pool_threads_idle;
  // - threads_num is
  _Atomic(size_t)   pool_threads_num;    // Total number of threads.
  _Atomic(bool)     pool_to_be_deleted;  // Marked for deletion.

  // ---
  mtx_t pool_lock;
  // Variables which require mutex lock
  cnd_t pool_work_finished;
  cnd_t pool_new_work_available;
  cnd_t pool_threads_deleted;

  task_queue *task_head;
  task_queue *task_tail;

  // array of thread IDs. initialized to pool_threads_num size
  // (flexible array member of this struct must be its last element)
  thrd_t thread_id[];
};

/**
 * @brief Main function every thread is called with.
 *
 * Joins back into the main thread after calling @threadpool_delete().
 *
 * @param arg void pointer to threadpool struct.
 *
 * @return int
 *
 * Return value currently unused.
 */
int worker_thread(void* arg) {
  threadpool* pool = arg;
    for (;;) {
    // Acquire lock
    int err = mtx_lock(&pool->pool_lock);
    if (err) {
      fprintf(stderr, "worker: error acquiring lock, code %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }

    // Check if the thread pool is being deleted
    if (pool->pool_to_be_deleted) {
      DEBUG_MSG("worker: tbd\n");
      break;
    }
    // Check if there is work available
    if (!pool->task_head) { // No tasks in queue
      // Mark as idle
      DEBUG_MSG("worker: idle\n");
      pool->pool_threads_active--;
      pool->pool_threads_idle++;

      // Check if this is the last thread finishing work
      if (pool->pool_threads_active == 0) {
        assert(pool->pool_threads_idle == pool->pool_threads_num);
        // Send work_finished signal to threadpool_wait function
        // (does nothing if threadpool_wait isn't waiting)
        err = cnd_signal(&pool->pool_work_finished);
        if (err) {
          fprintf(stderr, "condition signal %d", err);
          PRINT_ERROR_LOCATION;
          exit(EXIT_FAILURE);
        }
      }
      // Wait for more tasks; meanwhile cede lock
      while ((!pool->task_head) && (!pool->pool_to_be_deleted)) {
        DEBUG_MSG("worker: wait for new tasks\n");
        int err = cnd_wait(&pool->pool_new_work_available, &pool->pool_lock);
        if (err) {
          fprintf(stderr, "error waiting for new work, code %d", err);
          exit(EXIT_FAILURE);
        }
      }
      DEBUG_MSG("worker: active\n");
      pool->pool_threads_active++;
      pool->pool_threads_idle--;
      // Check if it has only been woken up to delete the thread pool
      if (pool->pool_to_be_deleted) {
        DEBUG_MSG("worker: tbd\n");
        break;
      }
    }
    // There is a task in queue
    DEBUG_MSG("worker: acquire task\n");
    // Obtain function and argument pointers
    task_queue* temp = pool->task_head;

    // Move next task up in the queue
    pool->task_head = pool->task_head->task_next;

    // Cede lock
    err = mtx_unlock(&pool->pool_lock);
    if (err) {
      fprintf(stderr, "worker: unlock error, code %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }
    DEBUG_MSG("worker: cede lock\n");

    // Call task function.
    // (Call from the struct avoids compiler warnings)
    (temp->task_func)(temp->task_arg);

    free(temp);
    thrd_yield();
    // Return to beginning of the loop
  }
  DEBUG_MSG("worker: delete thread\n");
  // Thread will close after exiting loop
  pool->pool_threads_active--;
  // Check if this is the last thread being deleted
  if (!(pool->pool_threads_active + pool->pool_threads_idle)) {
    // Send final cleanup signal to threadpool_destroy function
    cnd_signal(&pool->pool_threads_deleted);
  }
  int err = mtx_unlock(&pool->pool_lock);
  if (err) {
      fprintf(stderr, "worker: unlock error, code %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
  }
  // Join back to avoid leaks
  thrd_exit(0);
}

threadpool* threadpool_create(size_t const num_threads) {
  if (!num_threads) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Cannot create a zero size thread pool.");
    return NULL;
  }

  // Freed in threadpool_destroy
  threadpool* pool = malloc(sizeof(threadpool)
                            + _Alignof(thrd_t)
                            + num_threads*sizeof(thrd_t));
  if (!pool) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Out of memory.");
    return NULL;
  }

  // Initialize thread pool data.
  // Deleted in threadpool_destroy
  int err;
  err = mtx_init(&pool->pool_lock, mtx_plain);
  if (err) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Could not initialize mutex.");
    return NULL;
  }
  err = cnd_init(&pool->pool_new_work_available);
  if (err) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Could not initialize condition variable.");
    return NULL;
  }
  err = cnd_init(&pool->pool_work_finished);
  if (err) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Could not initialize condition variable.");
    return NULL;
  }
  err = cnd_init(&pool->pool_threads_deleted);
  if (err) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Could not initialize condition variable.");
    return NULL;
  }

  pool->task_head = NULL;
  pool->task_tail = NULL;

  pool->pool_threads_idle = 0;
  pool->pool_threads_active = num_threads;
  pool->pool_threads_num = num_threads;
  pool->pool_to_be_deleted = false;

  for (size_t i = 0; i < pool->pool_threads_num; i++) {
    err = thrd_create(
      &(pool->thread_id[i]), worker_thread, pool);
    if (err) {
      PRINT_ERROR_LOCATION;
      fprintf(stderr, "Could not initialize worker threads.");
      // clean up threads that were initialized before error
      threadpool_destroy(pool);
      return NULL;
    }
  }
  return pool;
}

int threadpool_queue(threadpool* pool, void (*func)(void*), void* arg) {
  if (!pool) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Invalid thread pool.");
    return EXIT_FAILURE;
  }
  if (!func) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Invalid function pointer.");
    return EXIT_FAILURE;
  }
  if (pool->pool_to_be_deleted) { // Refuse additional tasks
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Thread pool is being deleted.");
    return EXIT_FAILURE;
  }

  task_queue* task;
  // Freed by worker_thread or threadpool_destroy
  task = malloc(sizeof(*task));
  if (!task) {
    PRINT_ERROR_LOCATION;
    fprintf(stderr, "Out of memory.");
    return EXIT_FAILURE;
  }

  task->task_next = NULL;
  task->task_func = func;
  task->task_arg = arg;

  // Wait to acquire lock
  int err = mtx_lock(&pool->pool_lock);
  if (err) {
      fprintf(stderr, "lock error %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }

  if (!pool->task_head) {
    // Task queue empty
    pool->task_head = task;
    pool->task_tail = task;
  }
  else {
    // Task queue populated, append task
    pool->task_tail->task_next = task;
    pool->task_tail = task;
  }

  // Signal to idle threads there is work available
  if (pool->pool_threads_idle)
      cnd_signal(&pool->pool_new_work_available);

  err = mtx_unlock(&pool->pool_lock);
  if (err) {
      fprintf(stderr, "unlock error %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }

  return EXIT_SUCCESS;
}

void threadpool_wait(threadpool* pool) {
  int err = mtx_lock(&pool->pool_lock);
  if (err) {
    fprintf(stderr, "lock error %d", err);
    PRINT_ERROR_LOCATION;
    exit(EXIT_FAILURE);
  }

  // Check if there is still work being done
  if (pool->pool_threads_idle != pool->pool_threads_num) {
    // If so, wait for a signal from the last worker_thread to finish
    while (pool->task_head != NULL || pool->pool_threads_active) {
      cnd_wait(&pool->pool_work_finished, &pool->pool_lock);
    }
  }
  err = mtx_unlock(&pool->pool_lock);
  if (err) {
    fprintf(stderr, "worker: error acquiring lock, code %d", err);
    PRINT_ERROR_LOCATION;
    exit(EXIT_FAILURE);
  }
  return;
}

// Read number of active threads
size_t threadpool_active_num(threadpool* pool) {
  return pool->pool_threads_active;
}

void threadpool_destroy(threadpool* pool) {
  DEBUG_MSG("mark pool tbd\n");
  // Mark pool to be deleted (new tasks will stop being executed)
  pool->pool_to_be_deleted = true;
  DEBUG_MSG("wake up idle workers\n");
  // Wake up idle workers, cede lock to each
  while (pool->pool_threads_idle || pool->pool_threads_active) {
    DEBUG_MSG("acquire lock\n");
    int err = mtx_lock(&pool->pool_lock);
    if (err) {
      fprintf(stderr, "lock error %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }
    DEBUG_MSG("broadcast\n");
    err = cnd_broadcast(&pool->pool_new_work_available);
    if (err) {
      fprintf(stderr, "condition broadcast %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }
    err = mtx_unlock(&pool->pool_lock);
    if (err) {
      fprintf(stderr, "worker: error acquiring lock, code %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
    }
    thrd_yield();
  }
  DEBUG_MSG("done waking up workers. reacquiring lock\n");
  assert(!pool->pool_threads_idle);
  int err = mtx_lock(&pool->pool_lock);
  if (err) {
      fprintf(stderr, "lock error %d", err);
      PRINT_ERROR_LOCATION;
      exit(EXIT_FAILURE);
  }
  // Wait for last thread to be deleted, then reacquire lock
  DEBUG_MSG("waiting on last thread to be deleted\n");
  while (pool->pool_threads_active) {
    cnd_wait(&pool->pool_threads_deleted, &pool->pool_lock);
  }

  assert(!pool->pool_threads_active);
  assert(!pool->pool_threads_idle);
  //assert(!pool->pool_threads_num);

  DEBUG_MSG("cleanup\n");
  // Destroy remaining tasks
  while (pool->task_head) {
    task_queue* task = pool->task_head;
    pool->task_head = pool->task_head->task_next;
    free(task);
  }

  int ret;
  for (size_t i = 0; i < pool->pool_threads_num; i++) {
    thrd_join(pool->thread_id[i], &ret);
    if (ret) {
      fprintf(stderr, "error from worker thread n. %lu", pool->thread_id[i]);
    }
  }

  cnd_destroy(&pool->pool_threads_deleted);
  cnd_destroy(&pool->pool_new_work_available);
  cnd_destroy(&pool->pool_work_finished);
  mtx_unlock(&pool->pool_lock);
  mtx_destroy(&pool->pool_lock);
  free(pool);
}

/**
 * @file mult-test.c
 * @brief Simple test program for threadpool.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threadpool.h"
#include <unistd.h>


typedef struct mul_wrp_arg mul_wrp_arg;
struct mul_wrp_arg {
  _Atomic(size_t)* product;
  size_t           factor;
};
void multiply_wrp(void* arg);

int main(int argc, char* argv[]) {
  if (argc < 3) {
    return EXIT_FAILURE;
  }

  size_t thread_num = strtoull(argv[1], NULL, 10);
  if (thread_num < 1 || thread_num > 1000)
    return EXIT_FAILURE;

  argc -= 2;
  argv += 2;

  size_t nums[argc];
  printf("(");
  for (size_t i = 0; i < argc; i++) {
    nums[i] = strtoull(argv[i], NULL, 10);
    if (!nums[i])
      return EXIT_FAILURE;
    printf("%zu, ", nums[i]);
  }
  printf(")\n");

  threadpool* pool = threadpool_create(nums[0]);

  _Atomic(size_t) product = 1;
  printf("queue operations:\n");
  for (size_t i = 0; i < argc; i++) {
    threadpool_queue(pool, multiply_wrp, &(mul_wrp_arg) {.product=&product, .factor=nums[i]});
    sleep(1);
  }

  printf("waiting for operations:\n");
  threadpool_wait(pool);


  printf("result = %zu\n", product);
  printf("deleting pool\n");
  threadpool_destroy(pool);
  return EXIT_SUCCESS;
}

void multiply(_Atomic(size_t)* a, size_t b) {
  *a *= b;
  sleep(2);
}

void multiply_wrp(void* arg) {
  mul_wrp_arg* input = arg;
  multiply(input->product, input->factor);
}


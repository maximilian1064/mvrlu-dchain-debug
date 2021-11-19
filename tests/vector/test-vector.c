#define _GNU_SOURCE

#include <inttypes.h>
// DPDK uses these but doesn't include them. :|
#include <linux/limits.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>

#include <pthread.h>
#include "vector.h"
#include "rlu-wrapper.h"

#define VECTOR_SIZE 3
#define NUM_ITERS 1000000

struct element {
  int a;
  int b;
};

static struct NfosVector *vector;

// TODO: Ugly to define those here, should have a rlu_wrapper.c
// RLU per thread data
rlu_thread_data_t **rlu_threads_data;
__thread int rlu_thread_id;

static void elem_init(void *obj) {
  struct element *elem = (struct element *)obj;
  elem->a = 0;
  elem->b = 0;
}

static int nfos_vector_test(void* id) {
  int core = *(int *)id;

  // set thread affinity
  // using cores from 0 to #cores - 1
	cpu_set_t cpuset;
	pthread_t thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(core * 2, &cpuset);
	pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

  // set rlu thread id
  set_rlu_thread_id(core);
  printf("id %d %d\n", get_rlu_thread_id(), core);

  rlu_thread_data_t *rlu_data = get_rlu_thread_data();

  for (int i = 0; i < NUM_ITERS; i++) {
retry:
    RLU_READER_LOCK(rlu_data);
    int index = 1;
    struct element *elem;
    nfos_vector_borrow(vector, index, (void **)&elem);
    int new_a = elem->a + 1;
    int new_b = elem->b + 2;
    if (nfos_vector_borrow_mut(vector, index, (void **)&elem) == ABORT_HANDLER) {
      RLU_ABORT(rlu_data);
      goto retry;
    }
    elem->a = new_a;
    elem->b = new_b;
    // printf("%d %d\n", elem->a, elem->b);
    RLU_READER_UNLOCK(rlu_data);
  }

  return 0;
}

// Default placeholder idle task
static int idle_main(void* unused) {
  return 0;
}

int main(int argc, char *argv[]) {
  unsigned num_threads = NUM_CORES;

  // Init threads
  pthread_t *threads;
  if ((threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t))) == NULL) {
    printf("failed to malloc pthread_t\n");
    return 1;
  }

  // Init RLU
  RLU_INIT();
  // Init (mv-)RLU per-thread data
  rlu_threads_data = malloc(num_threads * sizeof(rlu_thread_data_t *));
  for (int i = 0; i < num_threads; i++) {
    rlu_threads_data[i] = RLU_THREAD_ALLOC();
    RLU_THREAD_INIT(rlu_threads_data[i]);
  }

  // Init DS
  nfos_vector_allocate(sizeof(struct element), VECTOR_SIZE, elem_init, &vector);
  // Check results
  for (int i = 0; i < VECTOR_SIZE; i++) {
    struct element *elem;
    nfos_vector_borrow_unsafe(vector, i, (void **)&elem);
    printf("vector[%d]: %d %d\n", i, elem->a, elem->b);
  }

  // Launch threads
  int *thread_ids = malloc(sizeof(int) * num_threads);
  for (int i = 0; i < num_threads; i++) {
    thread_ids[i] = i;
    if (pthread_create(&threads[i], NULL, nfos_vector_test, (void *)(&thread_ids[i])) != 0) {
      printf("failed to create thread %d\n", i);
      return 1;
    }
  }
  printf("threads started\n");

  for (int i = 0; i < num_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      printf("failed to join child thread %d\n", i);
      return 1;
    }
  }
  printf("threads finished\n");

  rlu_thread_data_t *rlu_data = get_rlu_thread_data();
  RLU_READER_LOCK(rlu_data);
  for (int i = 0; i < VECTOR_SIZE; i++) {
    struct element *elem;
    nfos_vector_borrow(vector, i, (void **)&elem);
    printf("vector[%d]: %d %d\n", i, elem->a, elem->b);
  }
  RLU_READER_UNLOCK(rlu_data);

  // FINI RLU
  for (int i = 0; i < num_threads; i++) {
    RLU_THREAD_FINISH(rlu_threads_data[i]);
  }
  RLU_FINISH();

  return 0;
}

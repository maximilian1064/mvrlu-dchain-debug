#define _GNU_SOURCE

#include <inttypes.h>
// DPDK uses these but doesn't include them. :|
#include <linux/limits.h>
#include <sys/types.h>

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "double-chain.h"
#include "rlu-wrapper.h"

// @Sanidhya consider changing this to 3250000, the dchain size I used in the bug scenario
#define DCHAIN_SIZE 64
#define NUM_ITERS 10000000

static struct NfosDoubleChain* dchain;
static int alloc_inds[NUM_CORES][DCHAIN_SIZE];

// TODO: Ugly to define those here, should have a rlu_wrapper.c
// RLU per thread data
rlu_thread_data_t **rlu_threads_data;
__thread int rlu_thread_id;

// Wrapper around random_r utils...
typedef struct rand_gen {
  struct random_data state;
  char statebuf[32];
} rand_gen_t;

static int rand_gen_init(int seed, rand_gen_t *generator) {
  memset(generator, 0, sizeof(rand_gen_t));
  return initstate_r(seed, generator->statebuf, 32, &(generator->state));
}

static int rand_gen_output(rand_gen_t *generator) {
  int32_t ret = 0;
  random_r(&(generator->state), &ret);
  return ret;
}

// Wrapper around dchain ops
static int alloc_index(struct NfosDoubleChain *dchain, int *ind_out) {
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();
  int ret = 0;

alloc_index_restart:
  RLU_READER_LOCK(rlu_data);
    ret = nfos_dchain_allocate_new_index(dchain, ind_out);
    if (ret == ABORT_HANDLER) {
      RLU_ABORT(rlu_data);
      goto alloc_index_restart;
    }
  RLU_READER_UNLOCK(rlu_data);

  return ret;
}

static int free_index(struct NfosDoubleChain *dchain, int ind) {
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();
  int ret = 0;

free_index_restart:
  RLU_READER_LOCK(rlu_data);
    ret = nfos_dchain_free_index(dchain, ind);
    if (ret == ABORT_HANDLER) {
      RLU_ABORT(rlu_data);
      goto free_index_restart;
    }
  RLU_READER_UNLOCK(rlu_data);

  return ret;
}

// Test concurrent dchain_alloc/free
static void *nfos_dchain_test_alloc_free_ind(void* id) {
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

  rand_gen_t rand_gen;
  rand_gen_init(core, &rand_gen);
  int num_alloc_inds = 0;

  // Alloc a random number of inds first
  int k = rand_gen_output(&rand_gen) % DCHAIN_SIZE;
  for (int i = 0; i < k; i++) {
    if (alloc_index(dchain, &alloc_inds[core][num_alloc_inds]))
      num_alloc_inds++;
  }

  for (int i = 0; i < NUM_ITERS; i++) {
    int rand_num = rand_gen_output(&rand_gen);

    // alloc
    if (rand_num > (INT32_MAX >> 2)) {
      if (alloc_index(dchain, &alloc_inds[core][num_alloc_inds]))
        num_alloc_inds++;
    // free a random index allocated by the same core
    } else if (num_alloc_inds > 0) {
      int freed_ind = rand_num % num_alloc_inds;
      free_index(dchain, alloc_inds[core][freed_ind]);
      alloc_inds[core][freed_ind] = alloc_inds[core][num_alloc_inds - 1];
      num_alloc_inds--;
    }
  }

  return NULL;
}

// --- Main ---

int main(int argc, char *argv[]) {
  // Initialize the Environment Abstraction Layer (EAL)
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

  // Init dchain
  nfos_dchain_allocate(DCHAIN_SIZE, &dchain);
  printf("== initial state of dchain (note the padding cells contain garbage) ==\n");
  nfos_dchain_dump(dchain, DCHAIN_SIZE);

  // Launch threads
  int *thread_ids = malloc(sizeof(int) * num_threads);
  for (int i = 0; i < num_threads; i++) {
    thread_ids[i] = i;
    if (pthread_create(&threads[i], NULL, nfos_dchain_test_alloc_free_ind, (void *)(&thread_ids[i])) != 0) {
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

  for (int i = 0; i < num_threads; i++) {
    RLU_THREAD_FINISH(rlu_threads_data[i]);
  }
  RLU_FINISH();

  printf("== dump dchain again to verify its integrity ==\n");
  nfos_dchain_dump(dchain, DCHAIN_SIZE);

  return 0;
}

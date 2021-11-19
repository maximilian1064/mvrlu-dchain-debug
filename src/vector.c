#include "vector.h"

#include <stdlib.h>

#include "rlu-wrapper.h"

struct NfosVector {
  void **elems;
  int elem_size;
  unsigned capacity;
};

int nfos_vector_allocate(int elem_size, unsigned capacity, 
            nfos_vector_init_elem_t init_elem, struct NfosVector **vector_out) {
  struct NfosVector *vector = (struct NfosVector *) malloc(sizeof(struct NfosVector));
  if (!vector) return 0;

  vector->elems = (void **) malloc(sizeof(void *) * (uint64_t)capacity);
  if (!vector->elems) {
    free(vector);
    return 0;
  }
  vector->elem_size = elem_size;
  vector->capacity = capacity;

  for (int i = 0; i < capacity; i++) {
    vector->elems[i] = RLU_ALLOC(elem_size);
    init_elem(vector->elems[i]);
  }

  *vector_out = vector;
  return 1;
}

int nfos_vector_borrow_mut(struct NfosVector *vector, int index, void **val_out) {
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();
  void *elem = vector->elems[index];
  if (!_mvrlu_try_lock(rlu_data, (void **)&elem, vector->elem_size)) {
    return ABORT_HANDLER;
  } else {
    *val_out = elem;
    return 1;
  }
}

void nfos_vector_borrow(struct NfosVector *vector, int index, void **val_out) {
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();
  void *elem = vector->elems[index];
  elem = RLU_DEREF(rlu_data, elem);
  *val_out = elem;
}

void nfos_vector_borrow_unsafe(struct NfosVector *vector, int index, void **val_out) {
  void *elem = vector->elems[index];
  *val_out = elem;
}

#pragma once

struct NfosVector;

typedef void (*nfos_vector_init_elem_t)(void *elem);

int nfos_vector_allocate(int elem_size, unsigned capacity, 
            nfos_vector_init_elem_t init_elem, struct NfosVector **vector_out);


// Get ptr to vector elem for write
int nfos_vector_borrow_mut(struct NfosVector *vector, int index, void **val_out);

// Get ptr to vector elem for read
void nfos_vector_borrow(struct NfosVector *vector, int index, void **val_out);

// Internal API, do not use
void nfos_vector_borrow_unsafe(struct NfosVector *vector, int index, void **val_out);

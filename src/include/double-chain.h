/* Double-chain index allocator */
#pragma once

#ifndef NUM_CORES
#define NUM_CORES 8
#endif

struct NfosDoubleChain;

//   Allocate memory and initialize a new double chain allocator. The produced
//   allocator will operate on indexes [0-index_range).
//   @param index_range - the limit on the number of allocated indexes.
//   @param chain_out - an output pointer that will hold the pointer to the newly
//                      allocated allocator in the case of success.
//   @returns 0 if the allocation failed, and 1 if the allocation is successful.
int nfos_dchain_allocate(int index_range, struct NfosDoubleChain** chain_out);

//   Allocate a fresh index. If there is an unused index in the range,
//   allocate it.
//   @param chain - pointer to the allocator.
//   @param index_out - output pointer to the newly allocated index.
//   @returns 0 if there is no space, and 1 if the allocation is successful.
int nfos_dchain_allocate_new_index(struct NfosDoubleChain* chain, int* index_out);

//   Free an index
//   @param chain - pointer to the allocator.
//   @param index - freed index.
//   @returns 1 if successful, 0 otherwise
int nfos_dchain_free_index(struct NfosDoubleChain* chain, int index);

void nfos_dchain_dump(struct NfosDoubleChain* chain, int index_range);

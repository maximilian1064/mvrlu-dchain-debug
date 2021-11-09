#include "double-chain-impl.h"
#include "double-chain.h"

#include "rlu-wrapper.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#ifndef NULL
#define NULL 0
#endif//NULL

struct NfosDoubleChain {
  struct nfos_dchain_cell **cells;
};

int nfos_dchain_allocate(int index_range, struct NfosDoubleChain** chain_out)
{
  int num_cores = NUM_CORES;

  struct NfosDoubleChain* old_chain_out = *chain_out;
  struct NfosDoubleChain* chain_alloc = (struct NfosDoubleChain*) malloc(sizeof(struct NfosDoubleChain));
  if (chain_alloc == NULL) return 0;
  *chain_out = (struct NfosDoubleChain*) chain_alloc;

  // Allocate array of cell pointers (index_range => #index cells, (num_cores << LIST_HEAD_PADDING) * 2 + 1 => #metadata cells)
  int num_cells = index_range + (num_cores << LIST_HEAD_PADDING) * 2 + 1;
  struct nfos_dchain_cell **cells_alloc =
    (struct nfos_dchain_cell **) malloc(sizeof(struct nfos_dchain_cell *) * num_cells);
  if (cells_alloc == NULL) {
    free(chain_alloc);
    *chain_out = old_chain_out;
    return 0;
  }
  (*chain_out)->cells = cells_alloc;

  // Allocate cell memory through mvrlu
  for (int i = 0; i < num_cells; i++)
    cells_alloc[i] = (struct nfos_dchain_cell *) RLU_ALLOC(sizeof(struct nfos_dchain_cell));

  // Initialize cells
  nfos_dchain_impl_init((*chain_out)->cells, index_range, num_cores);
  return 1;
}

int nfos_dchain_allocate_new_index(struct NfosDoubleChain* chain, int *index_out)
{
  // Get relative core id [0, #cores-1)
  int core_id = get_rlu_thread_id();

  // try to get index from the local per-core index pool
  int ret = nfos_dchain_impl_allocate_new_index(chain->cells, index_out, core_id);
  // try to get index from the global pool if local pool is empty
  if (ret == 0) {
    ret = nfos_dchain_impl_allocate_new_index_global(chain->cells, index_out, core_id);
  }

  return ret;
}

int nfos_dchain_free_index(struct NfosDoubleChain* chain, int index)
{
  int core_id = get_rlu_thread_id();

  // free the index
  return nfos_dchain_impl_free_index(chain->cells, index, core_id);
}

void nfos_dchain_dump(struct NfosDoubleChain* chain, int index_range)
{
  // the last core is not used for data plane
  int num_cores = NUM_CORES;
  int num_cells = index_range + (num_cores << LIST_HEAD_PADDING) * 2 + 1;
  struct nfos_dchain_cell **cells = chain->cells;

  printf("cell ind: list_ind next prev\n");
  for (int i = 0; i < num_cells; i++) {
    struct nfos_dchain_cell *cell = cells[i];
    printf("cell %d: %8d %8d %8d\n", i, cell->list_ind, cell->next, cell->prev);
  }
}

#include "double-chain-impl.h"
#include "rlu-wrapper.h"

/* check double-chain-impl.h for a detailed description of this data structure*/
static int ALLOC_LIST_HEAD, FREE_LIST_HEAD, GLOBAL_FREE_LIST_HEAD, INDEX_SHIFT, NUM_FREE_LISTS;

void nfos_dchain_impl_init(struct nfos_dchain_cell **cells, int size, int num_cores)
{
  ALLOC_LIST_HEAD = 0;
  FREE_LIST_HEAD = num_cores << LIST_HEAD_PADDING;
  GLOBAL_FREE_LIST_HEAD = (num_cores * 2) << LIST_HEAD_PADDING;
  INDEX_SHIFT = GLOBAL_FREE_LIST_HEAD + 1;
  NUM_FREE_LISTS = num_cores;

  // Init per-core lists of allocated index
  struct nfos_dchain_cell* al_head;
  int i = ALLOC_LIST_HEAD;
  for (; i < FREE_LIST_HEAD; i += (1 << LIST_HEAD_PADDING))
  {
    al_head = cells[i];
    al_head->prev = i;
    al_head->next = i;
    al_head->list_ind = i - ALLOC_LIST_HEAD;
  }

  // Init per-core lists of free index
  struct nfos_dchain_cell* fl_head;
  for (i = FREE_LIST_HEAD; i < GLOBAL_FREE_LIST_HEAD; i += (1 << LIST_HEAD_PADDING))
  {
    fl_head = cells[i];
    fl_head->next = INDEX_SHIFT + ((size / num_cores) * ((i - FREE_LIST_HEAD) >> LIST_HEAD_PADDING));
    fl_head->prev = fl_head->next;
    fl_head->list_ind = i - FREE_LIST_HEAD;
  }

  // Init global list of free index, empty at initialization
  struct nfos_dchain_cell* glb_fl_head = cells[GLOBAL_FREE_LIST_HEAD];
  glb_fl_head->next = GLOBAL_FREE_LIST_HEAD;
  glb_fl_head->prev = glb_fl_head->next;
  glb_fl_head->list_ind = -1;

  // Partition indexes among per-core lists of free index
  int core_id;
  for (i = INDEX_SHIFT, core_id = 0; i < size + INDEX_SHIFT; i += size / num_cores, core_id++)
  {
    int j;
    for (j = 0; (j < size / num_cores - 1) && (i + j < size + INDEX_SHIFT - 1); j++)
    {
        struct nfos_dchain_cell* current = cells[i + j];
        current->next = i + j + 1;
        current->prev = current->next;
        current->list_ind = -1;
    }
    struct nfos_dchain_cell* last = cells[i + j];
    last->next = FREE_LIST_HEAD + (core_id << LIST_HEAD_PADDING);
    last->prev = last->next;
    last->list_ind = -1;
  }

}

int nfos_dchain_impl_allocate_new_index(struct nfos_dchain_cell **cells, int *index, int core_id) 
{
  int al_head_ind = core_id << LIST_HEAD_PADDING;

  rlu_thread_data_t *rlu_data = get_rlu_thread_data();

  struct nfos_dchain_cell* fl_head = cells[FREE_LIST_HEAD + al_head_ind];
  struct nfos_dchain_cell* al_head = cells[ALLOC_LIST_HEAD + al_head_ind];

  fl_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, fl_head);
  int allocated = fl_head->next;
  // local free list is empty, allocation failed
  if (allocated == FREE_LIST_HEAD + al_head_ind)
  {
    return 0;
  }

  struct nfos_dchain_cell* allocp = cells[allocated];

  // Extract the link from the local free list.
  if (!RLU_TRY_LOCK(rlu_data, &fl_head)) {
    // notify the caller that there is mvrlu abort.
    return ABORT_HANDLER;
  }
  allocp = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, allocp);
  fl_head->next = allocp->next;
  fl_head->prev = fl_head->next;

  // Add the link to the end of the local alloc list.
  if (!RLU_TRY_LOCK(rlu_data, &allocp)) {
    return ABORT_HANDLER;
  }
  allocp->next = ALLOC_LIST_HEAD + al_head_ind;
  al_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, al_head);
  allocp->prev = al_head->prev;
  allocp->list_ind = al_head_ind;

  struct nfos_dchain_cell* alloc_head_prevp = cells[al_head->prev];
  if (!RLU_TRY_LOCK(rlu_data, &alloc_head_prevp)) {
    return ABORT_HANDLER;
  }
  alloc_head_prevp->next = allocated;

  if (!RLU_TRY_LOCK(rlu_data, &al_head)) {
    return ABORT_HANDLER;
  }
  al_head->prev = allocated;

  *index = allocated - INDEX_SHIFT;
  return 1;
}

// Try to move a free index from a local list to the global list.
// Returns 1 if succeeds
static inline int nfos_dchain_impl_fill_global_free_list(struct nfos_dchain_cell **cells, int core_id) 
{
  int fl_head_ind = core_id << LIST_HEAD_PADDING;
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();

  struct nfos_dchain_cell* fl_head = cells[FREE_LIST_HEAD + fl_head_ind];
  struct nfos_dchain_cell* glb_fl_head = cells[GLOBAL_FREE_LIST_HEAD];

  fl_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, fl_head);

  int allocated = fl_head->next;
  if (allocated == FREE_LIST_HEAD + fl_head_ind)
  {
    return 0;
  }
  struct nfos_dchain_cell* allocp = cells[allocated];
  // Extract the link from the local free list.
  if (!RLU_TRY_LOCK(rlu_data, &fl_head)) {
    return ABORT_HANDLER;
  }
  allocp = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, allocp);
  fl_head->next = allocp->next;
  fl_head->prev = fl_head->next;

  // Add the link to the global free list.
  if (!RLU_TRY_LOCK(rlu_data, &allocp)) {
    return ABORT_HANDLER;
  }
  glb_fl_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, glb_fl_head);
  allocp->next = glb_fl_head->next;
  allocp->prev = allocp->next;
  allocp->list_ind = -1;

  if (!RLU_TRY_LOCK(rlu_data, &glb_fl_head)) {
    return ABORT_HANDLER;
  }
  glb_fl_head->next = allocated;
  glb_fl_head->prev = glb_fl_head->next;

  return 1;
}

int nfos_dchain_impl_allocate_new_index_global(struct nfos_dchain_cell **cells, int *index, int core_id)
{
  int al_head_ind = core_id << LIST_HEAD_PADDING;
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();

  struct nfos_dchain_cell* glb_fl_head = cells[GLOBAL_FREE_LIST_HEAD];
  struct nfos_dchain_cell* al_head = cells[ALLOC_LIST_HEAD + al_head_ind];
  int allocated;
  int ret = 0;

  glb_fl_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, glb_fl_head);
  allocated = glb_fl_head->next;
  // the global free list is empty
  if (allocated == GLOBAL_FREE_LIST_HEAD)
  {
    // Try to take one index from each of the per-core free lists and migrate them to the global free list
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
      if (nfos_dchain_impl_fill_global_free_list(cells, i) == ABORT_HANDLER)
        return ABORT_HANDLER;
    }

    // retry allocation from the global free list
    glb_fl_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, glb_fl_head);
    allocated = glb_fl_head->next;
    if (allocated != GLOBAL_FREE_LIST_HEAD)
      ret = 1;

  } else {
    ret = 1;
  }
 
  if (ret == 0) {
    return 0;
  }

  struct nfos_dchain_cell* allocp = cells[allocated];
  // Extract the link from the global free list.
  if (!RLU_TRY_LOCK(rlu_data, &glb_fl_head)) {
    return ABORT_HANDLER;
  }
  allocp = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, allocp);
  glb_fl_head->next = allocp->next;
  glb_fl_head->prev = glb_fl_head->next;

  // Add the link to the end of the local alloc list.
  if (!RLU_TRY_LOCK(rlu_data, &allocp)) {
    return ABORT_HANDLER;
  }
  allocp->next = ALLOC_LIST_HEAD + al_head_ind;
  al_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, al_head);
  allocp->prev = al_head->prev;
  allocp->list_ind = al_head_ind;

  struct nfos_dchain_cell* alloc_head_prevp = cells[al_head->prev];
  if (!RLU_TRY_LOCK(rlu_data, &alloc_head_prevp)) {
    return ABORT_HANDLER;
  }
  alloc_head_prevp->next = allocated;

  if (!RLU_TRY_LOCK(rlu_data, &al_head)) {
    return ABORT_HANDLER;
  }
  al_head->prev = allocated;

  *index = allocated - INDEX_SHIFT;
  return 1;
}

int nfos_dchain_impl_free_index(struct nfos_dchain_cell **cells, int index, int core_id)
{
  int al_head_ind = core_id << LIST_HEAD_PADDING;
  rlu_thread_data_t *rlu_data = get_rlu_thread_data();

  int freed = index + INDEX_SHIFT;
  struct nfos_dchain_cell* freedp = cells[freed];
  freedp = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, freedp);
  // The index is already free.
  if (freedp->list_ind == -1) {
    return 0;
  }

  // Extract the link from the local "alloc" list.
  int freed_prev = freedp->prev;
  int freed_next = freedp->next;
  struct nfos_dchain_cell* freed_prevp = cells[freed_prev];
  struct nfos_dchain_cell* freed_nextp = cells[freed_next];
  if ( (!RLU_TRY_LOCK(rlu_data, &freed_prevp)) || (!RLU_TRY_LOCK(rlu_data, &freed_nextp)) ) {
    return ABORT_HANDLER;
  }
  freed_prevp->next = freed_next;
  freed_nextp->prev = freed_prev;

  // Add the link to the local "free" list.
  struct nfos_dchain_cell* fr_head = cells[FREE_LIST_HEAD + al_head_ind];
  fr_head = (struct nfos_dchain_cell *) RLU_DEREF(rlu_data, fr_head);

  if (!RLU_TRY_LOCK(rlu_data, &freedp)) {
    return ABORT_HANDLER;
  }
  freedp->next = fr_head->next;
  freedp->prev = freedp->next;
  freedp->list_ind = -1;

  if (!RLU_TRY_LOCK(rlu_data, &fr_head)) {
    return ABORT_HANDLER;
  }
  fr_head->next = freed;
  fr_head->prev = fr_head->next;

  return 1;
}

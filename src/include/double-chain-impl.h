#pragma once

struct nfos_dchain_cell {
    int prev;
    int next;
    int list_ind; // ind of corresponding per-core index pool, -1 means the cell is free
};

// Separate free/alloc list head cells with 7 (2^3 - 1) padding cells
#define LIST_HEAD_PADDING 3

// Requires the array dchain_cell, large enough to fit all the range of
// possible 'index' values + metadata values.
// Forms 2 * #cores + 1 closed linked lists inside the array.
// The first #cores lists are per-core "alloc lists", they represent the cells alloced to each core. They are double linked lists.
// The next #cores lists are per-core "free lists", they represent the cells represent the "free" cells on each core. They are single linked lists.
// The last list is the global "free list", it represents the "free" cells in the global index pool. It is a single linked list. 
// Initially the whole array (except the special cells holding metadata) added to the "free" list.
//
//
// The lists are organized as follows (only showing one per-core free list and one per-core alloc list):
//              +----+   +---+   +-------------------+   +-----
//              |    V   |   V   |                   V   |
//..[. + .][    .]  {    .} {    .} {. + .} {. + .} {    .} ....
//   ^   ^                           ^   ^   ^   ^
//   |   |                           |   |   |   |
//   |   +---------------------------+   +---+   +-------------
//   +---------------------------------------------------------
//
// Where {    .} is an "free" list cell, and {. + .} is an "alloc" list cell,
// and dots represent prev/next fields.
// [] - denote the special cells (head of the lists) - the ones that are always kept in the
// corresponding lists.
// Empty "alloc" and "free" lists look like this:
//
//   +---+   +---+
//   V   V   V   |
//  [. + .] [    .]
//
// , i.e. cells[0].next == 0 && cells[0].prev == 0 for the "alloc" list, and
// cells[1].next == 1 for the free list.
// For any cell in the "alloc" list, 'prev' and 'next' fields must be different.
// Any cell in the "free" list, in contrast, have 'prev' and 'next' equal;
// After initialization, any cell is allways on one and only one of these lists.
//
// As a performance optimization, they are 7 padding cells between each two consective special cells (list head) to separate them on different cache lines

void nfos_dchain_impl_init(struct nfos_dchain_cell **cells, int index_range, int num_cores);

int nfos_dchain_impl_allocate_new_index(struct nfos_dchain_cell **cells, int *index, int core_id);

int nfos_dchain_impl_allocate_new_index_global(struct nfos_dchain_cell **cells, int *index, int core_id);

int nfos_dchain_impl_free_index(struct nfos_dchain_cell **cells, int index, int core_id);

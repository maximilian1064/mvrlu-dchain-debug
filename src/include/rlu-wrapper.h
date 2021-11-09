#pragma once

#include "mv-rlu/include/mvrlu.h"

#define ABORT_HANDLER (-1)

extern __thread int rlu_thread_id;
extern rlu_thread_data_t **rlu_threads_data;

static inline rlu_thread_data_t *get_rlu_thread_data() {
    return rlu_threads_data[rlu_thread_id];
}

static inline int get_rlu_thread_id() {
    return rlu_thread_id;
}

static inline int set_rlu_thread_id(int id) {
    rlu_thread_id = id;
}
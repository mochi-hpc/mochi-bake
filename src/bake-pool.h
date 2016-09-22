/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef BAKE_POOL_H
#define BAKE_POOL_H

#include <mercury_types.h>
#include <hg-bulk-pool.h>

/* Internal interface used for buffer pool management */

struct bake_bulk_pool_set
{
    hg_bulk_pool_t **pools;
    hg_size_t npools;
    hg_size_t nbufs;
    hg_size_t init_size;
    hg_size_t size_multiple;
};
typedef struct bake_bulk_pool_set bake_bulk_pool_set_t;

hg_bulk_t get_pool_bulk(hg_size_t s, hg_uint8_t flag);

void release_pool_bulk(hg_size_t s, hg_bulk_t bulk, hg_uint8_t flag);

int is_pool_enabled(void);

#endif

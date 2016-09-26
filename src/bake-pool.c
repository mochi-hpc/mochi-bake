/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>

#include "bake-pool.h"

hg_bulk_pool_set_t *poolset_rd = NULL;
hg_bulk_pool_set_t *poolset_wr = NULL;
hg_bulk_pool_set_t *poolset_rw = NULL;

void bake_set_buffer_pool_set(hg_bulk_pool_set_t *poolset)
{
    hg_uint8_t flag = hg_bulk_pool_set_get_flag(poolset);
    if (flag == HG_BULK_READ_ONLY) {
        poolset_rd = poolset;
        poolset_rw = NULL;
    }
    else if (flag == HG_BULK_WRITE_ONLY) {
        poolset_wr = poolset;
        poolset_rw = NULL;
    }
    else if (flag == HG_BULK_READWRITE) {
        poolset_rw = poolset;
        poolset_wr = NULL;
        poolset_rd = NULL;
    }
    else assert(0 && "Bad flag");
}

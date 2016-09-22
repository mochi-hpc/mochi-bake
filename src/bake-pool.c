/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>

#include "bake-pool.h"

static bake_bulk_pool_set_t poolset_rd = { NULL, 0, 0, 0, 0 };
static bake_bulk_pool_set_t poolset_wr = { NULL, 0, 0, 0, 0 };

hg_bulk_t get_pool_bulk(hg_size_t s, hg_uint8_t flag)
{
    bake_bulk_pool_set_t *ps = (flag == HG_BULK_READ_ONLY) ?
        &poolset_rd : &poolset_wr;

    hg_size_t i, size = ps->init_size, sm = ps->size_multiple;
    for (i = 0; i < ps->npools; i++) {
        if (s < size) return hg_bulk_pool_get(ps->pools[i]);
        else size *= sm;
    }
    return HG_BULK_NULL;
}

void release_pool_bulk(hg_size_t s, hg_bulk_t bulk, hg_uint8_t flag)
{
    bake_bulk_pool_set_t *ps = (flag == HG_BULK_READ_ONLY) ?
        &poolset_rd : &poolset_wr;
    hg_size_t i, size = ps->init_size, sm = ps->size_multiple;
    for (i = 0; i < ps->npools; i++) {
        if (s < size) { hg_bulk_pool_release(ps->pools[i], bulk); return; }
        else size *= sm;
    }
    assert(0);
}

int is_pool_enabled(void) { return poolset_rd.pools != NULL; }

static hg_return_t create_buffer_pool_set_helper(
        hg_class_t *hgcl,
        hg_size_t npools,
        hg_size_t nbufs,
        hg_size_t init_size,
        hg_size_t size_multiple,
        hg_bulk_pool_thread_opt_t thread_opt,
        hg_uint8_t bulk_flag,
        bake_bulk_pool_set_t *ps)
{
    hg_size_t i, j, size;
    hg_return_t hret;

    ps->pools = malloc(npools * sizeof(*ps->pools));
    if (ps->pools == NULL) return HG_NOMEM_ERROR;

    ps->npools = npools;
    ps->nbufs = nbufs;
    ps->init_size = init_size;
    ps->size_multiple = size_multiple;

    size = init_size;
    for (i = 0; i < npools; i++) {
        hret = hg_bulk_pool_create(hgcl, nbufs, size, bulk_flag, thread_opt,
                &ps->pools[i]);
        if (hret != HG_SUCCESS) {
            for (j = 0; i < i; j++)
                hg_bulk_pool_destroy(ps->pools[j]);
            free(ps->pools);
            ps->pools = NULL;
            return hret;
        }
        size *= size_multiple;
    }
    return HG_SUCCESS;
}

/* set the buffer pool for use by rpcs - see bake-bulk.h */
hg_return_t bake_create_buffer_pool_set(
        hg_class_t *hgcl,
        hg_size_t npools,
        hg_size_t nbufs,
        hg_size_t init_size,
        hg_size_t size_multiple,
        hg_bulk_pool_thread_opt_t thread_opt)
{
    hg_return_t hret;
    hret = create_buffer_pool_set_helper(hgcl, npools, nbufs, init_size,
            size_multiple, thread_opt, HG_BULK_READ_ONLY, &poolset_rd);
    if (hret != HG_SUCCESS) return hret;

    return create_buffer_pool_set_helper(hgcl, npools, nbufs, init_size,
            size_multiple, thread_opt, HG_BULK_WRITE_ONLY, &poolset_wr);
}

void bake_destroy_buffer_pool_set(void)
{
    hg_size_t i;
    for (i = 0; i < poolset_rd.npools; i++) {
        hg_bulk_pool_destroy(poolset_rd.pools[i]);
        hg_bulk_pool_destroy(poolset_wr.pools[i]);
    }
}


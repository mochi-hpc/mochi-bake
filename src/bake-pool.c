/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>

#include "bake-pool.h"

int is_poolset_rd_external = 0;
int is_poolset_wr_external = 0;
int is_poolset_rw_external = 0;
hg_bulk_pool_set_t *poolset_rd = NULL;
hg_bulk_pool_set_t *poolset_wr = NULL;
hg_bulk_pool_set_t *poolset_rw = NULL;

void bake_set_buffer_pool_set(hg_bulk_pool_set_t *poolset)
{
    hg_uint8_t flag = hg_bulk_pool_set_get_flag(poolset);
    if (flag == HG_BULK_READ_ONLY) {
        if (poolset_rd != NULL && !is_poolset_rd_external)
            hg_bulk_pool_set_destroy(poolset_rd);
        poolset_rd = poolset;
        if (poolset_rw != NULL && !is_poolset_rw_external)
            hg_bulk_pool_set_destroy(poolset_rw);
        poolset_rw = NULL;
        is_poolset_rd_external = 1;
    }
    else if (flag == HG_BULK_WRITE_ONLY) {
        if (poolset_wr != NULL && !is_poolset_wr_external)
            hg_bulk_pool_set_destroy(poolset_wr);
        poolset_wr = poolset;
        if (poolset_rw != NULL && !is_poolset_rw_external)
            hg_bulk_pool_set_destroy(poolset_rw);
        poolset_rw = NULL;
        is_poolset_wr_external = 1;
        is_poolset_rw_external = 0;
    }
    else if (flag == HG_BULK_READWRITE) {
        poolset_rw = poolset;
        if (poolset_wr != NULL && !is_poolset_wr_external)
            hg_bulk_pool_set_destroy(poolset_wr);
        poolset_wr = NULL;
        if (poolset_rd != NULL && !is_poolset_rd_external)
            hg_bulk_pool_set_destroy(poolset_rd);
        poolset_rd = NULL;
        is_poolset_rw_external = 1;
        is_poolset_rd_external = 0;
        is_poolset_wr_external = 0;
    }
    else { assert(0 && "Bad flag"); }
}

void init_pools(hg_class_t *hg_class)
{
    hg_return_t hret;
    if (poolset_rw == NULL && poolset_rd == NULL) {
        hret = hg_bulk_pool_set_create(hg_class, 0, 0, 0, 0, HG_BULK_READ_ONLY,
                HG_BULK_POOL_THREAD_NONE, &poolset_rd);
        assert(hret == HG_SUCCESS);
        is_poolset_rd_external = 0;
    }
    if (poolset_rw == NULL && poolset_rd == NULL) {
        hret = hg_bulk_pool_set_create(hg_class, 0, 0, 0, 0, HG_BULK_WRITE_ONLY,
                HG_BULK_POOL_THREAD_NONE, &poolset_wr);
        assert(hret == HG_SUCCESS);
        is_poolset_wr_external = 0;
    }
}

void fini_pools(void)
{
    if (poolset_rd != NULL && !is_poolset_rd_external) {
        hg_bulk_pool_set_destroy(poolset_rd);
        poolset_rd = NULL;
        is_poolset_rd_external = 0;
    }
    if (poolset_wr != NULL && !is_poolset_wr_external) {
        hg_bulk_pool_set_destroy(poolset_wr);
        poolset_wr = NULL;
        is_poolset_wr_external = 0;
    }
    if (poolset_rw != NULL && !is_poolset_rw_external) {
        hg_bulk_pool_set_destroy(poolset_rw);
        poolset_rw = NULL;
        is_poolset_rw_external = 0;
    }
}

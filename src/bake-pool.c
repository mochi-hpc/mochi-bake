/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bake-pool.h"
#include <mercury.h>

#define DO_DEBUG 0
#define DEBUG(fmt, ...) \
    do { \
        if (DO_DEBUG) { \
            printf(fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define ALWAYS_USE_POOLS 0


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

static void init_default_pools(hg_class_t *hg_class, int use_noop_pools)
{
    hg_size_t npools, nbufs;
    hg_size_t size_init, size_multiple, size_max;
    hg_size_t eager_in_limit, eager_out_limit, eager_min_limit;
    // TODO: remove magic constant
    const hg_size_t max_bulk_size = 32768;
    hg_return_t hret;

    if (use_noop_pools) {
        npools = 0;
        nbufs = 0;
        size_init = 0;
        size_multiple = 0;
    }
    else {
        // TODO: remove magic constants
        nbufs = 4;
        size_multiple = 2;

        // heuristic: find the eager limit for read and write,
        // start at the nearest power of two larger than it, and extend a couple
        // levels
        // TODO: we don't know here whether the caller is a "server" or "client",
        // so we make the worst case assumption and take the smallest of the
        // request/response sizes
        eager_in_limit  = HG_Class_get_input_eager_size(hg_class);
        eager_out_limit = HG_Class_get_output_eager_size(hg_class);
        eager_min_limit = (eager_in_limit < eager_out_limit) ?
            eager_in_limit : eager_out_limit;

        // find the next largest power of two
        for (size_init = 1; size_init < eager_min_limit; size_init *= 2) { }

        // use an npools that will get us up to and including the predefined
        // max bulk size
        for (npools = 1, size_max = size_init;
                size_max < max_bulk_size;
                npools++, size_max *= size_multiple) { }
    }

    if (poolset_rw == NULL && poolset_rd == NULL) {
        DEBUG("Creating bulk read pool: npools:%lu, nbufs:%lu, size_init:%lu,"
                " size_multiple:%lu\n", npools, nbufs, size_init, size_multiple);
        hret = hg_bulk_pool_set_create(hg_class, npools, nbufs, size_init,
                size_multiple, HG_BULK_READ_ONLY, HG_BULK_POOL_THREAD_ABT,
                &poolset_rd);
        assert(hret == HG_SUCCESS);
        is_poolset_rd_external = 0;
    }
    if (poolset_rw == NULL && poolset_wr == NULL) {
        DEBUG("Creating bulk write pool: npools:%lu, nbufs:%lu, size_init:%lu,"
                " size_multiple:%lu\n", npools, nbufs, size_init, size_multiple);
        hret = hg_bulk_pool_set_create(hg_class, npools, nbufs, size_init,
                size_multiple, HG_BULK_WRITE_ONLY, HG_BULK_POOL_THREAD_ABT,
                &poolset_wr);
        assert(hret == HG_SUCCESS);
        is_poolset_wr_external = 0;
    }
}

void init_pools(hg_class_t *hg_class)
{
    const char *class_str, *protocol_str;
    int use_noop_pools = 1;

    /* TODO: directly checking for the plugins where bulk registration is
     * expensive (cci+verbs, cci+gni). Put more checks as needed. */
    class_str = HG_Class_get_name(hg_class);
    protocol_str = HG_Class_get_protocol(hg_class);

    if (ALWAYS_USE_POOLS || (!strcmp(class_str, "cci") &&
            (!strcmp(protocol_str, "verbs") || !strcmp(protocol_str, "gni")))) {
        use_noop_pools = 0;
    }

    init_default_pools(hg_class, use_noop_pools);
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

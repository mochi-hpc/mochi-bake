/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef BAKE_POOL_H
#define BAKE_POOL_H

#include <mercury_types.h>
#include <hg-bulk-pool.h>

/* buffer pool globals */

extern hg_bulk_pool_set_t *poolset_rd;
extern hg_bulk_pool_set_t *poolset_wr;
extern hg_bulk_pool_set_t *poolset_rw;

#endif

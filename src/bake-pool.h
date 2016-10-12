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

/*  init flags */
extern int is_poolset_rd_external;
extern int is_poolset_wr_external;
extern int is_poolset_rw_external;

extern hg_bulk_pool_set_t *poolset_rd;
extern hg_bulk_pool_set_t *poolset_wr;
extern hg_bulk_pool_set_t *poolset_rw;

/* NOTE: init_pools is reentrant and respects previous sets to the poolsets */
void init_pools(hg_class_t *hg_class);
void fini_pools(void);

#endif

/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __BAKE_PROVIDER_H
#define __BAKE_PROVIDER_H

#include "bake-config.h"

#include <libpmemobj.h>
#include <unistd.h>
#include <fcntl.h>
#include <margo.h>
#include <margo-bulk-pool.h>
#ifdef USE_REMI
    #include <remi/remi-client.h>
    #include <remi/remi-server.h>
#endif
#include "bake-server.h"
#include "bake-backend.h"
#include "uthash.h"

typedef struct {
    bake_target_id_t  target_id;
    backend_context_t context;
    bake_backend_t    backend;
    UT_hash_handle    hh;
} bake_target_t;

typedef struct bake_provider {
    margo_instance_id mid;
    ABT_pool   handler_pool; // pool used to run RPC handlers for this provider
    ABT_rwlock lock; // write-locked during migration, read-locked by all other
    // operations. There should be something better to avoid locking everything
    // but we are going with that for simplicity for now.
    uint64_t       num_targets;
    bake_target_t* targets;
    abt_io_instance_id
        aid; /* externally provided abt-io instance, if present */
    hg_id_t
        bake_create_write_persist_id; // <-- this is a client version of the id

#ifdef USE_REMI
    remi_client_t   remi_client;
    remi_provider_t remi_provider;
#endif

    margo_bulk_poolset_t poolset; /* intermediate buffers, if used */

    // list of RPC ids
    hg_id_t rpc_create_id;
    hg_id_t rpc_write_id;
    hg_id_t rpc_eager_write_id;
    hg_id_t rpc_persist_id;
    hg_id_t rpc_create_write_persist_id;
    hg_id_t rpc_eager_create_write_persist_id;
    hg_id_t rpc_get_size_id;
    hg_id_t rpc_get_data_id;
    hg_id_t rpc_read_id;
    hg_id_t rpc_eager_read_id;
    hg_id_t rpc_probe_id;
    hg_id_t rpc_noop_id;
    hg_id_t rpc_remove_id;
    hg_id_t rpc_migrate_region_id;
    hg_id_t rpc_migrate_target_id;

    struct json_object* json_cfg;

} bake_provider;

#endif

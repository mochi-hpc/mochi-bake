/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <assert.h>
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
#include "uthash.h"
#include "bake-rpc.h"
#include "bake-timing.h"
#include "bake-provider.h"

extern bake_backend g_bake_pmem_backend;

DECLARE_MARGO_RPC_HANDLER(bake_shutdown_ult)
DECLARE_MARGO_RPC_HANDLER(bake_create_ult)
DECLARE_MARGO_RPC_HANDLER(bake_write_ult)
DECLARE_MARGO_RPC_HANDLER(bake_eager_write_ult)
DECLARE_MARGO_RPC_HANDLER(bake_persist_ult)
DECLARE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)
DECLARE_MARGO_RPC_HANDLER(bake_eager_create_write_persist_ult)
DECLARE_MARGO_RPC_HANDLER(bake_get_size_ult)
DECLARE_MARGO_RPC_HANDLER(bake_get_data_ult)
DECLARE_MARGO_RPC_HANDLER(bake_read_ult)
DECLARE_MARGO_RPC_HANDLER(bake_eager_read_ult)
DECLARE_MARGO_RPC_HANDLER(bake_probe_ult)
DECLARE_MARGO_RPC_HANDLER(bake_noop_ult)
DECLARE_MARGO_RPC_HANDLER(bake_remove_ult)
DECLARE_MARGO_RPC_HANDLER(bake_migrate_region_ult)
DECLARE_MARGO_RPC_HANDLER(bake_migrate_target_ult)

/* TODO: support different parameters per provider instance */
struct bake_provider_conf g_bake_provider_conf = 
{
    .pipeline_enable = 0,
    .pipeline_npools = 4,
    .pipeline_nbuffers_per_pool = 32,
    .pipeline_first_buffer_size = 65536,
    .pipeline_multiplier = 4
};

static bake_target_t* find_target_entry(bake_provider_t provider, bake_target_id_t target_id)
{
    bake_target_t* entry = NULL;
    HASH_FIND(hh, provider->targets, &target_id, sizeof(bake_target_id_t), entry);
    return entry;
}

static void bake_server_finalize_cb(void *data);

#ifdef USE_REMI
static int bake_target_post_migration_callback(remi_fileset_t fileset, void* provider);
#endif

int bake_provider_register(
        margo_instance_id mid,
        uint16_t provider_id,
        ABT_pool abt_pool,
        bake_provider_t* provider)
{
    bake_provider *tmp_provider;
    int ret;
    /* check if a provider with the same provider id already exists */
    {
        hg_id_t id;
        hg_bool_t flag;
        margo_provider_registered_name(mid, "bake_probe_rpc", provider_id, &id, &flag);
        if(flag == HG_TRUE) {
            fprintf(stderr, "bake_provider_register(): a BAKE provider with the same id (%d) already exists\n", provider_id);
            return BAKE_ERR_MERCURY;
        }
    }

    /* allocate the resulting structure */    
    tmp_provider = calloc(1,sizeof(*tmp_provider));
    if(!tmp_provider)
        return BAKE_ERR_ALLOCATION;

    tmp_provider->mid = mid;
    if(abt_pool != ABT_POOL_NULL)
        tmp_provider->handler_pool = abt_pool;
    else {
        margo_get_handler_pool(mid, &(tmp_provider->handler_pool));
        if(tmp_provider->handler_pool == ABT_POOL_NULL) {
            fprintf(stderr, "WARNING: provider handler pool is null\n");
        }
    }

    /* Create rwlock */
    ret = ABT_rwlock_create(&(tmp_provider->lock));
    if(ret != ABT_SUCCESS) {
        free(tmp_provider);
        return BAKE_ERR_ARGOBOTS;
    }

    /* register RPCs */
    hg_id_t rpc_id;
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_create_rpc",
            bake_create_in_t, bake_create_out_t, 
            bake_create_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_create_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_write_rpc",
            bake_write_in_t, bake_write_out_t, 
            bake_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_write_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_write_rpc",
            bake_eager_write_in_t, bake_eager_write_out_t, 
            bake_eager_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_eager_write_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_read_rpc",
            bake_eager_read_in_t, bake_eager_read_out_t, 
            bake_eager_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_eager_read_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_persist_rpc",
            bake_persist_in_t, bake_persist_out_t, 
            bake_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_create_write_persist_rpc",
            bake_create_write_persist_in_t, bake_create_write_persist_out_t,
            bake_create_write_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_create_write_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_create_write_persist_rpc",
            bake_eager_create_write_persist_in_t, bake_eager_create_write_persist_out_t,
            bake_eager_create_write_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_eager_create_write_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_size_rpc",
            bake_get_size_in_t, bake_get_size_out_t, 
            bake_get_size_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_get_size_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_data_rpc",
            bake_get_data_in_t, bake_get_data_out_t, 
            bake_get_data_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_get_data_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_read_rpc",
            bake_read_in_t, bake_read_out_t, 
            bake_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_read_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_probe_rpc",
            bake_probe_in_t, bake_probe_out_t, bake_probe_ult, 
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_probe_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_noop_rpc",
            void, void, bake_noop_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_noop_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_remove_rpc",
            bake_remove_in_t, bake_remove_out_t, bake_remove_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_remove_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_region_rpc",
            bake_migrate_region_in_t, bake_migrate_region_out_t, bake_migrate_region_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_migrate_region_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_target_rpc",
            bake_migrate_target_in_t, bake_migrate_target_out_t, bake_migrate_target_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_provider, NULL);
    tmp_provider->rpc_migrate_target_id = rpc_id;

    /* get a client-side version of the bake_create_write_persist RPC */
    hg_bool_t flag;
    margo_registered_name(mid, "bake_create_write_persist_rpc", &rpc_id, &flag);
    if(flag) {
        tmp_provider->bake_create_write_persist_id = rpc_id;
    } else {
        tmp_provider->bake_create_write_persist_id =
        MARGO_REGISTER(mid, "bake_create_write_persist_rpc",
                bake_create_write_persist_in_t, bake_create_write_persist_out_t, NULL);
    }

#ifdef USE_REMI
    /* register a REMI client */
    // TODO actually use an ABT-IO instance
    ret = remi_client_init(mid, ABT_IO_INSTANCE_NULL, &(tmp_provider->remi_client));
    if(ret != REMI_SUCCESS) {
        // XXX unregister RPCs, cleanup tmp_provider before returning
        return BAKE_ERR_REMI;
    }

    /* register a REMI provider */
    {
        int flag;
        remi_provider_t remi_provider;
        /* check if a REMI provider exists with the same provider id */
        remi_provider_registered(mid, provider_id, &flag, NULL, NULL, &remi_provider);
        if(flag) { /* REMI provider exists */
            tmp_provider->remi_provider = remi_provider;
            tmp_provider->owns_remi_provider = 0;
        } else { /* REMI provider does not exist */
            // TODO actually use an ABT-IO instance
            ret = remi_provider_register(mid, ABT_IO_INSTANCE_NULL, provider_id, abt_pool, &(tmp_provider->remi_provider));
            if(ret != REMI_SUCCESS) {
                // XXX unregister RPCs, cleanup tmp_provider before returning
                return BAKE_ERR_REMI;
            }
            tmp_provider->owns_remi_provider = 1;
        }
        ret = remi_provider_register_migration_class(tmp_provider->remi_provider,
                "bake", NULL,
                bake_target_post_migration_callback, NULL, tmp_provider);
        if(ret != REMI_SUCCESS) {
            // XXX unregister RPCs, cleanup tmp_provider before returning
            return BAKE_ERR_REMI;
        }
    }
#endif

    /* install the bake server finalize callback */
    margo_provider_push_finalize_callback(mid, tmp_provider, &bake_server_finalize_cb, tmp_provider);

    if(provider != BAKE_PROVIDER_IGNORE)
        *provider = tmp_provider;

    return BAKE_SUCCESS;
}

int bake_provider_destroy(bake_provider_t provider)
{
    margo_provider_pop_finalize_callback(provider->mid, provider);
    bake_server_finalize_cb(provider);
    return BAKE_SUCCESS;
}

int bake_provider_add_storage_target(
        bake_provider_t provider,
        const char *target_name,
        bake_target_id_t* target_id)
{
    int ret = BAKE_SUCCESS;
    bake_target_id_t tid;
    backend_context_t ctx = NULL;
    bake_target_t* new_entry = calloc(1, sizeof(*new_entry));
    new_entry->backend = &g_bake_pmem_backend; // TODO make that depend on the name
    ret = new_entry->backend->_initialize(provider, target_name, &tid, &ctx);
    if(ret != 0) {
        free(new_entry);
        return ret;
    }
    new_entry->context = ctx;
    new_entry->target_id = tid;

    /* write-lock the provider */
    ABT_rwlock_wrlock(provider->lock);
    /* insert in the provider's hash */
    HASH_ADD(hh, provider->targets, target_id, sizeof(bake_target_id_t), new_entry);
    /* check that it was inserted */
    bake_target_t* check_entry = NULL;
    HASH_FIND(hh, provider->targets, &tid, sizeof(bake_target_id_t), check_entry);
    if(check_entry != new_entry) {
        fprintf(stderr, "Error: BAKE could not insert new pmem pool into the hash\n");
        new_entry->backend->_finalize(ctx);
        free(new_entry);
        ret = BAKE_ERR_ALLOCATION;
    } else {
        provider->num_targets += 1;
        *target_id = new_entry->target_id;
        ret = BAKE_SUCCESS;
    }
    /* unlock provider */
    ABT_rwlock_unlock(provider->lock);
    return ret;
}

int bake_provider_remove_storage_target(
        bake_provider_t provider,
        bake_target_id_t target_id)
{
    int ret;
    ABT_rwlock_wrlock(provider->lock);
    bake_target_t* entry = NULL;
    HASH_FIND(hh, provider->targets, &target_id, sizeof(bake_target_id_t), entry);
    if(!entry) {
        ret = BAKE_ERR_UNKNOWN_TARGET;
    } else {
        HASH_DEL(provider->targets, entry);
        entry->backend->_finalize(entry->context);
        free(entry);
        ret = BAKE_SUCCESS;
    }
    ABT_rwlock_unlock(provider->lock);
    return ret;
}

int bake_provider_remove_all_storage_targets(
        bake_provider_t provider)
{
    ABT_rwlock_wrlock(provider->lock);
    bake_target_t *p, *tmp;
    HASH_ITER(hh, provider->targets, p, tmp) {
        HASH_DEL(provider->targets, p);
        p->backend->_finalize(p->context);
        free(p);
    }
    provider->num_targets = 0;
    margo_bulk_poolset_destroy(provider->poolset);
    ABT_rwlock_unlock(provider->lock);
    return BAKE_SUCCESS;
}

int bake_provider_count_storage_targets(
        bake_provider_t provider,
        uint64_t* num_targets)
{
    ABT_rwlock_rdlock(provider->lock);
    *num_targets = provider->num_targets;
    ABT_rwlock_unlock(provider->lock);
    return BAKE_SUCCESS;
}

int bake_provider_list_storage_targets(
        bake_provider_t provider,
        bake_target_id_t* targets)
{
    ABT_rwlock_rdlock(provider->lock);
    bake_target_t *p, *tmp;
    uint64_t i = 0;
    HASH_ITER(hh, provider->targets, p, tmp) {
        targets[i] = p->target_id;
        i += 1;
    }
    ABT_rwlock_unlock(provider->lock);
    return BAKE_SUCCESS;
}

#define DECLARE_LOCAL_VARS(rpc_name)               \
    margo_instance_id mid = MARGO_INSTANCE_NULL;   \
    bake_##rpc_name##_out_t out;                   \
    bake_##rpc_name##_in_t in;                     \
    hg_return_t hret;                              \
    ABT_rwlock lock = ABT_RWLOCK_NULL;             \
    const struct hg_info* info = NULL;             \
    bake_provider_t provider = NULL;               \
    bake_target_t* target = NULL

#define FIND_PROVIDER                                    \
    do {                                                 \
        mid = margo_hg_handle_get_instance(handle);      \
        assert(mid);                                     \
        info = margo_get_info(handle);                   \
        provider = margo_registered_data(mid, info->id); \
        if(!provider) {                                  \
            out.ret = BAKE_ERR_UNKNOWN_PROVIDER;         \
            goto finish;                                 \
        } \
    } while(0)

#define GET_RPC_INPUT                        \
    do {                                     \
        hret = margo_get_input(handle, &in); \
        if(hret != HG_SUCCESS) {             \
            out.ret = BAKE_ERR_MERCURY;      \
            goto finish;                     \
        }                                    \
    } while(0)

#define LOCK_PROVIDER            \
    do {                         \
        lock = provider->lock;   \
        ABT_rwlock_rdlock(lock); \
    } while(0)

#define FIND_TARGET                                   \
    do {                                              \
        target = find_target_entry(provider, in.bti); \
        if(target == NULL) {                          \
            out.ret = BAKE_ERR_UNKNOWN_TARGET;        \
            goto finish;                              \
        }                                             \
    } while(0)

#define UNLOCK_PROVIDER             \
    do {                            \
        if(lock != ABT_RWLOCK_NULL) \
            ABT_rwlock_unlock(lock);\
    } while(0)

#define RESPOND_AND_CLEANUP             \
    do {                                \
        margo_respond(handle, &out);    \
        margo_free_input(handle, &in);  \
        margo_destroy(handle);          \
    } while(0)

/* service a remote RPC that creates a BAKE region */
static void bake_create_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(create);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    memset(&out, 0, sizeof(out));
    out.ret = target->backend->_create(target->context, in.region_size, &out.rid);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_ult)

/* service a remote RPC that writes to a BAKE region */
static void bake_write_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(write);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    memset(&out, 0, sizeof(out));
    hg_addr_t src_addr = HG_ADDR_NULL;
    if(in.remote_addr_str && strlen(in.remote_addr_str)) {
        hret = margo_addr_lookup(mid, in.remote_addr_str, &src_addr);
    } else {
        hret = margo_addr_dup(mid, info->addr, &src_addr);
    }
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    out.ret = target->backend->_write_bulk(target->context,
                                           in.rid,
                                           in.region_offset,
                                           in.bulk_size,
                                           in.bulk_handle,
                                           src_addr,
                                           in.bulk_offset);

finish:
    UNLOCK_PROVIDER;
    margo_addr_free(mid, src_addr);
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_write_ult)

    /* service a remote RPC that writes to a BAKE region in eager mode */
static void bake_eager_write_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(eager_write);
    in.buffer = NULL;
    in.size = 0;
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;

    out.ret = target->backend->_write_raw(
                    target->context,
                    in.rid,
                    in.region_offset,
                    in.size,
                    in.buffer);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_write_ult)

    /* service a remote RPC that persists to a BAKE region */
static void bake_persist_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(persist);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    out.ret = target->backend->_persist(
                target->context,
                in.rid,
                in.offset,
                in.size);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_persist_ult)

static void bake_create_write_persist_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(create_write_persist);
    ABT_pool handler_pool;
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;
    memset(&out, 0, sizeof(out));

    hg_addr_t src_addr = HG_ADDR_NULL;
    if(in.remote_addr_str && strlen(in.remote_addr_str)) {
        hret = margo_addr_lookup(mid, in.remote_addr_str, &src_addr);
    } else {
        hret = margo_addr_dup(mid, info->addr, &src_addr);
    }
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    out.ret = target->backend->_create_write_persist_bulk(
                        target->context, in.bulk_handle, src_addr,
                        in.bulk_offset, in.bulk_size, &out.rid);
finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)

static void bake_eager_create_write_persist_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(eager_create_write_persist);
    in.buffer = NULL;
    in.size = 0;
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    memset(&out, 0, sizeof(out));

    out.ret = target->backend->_create_write_persist_raw(
                    target->context, in.buffer, in.size, &out.rid);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_create_write_persist_ult)

/* service a remote RPC that retrieves the size of a BAKE region */
static void bake_get_size_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(get_size);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    memset(&out, 0, sizeof(out));
    out.ret = target->backend->_get_region_size(
                target->context, in.rid, &out.size);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_get_size_ult)

    /* Get the raw pointer of a region */
static void bake_get_data_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(get_data);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;
    out.ptr = 0;

    out.ret = target->backend->_get_region_data(
                target->context, in.rid, (void**)&out.ptr);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_get_data_ult)

    /* service a remote RPC for a BAKE no-op */
static void bake_noop_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);

    margo_respond(handle, NULL);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_noop_ult)

/* TODO consolidate with write handler; read and write are nearly identical */
/* service a remote RPC that reads from a BAKE region */
static void bake_read_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(read);
    in.remote_addr_str = NULL;
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    memset(&out, 0, sizeof(out));
    hg_addr_t src_addr = HG_ADDR_NULL;
    if(in.remote_addr_str && strlen(in.remote_addr_str)) {
        hret = margo_addr_lookup(mid, in.remote_addr_str, &src_addr);
    } else {
        hret = margo_addr_dup(mid, info->addr, &src_addr);
    }
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    out.ret = target->backend->_read_bulk(target->context,
                                          in.rid,
                                          in.region_offset,
                                          in.bulk_size,
                                          in.bulk_handle,
                                          src_addr,
                                          in.bulk_offset,
                                          &out.size);

finish:
    UNLOCK_PROVIDER;
    margo_addr_free(mid, src_addr);
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_read_ult)

    /* service a remote RPC that reads from a BAKE region and eagerly sends
     * response */
static void bake_eager_read_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(eager_read);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    free_fn free_data = NULL;
    out.ret = target->backend->_read_raw(
                target->context, in.rid, in.region_offset,
                in.size, (void**)&out.buffer, &out.size, &free_data);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
    if(free_data)
        free_data(out.buffer);
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_read_ult)

    /* service a remote RPC that probes for a BAKE target id */
static void bake_probe_ult(hg_handle_t handle)
{
    bake_probe_out_t out;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t provider = 
        margo_registered_data(mid, hgi->id);
    if(!provider) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        margo_respond(handle, &out);
        margo_destroy(handle);
        return;
    }

    uint64_t targets_count;
    bake_provider_count_storage_targets(provider, &targets_count);
    bake_target_id_t targets[targets_count];
    bake_provider_list_storage_targets(provider, targets);

    out.ret = BAKE_SUCCESS;
    out.targets = targets;
    out.num_targets = targets_count;

    margo_respond(handle, &out);

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_probe_ult)

static void bake_remove_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(remove);
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;

    out.ret = target->backend->_remove(
                target->context, in.rid);
finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_remove_ult)

static void bake_migrate_region_ult(hg_handle_t handle)
{
    DECLARE_LOCAL_VARS(migrate_region);
    in.dest_addr = NULL;
    FIND_PROVIDER;
    GET_RPC_INPUT;
    LOCK_PROVIDER;
    FIND_TARGET;


    memset(&out, 0, sizeof(out));

    out.ret = target->backend->_migrate_region(
                target->context, in.source_rid,
                in.region_size, in.remove_src,
                in.dest_addr, in.dest_provider_id,
                in.dest_target_id, &out.dest_rid);

finish:
    UNLOCK_PROVIDER;
    RESPOND_AND_CLEANUP;
}
DEFINE_MARGO_RPC_HANDLER(bake_migrate_region_ult)

static void bake_migrate_target_ult(hg_handle_t handle)
{
    bake_migrate_target_in_t in;
    in.dest_remi_addr = NULL;
    in.dest_root = NULL;
    bake_migrate_target_out_t out;
    hg_addr_t dest_addr = HG_ADDR_NULL;
    hg_return_t hret;
    int ret;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t provider = margo_registered_data(mid, info->id);
    if(!provider) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

#ifdef USE_REMI

    remi_provider_handle_t remi_ph = REMI_PROVIDER_HANDLE_NULL;
    remi_fileset_t local_fileset = REMI_FILESET_NULL;
    /* lock provider */
    lock = provider->lock;
    ABT_rwlock_wrlock(lock);
    bake_target_t* entry = find_target_entry(provider, in.target_id);
    if(!entry) {
        out.ret = BAKE_ERR_UNKNOWN_TARGET;
        goto finish;
    }

    /* lookup the address of the destination REMI provider */
    hret = margo_addr_lookup(mid, in.dest_remi_addr, &dest_addr);
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    /* use the REMI client to create a REMI provider handle */
    ret = remi_provider_handle_create(provider->remi_client, 
            dest_addr, in.dest_remi_provider_id, &remi_ph);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }

    /* create a fileset */
    ret = remi_fileset_create("bake", entry->root, &local_fileset);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }
    /* fill the fileset */
    ret = remi_fileset_register_file(local_fileset, entry->filename);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }

    /* issue the migration */
    int status = 0;
    ret = remi_fileset_migrate(remi_ph, local_fileset, in.dest_root, in.remove_src, REMI_USE_ABTIO, &status);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }

    /* remove the target from the list of managed targets */
    bake_provider_remove_storage_target(provider, in.target_id);

    out.ret = BAKE_SUCCESS;

#else

    out.ret = BAKE_ERR_OP_UNSUPPORTED;

#endif

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
#ifdef USE_REMI
    remi_fileset_free(local_fileset);
    remi_provider_handle_release(remi_ph);
#endif
    margo_addr_free(mid, dest_addr);
    margo_free_input(handle, &in);
    margo_respond(handle, &out);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_migrate_target_ult)

static void bake_server_finalize_cb(void *data)
{
    bake_provider *provider = (bake_provider *)data;
    assert(provider);
    margo_instance_id mid = provider->mid;

    margo_deregister(mid, provider->rpc_create_id);
    margo_deregister(mid, provider->rpc_write_id);
    margo_deregister(mid, provider->rpc_eager_write_id);
    margo_deregister(mid, provider->rpc_persist_id);
    margo_deregister(mid, provider->rpc_create_write_persist_id);
    margo_deregister(mid, provider->rpc_eager_create_write_persist_id);
    margo_deregister(mid, provider->rpc_get_size_id);
    margo_deregister(mid, provider->rpc_get_data_id);
    margo_deregister(mid, provider->rpc_read_id);
    margo_deregister(mid, provider->rpc_eager_read_id);
    margo_deregister(mid, provider->rpc_probe_id);
    margo_deregister(mid, provider->rpc_noop_id);
    margo_deregister(mid, provider->rpc_remove_id);
    margo_deregister(mid, provider->rpc_migrate_region_id);
    margo_deregister(mid, provider->rpc_migrate_target_id);

#ifdef USE_REMI
    remi_client_finalize(provider->remi_client);
    if(provider->owns_remi_provider) {
        remi_provider_destroy(provider->remi_provider);
    }
#endif

    bake_provider_remove_all_storage_targets(provider);

    ABT_rwlock_free(&(provider->lock));

    if(provider->poolset)
        margo_bulk_poolset_destroy(provider->poolset);

    free(provider);

    return;
}

#ifdef USE_REMI

typedef struct migration_cb_args {
    char root[1024];
    bake_provider* provider;
} migration_cb_args;

static void migration_fileset_cb(const char* filename, void* arg)
{
    migration_cb_args* mig_args = (migration_cb_args*)arg;
    char fullname[1024];
    fullname[0] = '\0';
    strcat(fullname, mig_args->root);
    strcat(fullname, filename);
    bake_target_id_t tid;
    bake_provider_add_storage_target(mig_args->provider, fullname, &tid);
}

static int bake_target_post_migration_callback(remi_fileset_t fileset, void* uarg)
{
    migration_cb_args args;
    args.provider = (bake_provider *)uarg;
    size_t root_size = 1024;
    remi_fileset_get_root(fileset, args.root, &root_size);
    remi_fileset_foreach_file(fileset, migration_fileset_cb, &args);
    return 0;
}

#endif

static int set_conf_cb_pipeline_enabled(bake_provider_t provider, 
    const char* value)
{
    int ret;
    hg_return_t hret;

    static int first_call = 1;

    if(first_call) {

        ret = sscanf(value, "%u", &g_bake_provider_conf.pipeline_enable);
        if(ret != 1)
            return BAKE_ERR_INVALID_ARG;

        if(g_bake_provider_conf.pipeline_enable) {
            hret = margo_bulk_poolset_create(
                    provider->mid, 
                    g_bake_provider_conf.pipeline_npools,
                    g_bake_provider_conf.pipeline_nbuffers_per_pool,
                    g_bake_provider_conf.pipeline_first_buffer_size,
                    g_bake_provider_conf.pipeline_multiplier,
                    HG_BULK_READWRITE,
                    &(provider->poolset));
            if(hret != 0)
                return BAKE_ERR_MERCURY;
        }
        first_call = 0; // for now we only allow setting the parameter once
        return BAKE_SUCCESS;
    } else {
        return BAKE_ERR_FORBIDDEN;
    }
}

int bake_provider_set_conf(
        bake_provider_t provider,
        const char *key,
        const char *value)
{
    int ret;

    /* TODO: make this more generic, manually issuing callbacks for
     * particular keys right now.
     */
    if(strcmp(key, "pipeline_enabled") == 0)
        return set_conf_cb_pipeline_enabled(provider, value);
    else
        return BAKE_ERR_INVALID_ARG;
}

int bake_target_set_conf(
        bake_provider_t provider,
        bake_target_id_t tid,
        const char* key,
        const char* value)
{
    // TODO
    return 0;
}

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

/* definition of BAKE root data structure (just a uuid for now) */
typedef struct
{   
    bake_target_id_t pool_id;
} bake_root_t;

/* definition of internal BAKE region_id_t identifier for libpmemobj back end */
typedef struct
{
    PMEMoid oid;
} pmemobj_region_id_t;

typedef struct {
#ifdef USE_SIZECHECK_HEADERS
    uint64_t size;
#endif
    char data[1];
} region_content_t;

typedef struct
{
    PMEMobjpool* pmem_pool;
    bake_root_t* pmem_root;
    bake_target_id_t target_id;
    char* root;
    char* filename;
    UT_hash_handle hh;
} bake_pmem_entry_t;

typedef struct bake_server_context_t
{
    margo_instance_id mid;
    ABT_rwlock lock; // write-locked during migration, read-locked by all other
    // operations. There should be something better to avoid locking everything
    // but we are going with that for simplicity for now.
    uint64_t num_targets;
    bake_pmem_entry_t* targets;
    hg_id_t bake_create_write_persist_id; // <-- this is a client version of the id

#ifdef USE_REMI
    remi_client_t remi_client;
    remi_provider_t remi_provider;
    int owns_remi_provider;
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

} bake_server_context_t;

typedef struct xfer_args {
    margo_instance_id   mid;            // margo instance
    hg_addr_t           remote_addr;    // remote address
    hg_bulk_t           remote_bulk;    // remote bulk handle for transfers
    size_t              remote_offset;  // remote offset at which to take the data
    size_t              bulk_size;
    char*               local_ptr;
    size_t              bytes_issued;
    size_t              bytes_retired;
    margo_bulk_poolset_t poolset;
    size_t              poolset_max_size;
    int32_t             ret;            // return value of the xfer_ult function
    int                 done;
    int                 ults_active;
    ABT_mutex           mutex;
    ABT_eventual        eventual;
} xfer_args;

struct bake_provider_conf
{
    unsigned pipeline_enable;  /* pipeline yes or no; implies intermediate buffering */
    unsigned pipeline_npools;  /* number of preallocated buffer pools */
    unsigned pipeline_nbuffers_per_pool; /* buffers per buffer pool */
    unsigned pipeline_first_buffer_size; /* size of buffers in smallest pool */
    unsigned pipeline_multiplier;        /* factor size increase per pool */
};
/* TODO: support different parameters per provider instance */
struct bake_provider_conf global_bake_provider_conf = 
{
    .pipeline_enable = 0,
    .pipeline_npools = 4,
    .pipeline_nbuffers_per_pool = 32,
    .pipeline_first_buffer_size = 65536,
    .pipeline_multiplier = 4
};

static void bake_server_finalize_cb(void *data);

#ifdef USE_REMI
static int bake_target_post_migration_callback(remi_fileset_t fileset, void* provider);
#endif

static void xfer_ult(void *_args);

static int write_transfer_data(
    margo_instance_id mid, 
    bake_provider_t svr_ctx,
    PMEMoid pmoid,
    uint64_t region_offset, 
    hg_bulk_t remote_bulk,
    uint64_t remote_bulk_offset,
    uint64_t bulk_size,
    hg_string_t remote_addr_str, 
    hg_addr_t hgi_addr,
    ABT_pool target_pool);

int bake_makepool(
        const char *pool_name,
        size_t pool_size,
        mode_t pool_mode)
{
    PMEMobjpool *pool;
    PMEMoid root_oid;
    bake_root_t *root;

    pool = pmemobj_create(pool_name, NULL, pool_size, pool_mode);
    if(!pool)
    {
        fprintf(stderr, "pmemobj_create: %s\n", pmemobj_errormsg());
        return BAKE_ERR_PMEM;
    }

    /* find root */
    root_oid = pmemobj_root(pool, sizeof(bake_root_t));
    root = pmemobj_direct(root_oid);

    /* store the target id for this bake pool at the root */
    uuid_generate(root->pool_id.id);
    pmemobj_persist(pool, root, sizeof(bake_root_t));

    pmemobj_close(pool);

    return BAKE_SUCCESS;
}

int bake_provider_register(
        margo_instance_id mid,
        uint16_t provider_id,
        ABT_pool abt_pool,
        bake_provider_t* provider)
{
    bake_server_context_t *tmp_svr_ctx;
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
    tmp_svr_ctx = calloc(1,sizeof(*tmp_svr_ctx));
    if(!tmp_svr_ctx)
        return BAKE_ERR_ALLOCATION;

    tmp_svr_ctx->mid = mid;

    /* Create rwlock */
    ret = ABT_rwlock_create(&(tmp_svr_ctx->lock));
    if(ret != ABT_SUCCESS) {
        free(tmp_svr_ctx);
        return BAKE_ERR_ARGOBOTS;
    }

    /* register RPCs */
    hg_id_t rpc_id;
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_create_rpc",
            bake_create_in_t, bake_create_out_t, 
            bake_create_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_create_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_write_rpc",
            bake_write_in_t, bake_write_out_t, 
            bake_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_write_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_write_rpc",
            bake_eager_write_in_t, bake_eager_write_out_t, 
            bake_eager_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_eager_write_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_read_rpc",
            bake_eager_read_in_t, bake_eager_read_out_t, 
            bake_eager_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_eager_read_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_persist_rpc",
            bake_persist_in_t, bake_persist_out_t, 
            bake_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_create_write_persist_rpc",
            bake_create_write_persist_in_t, bake_create_write_persist_out_t,
            bake_create_write_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_create_write_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_create_write_persist_rpc",
            bake_eager_create_write_persist_in_t, bake_eager_create_write_persist_out_t,
            bake_eager_create_write_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_eager_create_write_persist_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_size_rpc",
            bake_get_size_in_t, bake_get_size_out_t, 
            bake_get_size_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_get_size_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_data_rpc",
            bake_get_data_in_t, bake_get_data_out_t, 
            bake_get_data_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_get_data_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_read_rpc",
            bake_read_in_t, bake_read_out_t, 
            bake_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_read_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_probe_rpc",
            bake_probe_in_t, bake_probe_out_t, bake_probe_ult, 
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_probe_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_noop_rpc",
            void, void, bake_noop_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_noop_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_remove_rpc",
            bake_remove_in_t, bake_remove_out_t, bake_remove_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_remove_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_region_rpc",
            bake_migrate_region_in_t, bake_migrate_region_out_t, bake_migrate_region_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_migrate_region_id = rpc_id;

    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_target_rpc",
            bake_migrate_target_in_t, bake_migrate_target_out_t, bake_migrate_target_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    tmp_svr_ctx->rpc_migrate_target_id = rpc_id;

    /* get a client-side version of the bake_create_write_persist RPC */
    hg_bool_t flag;
    margo_registered_name(mid, "bake_create_write_persist_rpc", &rpc_id, &flag);
    if(flag) {
        tmp_svr_ctx->bake_create_write_persist_id = rpc_id;
    } else {
        tmp_svr_ctx->bake_create_write_persist_id =
        MARGO_REGISTER(mid, "bake_create_write_persist_rpc",
                bake_create_write_persist_in_t, bake_create_write_persist_out_t, NULL);
    }

#ifdef USE_REMI
    /* register a REMI client */
    // TODO actually use an ABT-IO instance
    ret = remi_client_init(mid, ABT_IO_INSTANCE_NULL, &(tmp_svr_ctx->remi_client));
    if(ret != REMI_SUCCESS) {
        // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
        return BAKE_ERR_REMI;
    }

    /* register a REMI provider */
    {
        int flag;
        remi_provider_t remi_provider;
        /* check if a REMI provider exists with the same provider id */
        remi_provider_registered(mid, provider_id, &flag, NULL, NULL, &remi_provider);
        if(flag) { /* REMI provider exists */
            tmp_svr_ctx->remi_provider = remi_provider;
            tmp_svr_ctx->owns_remi_provider = 0;
        } else { /* REMI provider does not exist */
            // TODO actually use an ABT-IO instance
            ret = remi_provider_register(mid, ABT_IO_INSTANCE_NULL, provider_id, abt_pool, &(tmp_svr_ctx->remi_provider));
            if(ret != REMI_SUCCESS) {
                // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
                return BAKE_ERR_REMI;
            }
            tmp_svr_ctx->owns_remi_provider = 1;
        }
        ret = remi_provider_register_migration_class(tmp_svr_ctx->remi_provider,
                "bake", NULL,
                bake_target_post_migration_callback, NULL, tmp_svr_ctx);
        if(ret != REMI_SUCCESS) {
            // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
            return BAKE_ERR_REMI;
        }
    }
#endif

    /* install the bake server finalize callback */
    margo_provider_push_finalize_callback(mid, tmp_svr_ctx, &bake_server_finalize_cb, tmp_svr_ctx);

    if(provider != BAKE_PROVIDER_IGNORE)
        *provider = tmp_svr_ctx;

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
    bake_pmem_entry_t* new_entry = calloc(1, sizeof(*new_entry));
    new_entry->root = NULL;
    new_entry->filename = NULL;

    char* tmp = strrchr(target_name, '/');
    new_entry->filename = strdup(tmp);
    ptrdiff_t d = tmp - target_name;
    new_entry->root = strndup(target_name, d);

    new_entry->pmem_pool = pmemobj_open(target_name, NULL);
    if(!(new_entry->pmem_pool)) {
        fprintf(stderr, "pmemobj_open: %s\n", pmemobj_errormsg());
        free(new_entry->filename);
        free(new_entry->root);
        free(new_entry);
        return BAKE_ERR_PMEM;
    }

    /* check to make sure the root is properly set */
    PMEMoid root_oid = pmemobj_root(new_entry->pmem_pool, sizeof(bake_root_t));
    new_entry->pmem_root = pmemobj_direct(root_oid);
    bake_target_id_t key = new_entry->pmem_root->pool_id;
    new_entry->target_id = key;

    if(uuid_is_null(key.id))
    {
        fprintf(stderr, "Error: BAKE pool %s is not properly initialized\n", target_name);
        pmemobj_close(new_entry->pmem_pool);
        free(new_entry->filename);
        free(new_entry->root);
        free(new_entry);
        return BAKE_ERR_UNKNOWN_TARGET;
    }

    /* write-lock the provider */
    ABT_rwlock_wrlock(provider->lock);
    /* insert in the provider's hash */
    HASH_ADD(hh, provider->targets, target_id, sizeof(bake_target_id_t), new_entry);
    /* check that it was inserted */
    bake_pmem_entry_t* check_entry = NULL;
    HASH_FIND(hh, provider->targets, &key, sizeof(bake_target_id_t), check_entry);
    if(check_entry != new_entry) {
        fprintf(stderr, "Error: BAKE could not insert new pmem pool into the hash\n");
        pmemobj_close(new_entry->pmem_pool);
        free(new_entry->filename);
        free(new_entry->root);
        free(new_entry);
        ret = BAKE_ERR_ALLOCATION;
    } else {
        provider->num_targets += 1;
        *target_id = key;
        ret = BAKE_SUCCESS;
    }
    /* unlock provider */
    ABT_rwlock_unlock(provider->lock);
    return ret;
}

static bake_pmem_entry_t* find_pmem_entry(
            bake_provider_t provider,
            bake_target_id_t target_id)
{
    bake_pmem_entry_t* entry = NULL;
    HASH_FIND(hh, provider->targets, &target_id, sizeof(bake_target_id_t), entry);
    return entry;
}

int bake_provider_remove_storage_target(
        bake_provider_t provider,
        bake_target_id_t target_id)
{
    int ret;
    ABT_rwlock_wrlock(provider->lock);
    bake_pmem_entry_t* entry = NULL;
    HASH_FIND(hh, provider->targets, &target_id, sizeof(bake_target_id_t), entry);
    if(!entry) {
        ret = BAKE_ERR_UNKNOWN_TARGET;
    } else {
        pmemobj_close(entry->pmem_pool);
        HASH_DEL(provider->targets, entry);
        free(entry->filename);
        free(entry->root);
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
    bake_pmem_entry_t *p, *tmp;
    HASH_ITER(hh, provider->targets, p, tmp) {
        HASH_DEL(provider->targets, p);
        pmemobj_close(p->pmem_pool);
        free(p->filename);
        free(p->root);
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
    bake_pmem_entry_t *p, *tmp;
    uint64_t i = 0;
    HASH_ITER(hh, provider->targets, p, tmp) {
        targets[i] = p->target_id;
        i += 1;
    }
    ABT_rwlock_unlock(provider->lock);
    return BAKE_SUCCESS;
}

/* service a remote RPC that creates a BAKE region */
static void bake_create_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","alloc","persist","respond");
    bake_create_out_t out;
    bake_create_in_t in;
    hg_return_t hret;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t svr_ctx = 
        margo_registered_data(mid, info->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }
    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    /* find the pmem pool */
    bake_pmem_entry_t* entry = find_pmem_entry(svr_ctx, in.bti);
    if(entry == NULL) {
        out.ret = BAKE_ERR_UNKNOWN_TARGET;
        goto finish;
    }

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

    memset(&out, 0, sizeof(out));

    prid = (pmemobj_region_id_t*)out.rid.data;

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = in.region_size + sizeof(uint64_t);
#else
    size_t content_size = in.region_size;
#endif

    TIMERS_END_STEP(0);

    int ret = pmemobj_alloc(entry->pmem_pool, &prid->oid,
            content_size, 0, NULL, NULL);
    if(ret != 0) {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

    TIMERS_END_STEP(1);

#ifdef USE_SIZECHECK_HEADERS
    region_content_t* region = (region_content_t*)pmemobj_direct(prid->oid);
    if(!region) {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }
    region->size = in.region_size;
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
    pmemobj_persist(pmem_pool, region, sizeof(region->size));
#endif

    TIMERS_END_STEP(2);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(3);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_ult)

static int write_transfer_data(
    margo_instance_id mid, 
    bake_provider_t svr_ctx,
    PMEMoid pmoid,
    uint64_t region_offset, 
    hg_bulk_t remote_bulk,
    uint64_t remote_bulk_offset,
    uint64_t bulk_size,
    hg_string_t remote_addr_str, 
    hg_addr_t hgi_addr,
    ABT_pool target_pool)
{
    region_content_t* region;
    char* memory;
    hg_addr_t src_addr = HG_ADDR_NULL;
    hg_return_t hret;
    hg_bulk_t bulk_handle = HG_BULK_NULL;
    int ret = 0;
    struct xfer_args x_args = {0};
    size_t i;

    /* find memory address for target object */
    region = pmemobj_direct(pmoid);
    if(!region)
        return(BAKE_ERR_UNKNOWN_REGION);

#ifdef USE_SIZECHECK_HEADERS
    if(region_offset + bulk_size > region->size)
        return(BAKE_ERR_OUT_OF_BOUNDS);
#endif

    memory = region->data + region_offset;

    /* resolve addr, could be addr of rpc sender (normal case) or a third
     * party (proxy write)
     */
    if(remote_addr_str && strlen(remote_addr_str))
    {
        /* a proxy address was provided to pull write data from */
        hret = margo_addr_lookup(mid, remote_addr_str, &src_addr);
        if(hret != HG_SUCCESS)
            return(BAKE_ERR_MERCURY);
    }
    else
    {
        /* no proxy write, use the source of this request */
        hret = margo_addr_dup(mid, hgi_addr, &src_addr);
        if(hret != HG_SUCCESS)
            return(BAKE_ERR_MERCURY);
    }

    if(global_bake_provider_conf.pipeline_enable == 0)
    {
        /* normal path; no pipeline or intermediate buffers */

        /* create bulk handle for local side of transfer */
        hret = margo_bulk_create(mid, 1, (void**)(&memory), &bulk_size,
                HG_BULK_WRITE_ONLY, &bulk_handle);
        if(hret != HG_SUCCESS)
        {
            ret = BAKE_ERR_MERCURY;
            goto finish;
        }

        hret = margo_bulk_transfer(mid, HG_BULK_PULL, src_addr, remote_bulk,
                remote_bulk_offset, bulk_handle, 0, bulk_size);
        if(hret != HG_SUCCESS)
        {
            ret = BAKE_ERR_MERCURY;
            goto finish;
        }

    } else { 
        
        /* pipelining mode, with intermediate buffers */

        x_args.mid = mid;
        x_args.remote_addr = src_addr;
        x_args.remote_bulk = remote_bulk;
        x_args.remote_offset = remote_bulk_offset;
        x_args.bulk_size = bulk_size;
        x_args.local_ptr = memory;
        x_args.bytes_issued = 0;
        x_args.bytes_retired = 0;
        x_args.poolset = svr_ctx->poolset;
        margo_bulk_poolset_get_max(svr_ctx->poolset, &x_args.poolset_max_size);
        x_args.ret = 0;
        ABT_mutex_create(&x_args.mutex);
        ABT_eventual_create(0, &x_args.eventual);

        for(i=0; i<bulk_size; i+= x_args.poolset_max_size)
            x_args.ults_active++;

        /* issue one ult per pipeline chunk */
        for(i=0; i<bulk_size; i+= x_args.poolset_max_size)
        {
            /* note: setting output tid to NULL to ignore; we will let
             * threads clean up themselves, with the last one setting an
             * eventual to signal completion.
             */
            ABT_thread_create(target_pool, xfer_ult, &x_args, ABT_THREAD_ATTR_NULL, NULL);
        }

        ABT_eventual_wait(x_args.eventual, NULL);
        ABT_eventual_free(&x_args.eventual);

        /* consolidated error code (0 if all successful, otherwise first
         * non-zero error code)
         */
        ret = x_args.ret;
    }

finish:
    margo_addr_free(mid, src_addr);
    margo_bulk_free(bulk_handle);

    return(ret);
}

    /* service a remote RPC that writes to a BAKE region */
static void bake_write_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","transfer","respond");
    bake_write_out_t out;
    bake_write_in_t in;
    hg_return_t hret;
    ABT_pool handler_pool;
    char* buffer = NULL;
    const struct hg_info *hgi;
    margo_instance_id mid;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    handler_pool = margo_hg_handle_get_handler_pool(handle);
    hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }
    
    /* read-lock the provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    prid = (pmemobj_region_id_t*)in.rid.data;

    TIMERS_END_STEP(0);

    out.ret = write_transfer_data(mid, svr_ctx, prid->oid, in.region_offset, in.bulk_handle, in.bulk_offset,
        in.bulk_size, in.remote_addr_str, hgi->addr, handler_pool);
    TIMERS_END_STEP(1);

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(2);
    TIMERS_FINALIZE();
    free(buffer);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_write_ult)

    /* service a remote RPC that writes to a BAKE region in eager mode */
static void bake_eager_write_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","memcpy","respond");
    bake_eager_write_out_t out;
    bake_eager_write_in_t in;
    in.buffer = NULL;
    in.size = 0;
    hg_return_t hret;
    char* buffer = NULL;
    pmemobj_region_id_t* prid = NULL;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, info->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

#ifdef USE_SIZECHECK_HEADERS
    if(in.size + in.region_offset > region->size) {
        out.ret = BAKE_ERR_OUT_OF_BOUNDS;
        goto finish;
    }
#endif

    TIMERS_END_STEP(0);

    buffer = region->data + in.region_offset;

    memcpy(buffer, in.buffer, in.size);

    TIMERS_END_STEP(1);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(2);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_write_ult)

    /* service a remote RPC that persists to a BAKE region */
static void bake_persist_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","persist","respond");
    bake_persist_out_t out;
    bake_persist_in_t in;
    hg_return_t hret;
    char* buffer = NULL;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, info->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }
    buffer = region->data;

    TIMERS_END_STEP(0);

    /* TODO: should this have an abt shim in case it blocks? */
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
    pmemobj_persist(pmem_pool, buffer + in.offset, in.size);

    TIMERS_END_STEP(1);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(2);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_persist_ult)

static void bake_create_write_persist_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","alloc","transfer","persist","respond");
    bake_create_write_persist_out_t out;
    bake_create_write_persist_in_t in;
    hg_return_t hret;
    ABT_pool handler_pool;
    char* buffer = NULL;
    const struct hg_info *hgi = NULL;
    margo_instance_id mid;
    int ret;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    mid = margo_hg_handle_get_instance(handle);
    handler_pool = margo_hg_handle_get_handler_pool(handle);
    assert(mid);
    hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }
    
    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    /* find the pmem pool */
    bake_pmem_entry_t* entry = find_pmem_entry(svr_ctx, in.bti);
    if(entry == NULL) {
        out.ret = BAKE_ERR_UNKNOWN_TARGET;
        goto finish;
    }

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = in.bulk_size + sizeof(uint64_t);
#else
    size_t content_size = in.bulk_size;
#endif

    TIMERS_END_STEP(0);

    prid = (pmemobj_region_id_t*)out.rid.data;

    ret = pmemobj_alloc(entry->pmem_pool, &prid->oid,
            content_size, 0, NULL, NULL);
    if(ret != 0)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

    TIMERS_END_STEP(1);

    out.ret = write_transfer_data(mid, svr_ctx, prid->oid, 0, in.bulk_handle, in.bulk_offset,
        in.bulk_size, in.remote_addr_str, hgi->addr, handler_pool);

    TIMERS_END_STEP(2);

    if(out.ret == BAKE_SUCCESS)
    {
        /* find memory address for target object */
        region_content_t* region = pmemobj_direct(prid->oid);
        if(!region)
        {
            out.ret = BAKE_ERR_PMEM;
            goto finish;
        }
#ifdef USE_SIZECHECK_HEADERS
        region->size = in.bulk_size;
#endif

        pmemobj_persist(entry->pmem_pool, region, content_size);
    }

    TIMERS_END_STEP(3);

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(4);
    TIMERS_FINALIZE();
    free(buffer);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)

static void bake_eager_create_write_persist_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","alloc","memcpy","persist","respond");
    bake_eager_create_write_persist_out_t out;
    bake_eager_create_write_persist_in_t in;
    in.buffer = NULL;
    in.size = 0;
    char* buffer = NULL;
    const struct hg_info *hgi = NULL;
    margo_instance_id mid;
    hg_return_t hret;
    int ret;

    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    /* find the pmem pool */
    bake_pmem_entry_t* entry = find_pmem_entry(svr_ctx, in.bti);
    if(entry == NULL) {
        out.ret = BAKE_ERR_UNKNOWN_TARGET;
        goto finish;
    }

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = in.size + sizeof(uint64_t);
#else
    size_t content_size = in.size;
#endif
    prid = (pmemobj_region_id_t*)out.rid.data;

    TIMERS_END_STEP(0);

    ret = pmemobj_alloc(entry->pmem_pool, &prid->oid,
            content_size, 0, NULL, NULL);
    if(ret != 0)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

    TIMERS_END_STEP(1);

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }
#ifdef USE_SIZECHECK_HEADERS
    region->size = in.size;
#endif
    buffer = region->data;

    memcpy(buffer, in.buffer, in.size);

    TIMERS_END_STEP(2);

    /* TODO: should this have an abt shim in case it blocks? */
    pmemobj_persist(entry->pmem_pool, region, content_size);

    TIMERS_END_STEP(3);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(4);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_create_write_persist_ult)

/* service a remote RPC that retrieves the size of a BAKE region */
static void bake_get_size_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","respond");
    bake_get_size_out_t out;
    bake_get_size_in_t in;
    hg_return_t hret;
#ifdef USE_SIZECHECK_HEADERS
    pmemobj_region_id_t* prid;
#endif
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

#ifdef USE_SIZECHECK_HEADERS
    prid = (pmemobj_region_id_t*)in.rid.data;
    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }
    out.size = region->size;
    out.ret = BAKE_SUCCESS;
#else
    out.ret = BAKE_ERR_OP_UNSUPPORTED;
#endif

    TIMERS_END_STEP(0);
finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(1);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_get_size_ult)

    /* Get the raw pointer of a region */
static void bake_get_data_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","respond");
    bake_get_data_out_t out;
    bake_get_data_in_t in;
    hg_return_t hret;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;
    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    out.ptr = (uint64_t)(region->data);
    out.ret = BAKE_SUCCESS;

    TIMERS_END_STEP(0);

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(1);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
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
    TIMERS_INITIALIZE("start","bulk_create","bulk_xfer","respond");
    bake_read_out_t out;
    bake_read_in_t in;
    in.remote_addr_str = NULL;
    hg_return_t hret;
    hg_addr_t src_addr = HG_ADDR_NULL;
    char* buffer = NULL;
    hg_bulk_t bulk_handle = HG_BULK_NULL;
    const struct hg_info *hgi;
    margo_instance_id mid;
    pmemobj_region_id_t* prid;
    hg_size_t size_to_read;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = 
        margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;
    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    size_to_read = in.bulk_size;

#ifdef USE_SIZECHECK_HEADERS
    if(in.region_offset > region->size)
    {
        out.ret = BAKE_ERR_OUT_OF_BOUNDS;
        goto finish;
    }
    if(in.region_offset + in.bulk_size > region->size) {
        size_to_read = region->size - in.region_offset;
    } else {
        size_to_read = in.bulk_size;
    }
#endif

    buffer = region->data + in.region_offset;

    TIMERS_END_STEP(0);

    /* create bulk handle for local side of transfer */
    hret = margo_bulk_create(mid, 1, (void**)(&buffer), &size_to_read,
            HG_BULK_READ_ONLY, &bulk_handle);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    TIMERS_END_STEP(1);

    if(in.remote_addr_str && strlen(in.remote_addr_str))
    {
        /* a proxy address was provided to push read data to */
        hret = margo_addr_lookup(mid, in.remote_addr_str, &src_addr);
        if(hret != HG_SUCCESS)
        {
            out.ret = BAKE_ERR_MERCURY;
            goto finish;
        }
    }
    else
    {
        /* no proxy write, use the source of this request */
        hret = margo_addr_dup(mid, hgi->addr, &src_addr);
        if(hret != HG_SUCCESS)
        {
            out.ret = BAKE_ERR_MERCURY;
            goto finish;
        }
    }
    hret = margo_bulk_transfer(mid, HG_BULK_PUSH, src_addr, in.bulk_handle,
            in.bulk_offset, bulk_handle, 0, size_to_read);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    TIMERS_END_STEP(2);

    out.ret = BAKE_SUCCESS;
    out.size = size_to_read;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(3);
    TIMERS_FINALIZE();
    margo_addr_free(mid, src_addr);
    margo_bulk_free(bulk_handle);
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_read_ult)

    /* service a remote RPC that reads from a BAKE region and eagerly sends
     * response */
static void bake_eager_read_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","respond");
    bake_eager_read_out_t out;
    out.buffer = NULL;
    out.size = 0;
    bake_eager_read_in_t in;
    hg_return_t hret;
    char* buffer = NULL;
    hg_size_t size_to_read;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = 
        margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    size_to_read = in.size;

#ifdef USE_SIZECHECK_HEADERS
    if(in.region_offset > region->size)
    {
        out.ret = BAKE_ERR_OUT_OF_BOUNDS;
        goto finish;
    }
    if(in.region_offset + in.size > region->size) {
        size_to_read = region->size - in.region_offset;
    }
#endif

    buffer = region->data + in.region_offset;

    out.ret = BAKE_SUCCESS;
    out.buffer = buffer;
    out.size = size_to_read;

    TIMERS_END_STEP(0);

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(1);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_read_ult)

    /* service a remote RPC that probes for a BAKE target id */
static void bake_probe_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","respond");
    bake_probe_out_t out;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = 
        margo_registered_data(mid, hgi->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        margo_respond(handle, &out);
        margo_destroy(handle);
        return;
    }

    uint64_t targets_count;
    bake_provider_count_storage_targets(svr_ctx, &targets_count);
    bake_target_id_t targets[targets_count];
    bake_provider_list_storage_targets(svr_ctx, targets);

    out.ret = BAKE_SUCCESS;
    out.targets = targets;
    out.num_targets = targets_count;

    TIMERS_END_STEP(0);

    margo_respond(handle, &out);

    TIMERS_END_STEP(1);
    TIMERS_FINALIZE();

    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_probe_ult)

static void bake_remove_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","remove","respond");
    bake_remove_in_t in;
    bake_remove_out_t out;
    hg_return_t hret;
    pmemobj_region_id_t* prid;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, info->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    TIMERS_END_STEP(0);

    pmemobj_free(&prid->oid);

    TIMERS_END_STEP(1);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(2);
    TIMERS_FINALIZE();
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_remove_ult)

static void bake_migrate_region_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","bulk_create","forward","remove","respond");
    bake_migrate_region_in_t in;
    in.dest_addr = NULL;
    bake_migrate_region_out_t out;
    hg_return_t hret;
    pmemobj_region_id_t* prid;
    hg_addr_t dest_addr = HG_ADDR_NULL;
    ABT_rwlock lock = ABT_RWLOCK_NULL;

    memset(&out, 0, sizeof(out));

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* info = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, info->id);
    if(!svr_ctx) {
        out.ret = BAKE_ERR_UNKNOWN_PROVIDER;
        goto finish;
    }

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.source_rid.data;

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if(!region)
    {
        out.ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    /* get the size of the region */
    size_t region_size = in.region_size;
    char* region_data  = region->data;

#ifdef USE_SIZECHECK_HEADERS
    /* check region size */
    if(in.region_size != region->size) {
        out.ret = BAKE_ERR_INVALID_ARG;
        goto finish;
    }
#endif

    /* lookup the address of the destination provider */
    hret = margo_addr_lookup(mid, in.dest_addr, &dest_addr);
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    TIMERS_END_STEP(0);

    { /* in this block we issue a create_write_persist to the destination */
        hg_handle_t cwp_handle = HG_HANDLE_NULL;
        bake_create_write_persist_in_t cwp_in;
        bake_create_write_persist_out_t cwp_out;

        cwp_in.bti = in.dest_target_id;
        cwp_in.bulk_offset = 0;
        cwp_in.bulk_size = region_size;
        cwp_in.remote_addr_str = NULL;

        hret = margo_bulk_create(mid, 1, (void**)(&region_data), &region_size,
                HG_BULK_READ_ONLY, &cwp_in.bulk_handle);
        if(hret != HG_SUCCESS) {
            out.ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        TIMERS_END_STEP(1);

        hret = margo_create(mid, dest_addr,
                            svr_ctx->bake_create_write_persist_id, &cwp_handle);
        if(hret != HG_SUCCESS) {
            out.ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        hret = margo_provider_forward(in.dest_provider_id, cwp_handle, &cwp_in);
        if(hret != HG_SUCCESS)
        {
            out.ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        hret = margo_get_output(cwp_handle, &cwp_out);
        if(hret != HG_SUCCESS)
        {
            out.ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        if(cwp_out.ret != BAKE_SUCCESS)
        {
            out.ret = cwp_out.ret;
            goto finish_scope;
        }

        out.dest_rid = cwp_out.rid;
        out.ret = BAKE_SUCCESS;

        TIMERS_END_STEP(2);

finish_scope:
        margo_free_output(cwp_handle, &cwp_out);
        margo_bulk_free(cwp_in.bulk_handle);
        margo_destroy(cwp_handle);
    } /* end of create-write-persist block */

    if(out.ret != BAKE_SUCCESS)
        goto finish;

    if(in.remove_src) {
        pmemobj_free(&prid->oid);
    }

    TIMERS_END_STEP(3);
    
finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    TIMERS_END_STEP(4);
    TIMERS_FINALIZE();
    margo_addr_free(mid, dest_addr);
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_migrate_region_ult)

static void bake_migrate_target_ult(hg_handle_t handle)
{
    TIMERS_INITIALIZE("start","remi_init","remi_migrate","remove","respond");
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
    bake_provider_t svr_ctx = margo_registered_data(mid, info->id);
    if(!svr_ctx) {
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
    lock = svr_ctx->lock;
    ABT_rwlock_wrlock(lock);
    bake_pmem_entry_t* entry = find_pmem_entry(svr_ctx, in.target_id);
    if(!entry) {
        out.ret = BAKE_ERR_UNKNOWN_TARGET;
        goto finish;
    }

    TIMERS_END_STEP(0);

    /* lookup the address of the destination REMI provider */
    hret = margo_addr_lookup(mid, in.dest_remi_addr, &dest_addr);
    if(hret != HG_SUCCESS) {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    /* use the REMI client to create a REMI provider handle */
    ret = remi_provider_handle_create(svr_ctx->remi_client, 
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

    TIMERS_END_STEP(1);

    /* issue the migration */
    int status = 0;
    ret = remi_fileset_migrate(remi_ph, local_fileset, in.dest_root, in.remove_src, REMI_USE_ABTIO, &status);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }

    TIMERS_END_STEP(2);

    /* remove the target from the list of managed targets */
    bake_provider_remove_storage_target(svr_ctx, in.target_id);

    TIMERS_END_STEP(3);

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
    TIMERS_END_STEP(4);
    TIMERS_FINALIZE();
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_migrate_target_ult)

static void bake_server_finalize_cb(void *data)
{
    bake_server_context_t *provider = (bake_server_context_t *)data;
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
    bake_server_context_t* provider;
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
    args.provider = (bake_server_context_t *)uarg;
    size_t root_size = 1024;
    remi_fileset_get_root(fileset, args.root, &root_size);
    remi_fileset_foreach_file(fileset, migration_fileset_cb, &args);
    return 0;
}

#endif

static void xfer_ult(void *_args)
{
    struct xfer_args *args = _args;
    hg_bulk_t local_bulk = HG_BULK_NULL;
    size_t this_size;
    char *this_local_ptr;
    void *local_bulk_ptr;
    size_t this_remote_offset;
    size_t tmp_buf_size;
    hg_uint32_t tmp_count;
    int turn_out_the_lights = 0;
    int ret;

    /* Set up a loop here.  It may or may not get used; just depends on
     * timing of whether this ULT gets through a cycle before other ULTs
     * start running.  We don't care which ULT does the next chunk.
     */
    ABT_mutex_lock(args->mutex);
    while(args->bytes_issued < args->bulk_size && !args->ret)
    {
        /* calculate what work we will do in this cycle */
        if((args->bulk_size - args->bytes_issued) > args->poolset_max_size)
            this_size = args->poolset_max_size;
        else
            this_size = args->bulk_size - args->bytes_issued;
        this_local_ptr = args->local_ptr + args->bytes_issued;
        this_remote_offset = args->remote_offset + args->bytes_issued;

        /* update state */
        args->bytes_issued += this_size;

        /* drop mutex while we work on our local piece */
        ABT_mutex_unlock(args->mutex);

        /* get buffer */
        ret = margo_bulk_poolset_get(args->poolset, this_size, &local_bulk);
        if(ret != 0 && args->ret == 0)
        {
            args->ret = ret;
            goto finished;
        }

        /* find pointer of memory in buffer */
        ret = margo_bulk_access(local_bulk, 0,
            this_size, HG_BULK_READWRITE, 1,
            &local_bulk_ptr, &tmp_buf_size, &tmp_count);
        /* shouldn't ever fail in this use case */
        assert(ret == 0);

        /* do the rdma transfer */
        ret = margo_bulk_transfer(args->mid, HG_BULK_PULL,
            args->remote_addr, args->remote_bulk,
            this_remote_offset, local_bulk, 0, this_size);
        if(ret != 0 && args->ret == 0)
        {
            args->ret = ret;
            goto finished;
        }

        /* copy to real destination */
        memcpy(this_local_ptr, local_bulk_ptr, this_size);

        /* let go of bulk handle */
        margo_bulk_poolset_release(args->poolset, local_bulk);
        local_bulk = HG_BULK_NULL;

        ABT_mutex_lock(args->mutex);
        args->bytes_retired += this_size;
    }
    
    /* TODO: think about this.  It is tempting to signal caller before all
     * of the threads have cleaned up, but if we do that then we need to get
     * args struct off of the caller's stack and free it here, otherwise
     * will go out of scope.
     */
#if 0
    if(args->bytes_retired == args->bulk_size && !args->done)
    {
        /* this is the first ULT to notice completion; signal caller */
        args->done = 1;
        ABT_eventual_set(args->eventual, NULL, 0);
    }
#endif

    ABT_mutex_unlock(args->mutex);

finished:
    if(local_bulk != HG_BULK_NULL)
        margo_bulk_poolset_release(args->poolset, local_bulk);
    ABT_mutex_lock(args->mutex);
    args->ults_active--;
    if(!args->ults_active)
        turn_out_the_lights = 1;
    ABT_mutex_unlock(args->mutex);

    /* last ULT to exit needs to clean up some resources, one else around
     * to touch mutex at this point
     */
    if(turn_out_the_lights)
    {
        ABT_mutex_free(&args->mutex);
        ABT_eventual_set(args->eventual, NULL, 0);
    }

    return;
}

static int set_conf_cb_pipeline_enabled(bake_provider_t provider, 
    const char* value)
{
    int ret;
    hg_return_t hret;

    static int first_call = 1;

    if(first_call) {

        ret = sscanf(value, "%u", &global_bake_provider_conf.pipeline_enable);
        if(ret != 1)
            return BAKE_ERR_INVALID_ARG;

        if(global_bake_provider_conf.pipeline_enable) {
            hret = margo_bulk_poolset_create(
                    provider->mid, 
                    global_bake_provider_conf.pipeline_npools,
                    global_bake_provider_conf.pipeline_nbuffers_per_pool,
                    global_bake_provider_conf.pipeline_first_buffer_size,
                    global_bake_provider_conf.pipeline_multiplier,
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

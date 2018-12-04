/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <assert.h>
#include <libpmemobj.h>
#include <remi/remi-client.h>
#include <remi/remi-server.h>
#include "bake-server.h"
#include "uthash.h"
#include "bake-rpc.h"

DECLARE_MARGO_RPC_HANDLER(bake_shutdown_ult)
DECLARE_MARGO_RPC_HANDLER(bake_create_ult)
DECLARE_MARGO_RPC_HANDLER(bake_write_ult)
DECLARE_MARGO_RPC_HANDLER(bake_eager_write_ult)
DECLARE_MARGO_RPC_HANDLER(bake_persist_ult)
DECLARE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)
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
    ABT_rwlock lock; // write-locked during migration, read-locked by all other
    // operations. There should be something better to avoid locking everything
    // but we are going with that for simplicity for now.
    uint64_t num_targets;
    bake_pmem_entry_t* targets;
    hg_id_t bake_create_write_persist_id;
    remi_client_t remi_client;
    remi_provider_t remi_provider;
} bake_server_context_t;

static void bake_server_finalize_cb(void *data);

static int bake_target_post_migration_callback(remi_fileset_t fileset, void* provider);

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
    /* check if a REMI provider exists with the same provider id */
    {
        int flag;
        // TODO pass an actual ABT-IO instance
        remi_provider_registered(mid, provider_id, &flag, NULL, NULL, NULL);
        if(flag) {
            fprintf(stderr, "bake_provider_register(): a REMI provider with the same (%d) already exists\n", provider_id);
            return BAKE_ERR_REMI;
        }
    }

    /* allocate the resulting structure */    
    tmp_svr_ctx = calloc(1,sizeof(*tmp_svr_ctx));
    if(!tmp_svr_ctx)
        return BAKE_ERR_ALLOCATION;

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
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_write_rpc",
            bake_write_in_t, bake_write_out_t, 
            bake_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_write_rpc",
            bake_eager_write_in_t, bake_eager_write_out_t, 
            bake_eager_write_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_eager_read_rpc",
            bake_eager_read_in_t, bake_eager_read_out_t, 
            bake_eager_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_persist_rpc",
            bake_persist_in_t, bake_persist_out_t, 
            bake_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_create_write_persist_rpc",
            bake_create_write_persist_in_t, bake_create_write_persist_out_t,
            bake_create_write_persist_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_size_rpc",
            bake_get_size_in_t, bake_get_size_out_t, 
            bake_get_size_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_get_data_rpc",
            bake_get_data_in_t, bake_get_data_out_t, 
            bake_get_data_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_read_rpc",
            bake_read_in_t, bake_read_out_t, 
            bake_read_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_probe_rpc",
            bake_probe_in_t, bake_probe_out_t, bake_probe_ult, 
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_noop_rpc",
            void, void, bake_noop_ult, provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_remove_rpc",
            bake_remove_in_t, bake_remove_out_t, bake_remove_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_region_rpc",
            bake_migrate_region_in_t, bake_migrate_region_out_t, bake_migrate_region_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);
    rpc_id = MARGO_REGISTER_PROVIDER(mid, "bake_migrate_target_rpc",
            bake_migrate_target_in_t, bake_migrate_target_out_t, bake_migrate_target_ult,
            provider_id, abt_pool);
    margo_register_data(mid, rpc_id, (void*)tmp_svr_ctx, NULL);

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

    /* register a REMI client */
    // TODO actually use an ABT-IO instance
    ret = remi_client_init(mid, ABT_IO_INSTANCE_NULL, &(tmp_svr_ctx->remi_client));
    if(ret != REMI_SUCCESS) {
        // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
        return BAKE_ERR_REMI;
    }

    /* register a REMI provider */
    // TODO actually use an ABT-IO instance
    ret = remi_provider_register(mid, ABT_IO_INSTANCE_NULL, provider_id, abt_pool, &(tmp_svr_ctx->remi_provider));
    if(ret != REMI_SUCCESS) {
        // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
        return BAKE_ERR_REMI;
    }
    ret = remi_provider_register_migration_class(tmp_svr_ctx->remi_provider,
            "bake", NULL,
            bake_target_post_migration_callback, NULL, tmp_svr_ctx);
    if(ret != REMI_SUCCESS) {
        // XXX unregister RPCs, cleanup tmp_svr_ctx before returning
        return BAKE_ERR_REMI;
    }

    /* install the bake server finalize callback */
    margo_push_finalize_callback(mid, &bake_server_finalize_cb, tmp_svr_ctx);

    if(provider != BAKE_PROVIDER_IGNORE)
        *provider = tmp_svr_ctx;

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

    int ret = pmemobj_alloc(entry->pmem_pool, &prid->oid,
            content_size, 0, NULL, NULL);
    if(ret != 0) {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

    region_content_t* region = (region_content_t*)pmemobj_direct(prid->oid);
    if(!region) {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }
#ifdef USE_SIZECHECK_HEADERS
    region->size = in.region_size;
#endif
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
#ifdef USE_SIZECHECK_HEADERS
    pmemobj_persist(pmem_pool, region, sizeof(region->size));
#endif

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_ult)

    /* service a remote RPC that writes to a BAKE region */
static void bake_write_ult(hg_handle_t handle)
{
    bake_write_out_t out;
    bake_write_in_t in;
    in.bulk_handle = HG_BULK_NULL;
    hg_return_t hret;
    hg_addr_t src_addr = HG_ADDR_NULL;
    char* buffer;
    hg_bulk_t bulk_handle = HG_BULK_NULL;
    const struct hg_info *hgi;
    margo_instance_id mid;
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
    /* read-lock the provider */
    lock = svr_ctx->lock;
    ABT_rwlock_rdlock(lock);

    hret = margo_get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);

    if(!region)
    {
        out.ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

#ifdef USE_SIZECHECK_HEADERS
    if(in.region_offset + in.bulk_size > region->size) {
        out.ret = BAKE_ERR_OUT_OF_BOUNDS;
        goto finish;
    }
#endif

    buffer = region->data + in.region_offset;

    /* create bulk handle for local side of transfer */
    hret = margo_bulk_create(mid, 1, (void**)(&buffer), &in.bulk_size,
            HG_BULK_WRITE_ONLY, &bulk_handle);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    if(in.remote_addr_str)
    {
        /* a proxy address was provided to pull write data from */
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
        src_addr = hgi->addr;
    }

    hret = margo_bulk_transfer(mid, HG_BULK_PULL, src_addr, in.bulk_handle,
            in.bulk_offset, bulk_handle, 0, in.bulk_size);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    if(in.remote_addr_str)
        margo_addr_free(mid, src_addr);
    margo_bulk_free(bulk_handle);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_write_ult)

    /* service a remote RPC that writes to a BAKE region in eager mode */
static void bake_eager_write_ult(hg_handle_t handle)
{
    bake_eager_write_out_t out;
    bake_eager_write_in_t in;
    in.buffer = NULL;
    in.size = 0;
    hg_return_t hret;
    char* buffer = NULL;
    hg_bulk_t bulk_handle = HG_BULK_NULL;
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

    buffer = region->data + in.region_offset;

    memcpy(buffer, in.buffer, in.size);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_eager_write_ult)

    /* service a remote RPC that persists to a BAKE region */
static void bake_persist_ult(hg_handle_t handle)
{
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

    /* TODO: should this have an abt shim in case it blocks? */
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
    pmemobj_persist(pmem_pool, buffer + in.offset, in.size);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_persist_ult)

static void bake_create_write_persist_ult(hg_handle_t handle)
{
    bake_create_write_persist_out_t out;
    bake_create_write_persist_in_t in;
    in.bulk_handle = HG_BULK_NULL;
    in.remote_addr_str = NULL;
    hg_addr_t src_addr = HG_ADDR_NULL;
    char* buffer = NULL;
    hg_bulk_t bulk_handle = HG_BULK_NULL;
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

    size_t content_size = in.bulk_size + sizeof(uint64_t);
    prid = (pmemobj_region_id_t*)out.rid.data;

    ret = pmemobj_alloc(entry->pmem_pool, &prid->oid,
            content_size, 0, NULL, NULL);
    if(ret != 0)
    {
        out.ret = BAKE_ERR_PMEM;
        goto finish;
    }

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
    buffer = region->data;

    /* create bulk handle for local side of transfer */
    hret = margo_bulk_create(mid, 1, (void**)(&buffer), &in.bulk_size,
            HG_BULK_WRITE_ONLY, &bulk_handle);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    if(in.remote_addr_str)
    {
        /* a proxy address was provided to pull write data from */
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
        src_addr = hgi->addr;
    }
    hret = margo_bulk_transfer(mid, HG_BULK_PULL, src_addr, in.bulk_handle,
            in.bulk_offset, bulk_handle, 0, in.bulk_size);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    /* TODO: should this have an abt shim in case it blocks? */
    pmemobj_persist(entry->pmem_pool, region, content_size);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    if(in.remote_addr_str) {
        margo_addr_free(mid, src_addr);
    }
    margo_bulk_free(bulk_handle);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)

    /* service a remote RPC that retrieves the size of a BAKE region */
static void bake_get_size_ult(hg_handle_t handle)
{
    bake_get_size_out_t out;
    bake_get_size_in_t in;
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

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_get_size_ult)

    /* Get the raw pointer of a region */
static void bake_get_data_ult(hg_handle_t handle)
{
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

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_get_data_ult)

    /* service a remote RPC for a BAKE no-op */
static void bake_noop_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    assert(mid);
    const struct hg_info* hgi = margo_get_info(handle);
    bake_provider_t svr_ctx = margo_registered_data(mid, hgi->id);

    margo_respond(handle, NULL);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_noop_ult)

/* TODO consolidate with write handler; read and write are nearly identical */
/* service a remote RPC that reads from a BAKE region */
static void bake_read_ult(hg_handle_t handle)
{
    bake_read_out_t out;
    bake_read_in_t in;
    in.bulk_handle = HG_BULK_NULL;
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

    /* create bulk handle for local side of transfer */
    hret = margo_bulk_create(mid, 1, (void**)(&buffer), &size_to_read,
            HG_BULK_READ_ONLY, &bulk_handle);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    if(in.remote_addr_str)
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
        src_addr = hgi->addr;
    }
    hret = margo_bulk_transfer(mid, HG_BULK_PUSH, src_addr, in.bulk_handle,
            in.bulk_offset, bulk_handle, 0, size_to_read);
    if(hret != HG_SUCCESS)
    {
        out.ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    out.ret = BAKE_SUCCESS;
    out.size = size_to_read;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    if(in.remote_addr_str)
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

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
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

    margo_respond(handle, &out);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_probe_ult)

static void bake_remove_ult(hg_handle_t handle)
{
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
    pmemobj_free(&prid->oid);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(bake_remove_ult)

static void bake_migrate_region_ult(hg_handle_t handle)
{
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
    
finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    margo_respond(handle, &out);
    margo_addr_free(mid, dest_addr);
    margo_free_input(handle, &in);
    margo_destroy(handle);
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
    remi_provider_handle_t remi_ph = REMI_PROVIDER_HANDLE_NULL;
    remi_fileset_t local_fileset = REMI_FILESET_NULL;
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

    /* lock provider */
    lock = svr_ctx->lock;
    ABT_rwlock_wrlock(lock);
    bake_pmem_entry_t* entry = find_pmem_entry(svr_ctx, in.target_id);
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
    /* issue the migration */
    int status = 0;
    ret = remi_fileset_migrate(remi_ph, local_fileset, in.dest_root, in.remove_src, REMI_USE_ABTIO, &status);
    if(ret != REMI_SUCCESS) {
        out.ret = BAKE_ERR_REMI;
        goto finish;
    }
    /* remove the target from the list of managed targets */
    bake_provider_remove_storage_target(svr_ctx, in.target_id);

    out.ret = BAKE_SUCCESS;

finish:
    if(lock != ABT_RWLOCK_NULL)
        ABT_rwlock_unlock(lock);
    remi_fileset_free(local_fileset);
    remi_provider_handle_release(remi_ph);
    margo_addr_free(mid, dest_addr);
    margo_free_input(handle, &in);
    margo_respond(handle, &out);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_migrate_target_ult)

static void bake_server_finalize_cb(void *data)
{
    bake_server_context_t *svr_ctx = (bake_server_context_t *)data;
    assert(svr_ctx);

    bake_provider_remove_all_storage_targets(svr_ctx);

    remi_client_finalize(svr_ctx->remi_client);

    ABT_rwlock_free(&(svr_ctx->lock));

    free(svr_ctx);

    return;
}

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

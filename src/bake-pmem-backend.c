#include <assert.h>
#include <json-c/json.h>
#include "bake-config.h"
#include "bake.h"
#include "bake-rpc.h"
#include "bake-server.h"
#include "bake-provider.h"
#include "bake-backend.h"
#include "bake-macros.h"

/* definition of BAKE root data structure (just a uuid for now) */
typedef struct {
    bake_target_id_t pool_id;
} bake_root_t;

/* definition of internal BAKE region_id_t identifier for libpmemobj back end */
typedef struct {
    PMEMoid oid;
} pmemobj_region_id_t;

typedef struct {
#ifdef USE_SIZECHECK_HEADERS
    uint64_t size;
#endif
    char data[1];
} region_content_t;

typedef struct {
    bake_provider_t provider;
    PMEMobjpool*    pmem_pool;
    bake_root_t*    pmem_root;
    char*           root;
    char*           filename;
} bake_pmem_entry_t;

typedef struct xfer_args {
    margo_instance_id mid;           // margo instance
    hg_addr_t         remote_addr;   // remote address
    hg_bulk_t         remote_bulk;   // remote bulk handle for transfers
    size_t            remote_offset; // remote offset at which to take the data
    size_t            bulk_size;
    char*             local_ptr;
    size_t            bytes_issued;
    size_t            bytes_retired;
    margo_bulk_poolset_t poolset;
    size_t               poolset_max_size;
    int32_t              ret; // return value of the xfer_ult function
    int                  done;
    int                  ults_active;
    ABT_mutex            mutex;
    ABT_eventual         eventual;
} xfer_args;

static void xfer_ult(void* _args);

static int bake_pmem_makepool(const char* pool_name, size_t pool_size)
{
    PMEMobjpool* pool;
    PMEMoid      root_oid;
    bake_root_t* root;

    pool = pmemobj_create(pool_name, NULL, pool_size, 0644);
    if (!pool) {
        fprintf(stderr, "pmemobj_create: %s\n", pmemobj_errormsg());
        return BAKE_ERR_PMEM;
    }

    /* find root */
    root_oid = pmemobj_root(pool, sizeof(bake_root_t));
    root     = pmemobj_direct(root_oid);

    /* store the target id for this bake pool at the root */
    uuid_generate(root->pool_id.id);
    pmemobj_persist(pool, root, sizeof(bake_root_t));

    pmemobj_close(pool);

    return BAKE_SUCCESS;
}
////////////////////////////////////////////////////////////////////////////////////////////
static int bake_pmem_backend_initialize(bake_provider_t    provider,
                                        const char*        path,
                                        bake_target_id_t*  target,
                                        backend_context_t* context)
{
    bake_pmem_entry_t* new_context
        = (bake_pmem_entry_t*)calloc(1, sizeof(*new_context));
    char* tmp                             = strrchr(path, '/');
    new_context->provider                 = provider;
    new_context->filename                 = strdup(tmp);
    ptrdiff_t d                           = tmp - path;
    new_context->root                     = strndup(path, d);
    struct json_object* pmem_backend_json = NULL;
    struct json_object* target_array      = NULL;

    CONFIG_HAS_OR_CREATE_OBJECT(provider->json_cfg, "pmem_backend",
                                "pmem_backend", pmem_backend_json);
    CONFIG_HAS_OR_CREATE_ARRAY(pmem_backend_json, "targets",
                               "pmem_backend.targets", target_array);

    /* TODO: populate tuning parameters specific to this backend */

    new_context->pmem_pool = pmemobj_open(path, NULL);
    if (!(new_context->pmem_pool)) {
        BAKE_ERROR(provider->mid, "pmemobj_open: %s", pmemobj_errormsg());
        free(new_context->filename);
        free(new_context->root);
        free(new_context);
        return BAKE_ERR_NOENT;
    }

    /* check to make sure the root is properly set */
    PMEMoid root_oid
        = pmemobj_root(new_context->pmem_pool, sizeof(bake_root_t));
    new_context->pmem_root = pmemobj_direct(root_oid);
    bake_target_id_t tid   = new_context->pmem_root->pool_id;

    if (uuid_is_null(tid.id)) {
        BAKE_ERROR(provider->mid, "pool %s is not properly initialized", path);
        pmemobj_close(new_context->pmem_pool);
        free(new_context->filename);
        free(new_context->root);
        free(new_context);
        return BAKE_ERR_UNKNOWN_TARGET;
    }

    /* target successfully added; inject it into the json in array of
     * targets for this backend
     */
    json_object_array_add(target_array, json_object_new_string(path));

    *target  = tid;
    *context = new_context;
    return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////
static int bake_pmem_backend_finalize(backend_context_t context)
{
    bake_pmem_entry_t* entry = (bake_pmem_entry_t*)context;
    pmemobj_close(entry->pmem_pool);
    free(entry->filename);
    free(entry->root);
    free(entry);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int
bake_pmem_create(backend_context_t context, size_t size, bake_region_id_t* rid)
{
    bake_pmem_entry_t* entry = (bake_pmem_entry_t*)context;
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = size + sizeof(uint64_t);
#else
    size_t content_size = size;
#endif

    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid->data;

    int ret = pmemobj_alloc(entry->pmem_pool, &prid->oid, content_size, 0, NULL,
                            NULL);
    if (ret != 0) return BAKE_ERR_PMEM;

#ifdef USE_SIZECHECK_HEADERS
    region_content_t* region = (region_content_t*)pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_PMEM;

    region->size           = size;
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
    pmemobj_persist(pmem_pool, region, sizeof(region->size));
#endif

    return BAKE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int write_transfer_data(margo_instance_id mid,
                               bake_provider_t   provider,
                               PMEMoid           pmoid,
                               uint64_t          region_offset,
                               hg_bulk_t         remote_bulk,
                               uint64_t          remote_bulk_offset,
                               uint64_t          bulk_size,
                               hg_addr_t         src_addr,
                               ABT_pool          target_pool)
{
    region_content_t* region;
    char*             memory;
    hg_return_t       hret;
    hg_bulk_t         bulk_handle = HG_BULK_NULL;
    int               ret         = 0;
    struct xfer_args  x_args      = {0};
    size_t            i;

    /* find memory address for target object */
    region = pmemobj_direct(pmoid);
    if (!region) return (BAKE_ERR_UNKNOWN_REGION);

#ifdef USE_SIZECHECK_HEADERS
    if (region_offset + bulk_size > region->size)
        return (BAKE_ERR_OUT_OF_BOUNDS);
#endif

    memory = region->data + region_offset;

    /* resolve addr, could be addr of rpc sender (normal case) or a third
     * party (proxy write)
     */
    if (!json_object_get_boolean(
            json_object_object_get(provider->json_cfg, "pipeline_enable"))) {
        /* normal path; no pipeline or intermediate buffers */

        /* create bulk handle for local side of transfer */
        hret = margo_bulk_create(mid, 1, (void**)(&memory), &bulk_size,
                                 HG_BULK_WRITE_ONLY, &bulk_handle);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish;
        }
        hret = margo_bulk_transfer(mid, HG_BULK_PULL, src_addr, remote_bulk,
                                   remote_bulk_offset, bulk_handle, 0,
                                   bulk_size);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish;
        }

    } else {

        /* pipelining mode, with intermediate buffers */

        x_args.mid           = mid;
        x_args.remote_addr   = src_addr;
        x_args.remote_bulk   = remote_bulk;
        x_args.remote_offset = remote_bulk_offset;
        x_args.bulk_size     = bulk_size;
        x_args.local_ptr     = memory;
        x_args.bytes_issued  = 0;
        x_args.bytes_retired = 0;
        x_args.poolset       = provider->poolset;
        margo_bulk_poolset_get_max(provider->poolset, &x_args.poolset_max_size);
        x_args.ret = 0;
        ABT_mutex_create(&x_args.mutex);
        ABT_eventual_create(0, &x_args.eventual);

        for (i = 0; i < bulk_size; i += x_args.poolset_max_size)
            x_args.ults_active++;

        /* issue one ult per pipeline chunk */
        for (i = 0; i < bulk_size; i += x_args.poolset_max_size) {
            /* note: setting output tid to NULL to ignore; we will let
             * threads clean up themselves, with the last one setting an
             * eventual to signal completion.
             */
            ABT_thread_create(target_pool, xfer_ult, &x_args,
                              ABT_THREAD_ATTR_NULL, NULL);
        }

        ABT_eventual_wait(x_args.eventual, NULL);
        ABT_eventual_free(&x_args.eventual);

        /* consolidated error code (0 if all successful, otherwise first
         * non-zero error code)
         */
        ret = x_args.ret;
    }

finish:
    margo_bulk_free(bulk_handle);

    return (ret);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_pmem_write_raw(backend_context_t context,
                               bake_region_id_t  rid,
                               size_t            offset,
                               size_t            size,
                               const void*       data)
{
    char*                ptr  = NULL;
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_PMEM;

#ifdef USE_SIZECHECK_HEADERS
    if (size + offset > region->size) return BAKE_ERR_OUT_OF_BOUNDS;
#endif

    ptr = region->data + offset;
    memcpy(ptr, data, size);

    return BAKE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_pmem_write_bulk(backend_context_t context,
                                bake_region_id_t  rid,
                                size_t            region_offset,
                                size_t            size,
                                hg_bulk_t         bulk,
                                hg_addr_t         source,
                                size_t            bulk_offset)
{
    bake_pmem_entry_t*   entry = (bake_pmem_entry_t*)context;
    pmemobj_region_id_t* prid;

    prid = (pmemobj_region_id_t*)rid.data;

    ABT_pool handler_pool = entry->provider->handler_pool;

    int ret = write_transfer_data(entry->provider->mid, entry->provider,
                                  prid->oid, region_offset, bulk, bulk_offset,
                                  size, source, handler_pool);
    return ret;
}

static int bake_pmem_read_raw(backend_context_t context,
                              bake_region_id_t  rid,
                              size_t            offset,
                              size_t            size,
                              void**            data,
                              uint64_t*         data_size,
                              free_fn*          free_data)
{
    *free_data = NULL;
    *data      = NULL;
    *data_size = 0;

    char*                buffer = NULL;
    hg_size_t            size_to_read;
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_UNKNOWN_REGION;

    size_to_read = size;

#ifdef USE_SIZECHECK_HEADERS
    if (offset > region->size) return BAKE_ERR_OUT_OF_BOUNDS;
    if (offset + size > region->size) { size_to_read = region->size - offset; }
#endif

    buffer = region->data + offset;

    *data      = buffer;
    *data_size = size_to_read;

    return BAKE_SUCCESS;
}

static int bake_pmem_read_bulk(backend_context_t context,
                               bake_region_id_t  rid,
                               size_t            region_offset,
                               size_t            size,
                               hg_bulk_t         bulk,
                               hg_addr_t         source,
                               size_t            bulk_offset,
                               size_t*           bytes_read)
{
    int                  ret         = BAKE_SUCCESS;
    bake_pmem_entry_t*   entry       = (bake_pmem_entry_t*)context;
    char*                buffer      = NULL;
    hg_bulk_t            bulk_handle = HG_BULK_NULL;
    pmemobj_region_id_t* prid;
    hg_size_t            size_to_read;
    *bytes_read = 0;

    prid = (pmemobj_region_id_t*)rid.data;

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) {
        ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    size_to_read = size;

#ifdef USE_SIZECHECK_HEADERS
    if (region_offset > region->size) {
        ret = BAKE_ERR_OUT_OF_BOUNDS;
        goto finish;
    }
    if (region_offset + size > region->size) {
        size_to_read = region->size - region_offset;
    } else {
        size_to_read = size;
    }
#endif

    buffer = region->data + region_offset;

    /* create bulk handle for local side of transfer */
    hg_return_t hret
        = margo_bulk_create(entry->provider->mid, 1, (void**)(&buffer),
                            &size_to_read, HG_BULK_READ_ONLY, &bulk_handle);
    if (hret != HG_SUCCESS) {
        ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    hret = margo_bulk_transfer(entry->provider->mid, HG_BULK_PUSH, source, bulk,
                               bulk_offset, bulk_handle, 0, size_to_read);

    if (hret != HG_SUCCESS) {
        ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    *bytes_read = size_to_read;

finish:
    margo_bulk_free(bulk_handle);
    return ret;
}

static int bake_pmem_persist(backend_context_t context,
                             bake_region_id_t  rid,
                             size_t            offset,
                             size_t            size)
{
    char*                ptr  = NULL;
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_PMEM;
    ptr = region->data;

    /* TODO: should this have an abt shim in case it blocks? */
    PMEMobjpool* pmem_pool = pmemobj_pool_by_oid(prid->oid);
    pmemobj_persist(pmem_pool, ptr + offset, size);

    return BAKE_SUCCESS;
}

static int bake_pmem_create_write_persist_raw(backend_context_t context,
                                              const void*       data,
                                              size_t            size,
                                              bake_region_id_t* rid)
{
    bake_pmem_entry_t*   entry  = (bake_pmem_entry_t*)context;
    char*                buffer = NULL;
    pmemobj_region_id_t* prid;

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = size + sizeof(uint64_t);
#else
    size_t content_size = size;
#endif
    prid = (pmemobj_region_id_t*)rid->data;

    int ret = pmemobj_alloc(entry->pmem_pool, &prid->oid, content_size, 0, NULL,
                            NULL);
    if (ret != 0) return BAKE_ERR_PMEM;

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_PMEM;
#ifdef USE_SIZECHECK_HEADERS
    region->size = size;
#endif
    buffer = region->data;

    memcpy(buffer, data, size);

    /* TODO: should this have an abt shim in case it blocks? */
    pmemobj_persist(entry->pmem_pool, region, content_size);

    return BAKE_SUCCESS;
}

static int bake_pmem_create_write_persist_bulk(backend_context_t context,
                                               hg_bulk_t         bulk,
                                               hg_addr_t         source,
                                               size_t            bulk_offset,
                                               size_t            size,
                                               bake_region_id_t* rid)
{
    bake_pmem_entry_t*   entry = (bake_pmem_entry_t*)context;
    pmemobj_region_id_t* prid;
    ABT_pool             handler_pool = entry->provider->handler_pool;

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

#ifdef USE_SIZECHECK_HEADERS
    size_t content_size = size + sizeof(uint64_t);
#else
    size_t content_size = size;
#endif

    prid = (pmemobj_region_id_t*)rid->data;

    int ret = pmemobj_alloc(entry->pmem_pool, &prid->oid, content_size, 0, NULL,
                            NULL);
    if (ret != 0) return BAKE_ERR_PMEM;

    ret = write_transfer_data(entry->provider->mid, entry->provider, prid->oid,
                              0, bulk, bulk_offset, size, source, handler_pool);

    if (ret == BAKE_SUCCESS) {
        /* find memory address for target object */
        region_content_t* region = pmemobj_direct(prid->oid);
        if (!region) return BAKE_ERR_PMEM;
#ifdef USE_SIZECHECK_HEADERS
        region->size = size;
#endif
        pmemobj_persist(entry->pmem_pool, region, content_size);
    }

    return BAKE_SUCCESS;
}

static int bake_pmem_get_region_size(backend_context_t context,
                                     bake_region_id_t  rid,
                                     size_t*           size)
{
#ifdef USE_SIZECHECK_HEADERS
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;
    /* lock provider */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_PMEM;
    *size = region->size;
    return BAKE_SUCCESS;
#else
    return BAKE_ERR_OP_UNSUPPORTED;
#endif
}

static int bake_pmem_get_region_data(backend_context_t context,
                                     bake_region_id_t  rid,
                                     void**            data)
{
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;
    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) return BAKE_ERR_UNKNOWN_REGION;

    *data = region->data;
    return BAKE_SUCCESS;
}

static int bake_pmem_remove(backend_context_t context, bake_region_id_t rid)
{
    pmemobj_region_id_t* prid = (pmemobj_region_id_t*)rid.data;
    pmemobj_free(&prid->oid);
    return BAKE_SUCCESS;
}

static int bake_pmem_migrate_region(backend_context_t context,
                                    bake_region_id_t  source_rid,
                                    size_t            region_size,
                                    int               remove_source,
                                    const char*       dest_addr_str,
                                    uint16_t          dest_provider_id,
                                    bake_target_id_t  dest_target_id,
                                    bake_region_id_t* dest_rid)
{
    bake_pmem_entry_t*   entry = (bake_pmem_entry_t*)context;
    pmemobj_region_id_t* prid;
    hg_addr_t            dest_addr = HG_ADDR_NULL;
    int                  ret       = BAKE_SUCCESS;

    prid = (pmemobj_region_id_t*)source_rid.data;

    /* find memory address for target object */
    region_content_t* region = pmemobj_direct(prid->oid);
    if (!region) {
        ret = BAKE_ERR_UNKNOWN_REGION;
        goto finish;
    }

    /* get the size of the region */
    char* region_data = region->data;

#ifdef USE_SIZECHECK_HEADERS
    /* check region size */
    if (region_size != region->size) {
        ret = BAKE_ERR_INVALID_ARG;
        goto finish;
    }
#endif

    /* lookup the address of the destination provider */
    hg_return_t hret
        = margo_addr_lookup(entry->provider->mid, dest_addr_str, &dest_addr);
    if (hret != HG_SUCCESS) {
        ret = BAKE_ERR_MERCURY;
        goto finish;
    }

    { /* in this block we issue a create_write_persist to the destination */
        hg_handle_t                     cwp_handle = HG_HANDLE_NULL;
        bake_create_write_persist_in_t  cwp_in;
        bake_create_write_persist_out_t cwp_out;

        cwp_in.bti             = dest_target_id;
        cwp_in.bulk_offset     = 0;
        cwp_in.bulk_size       = region_size;
        cwp_in.remote_addr_str = NULL;

        hret = margo_bulk_create(entry->provider->mid, 1,
                                 (void**)(&region_data), &region_size,
                                 HG_BULK_READ_ONLY, &cwp_in.bulk_handle);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        hret = margo_create(entry->provider->mid, dest_addr,
                            entry->provider->bake_create_write_persist_id,
                            &cwp_handle);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        hret = margo_provider_forward(dest_provider_id, cwp_handle, &cwp_in);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        hret = margo_get_output(cwp_handle, &cwp_out);
        if (hret != HG_SUCCESS) {
            ret = BAKE_ERR_MERCURY;
            goto finish_scope;
        }

        if (cwp_out.ret != BAKE_SUCCESS) {
            ret = cwp_out.ret;
            goto finish_scope;
        }

        *dest_rid = cwp_out.rid;
        ret       = BAKE_SUCCESS;

finish_scope:
        margo_free_output(cwp_handle, &cwp_out);
        margo_bulk_free(cwp_in.bulk_handle);
        margo_destroy(cwp_handle);
    } /* end of create-write-persist block */

    if (ret != BAKE_SUCCESS) goto finish;

    if (remove_source) { pmemobj_free(&prid->oid); }

finish:
    margo_addr_free(entry->provider->mid, dest_addr);
    return ret;
}

#ifdef USE_REMI
static int bake_pmem_create_fileset(backend_context_t context,
                                    remi_fileset_t*   fileset)
{
    bake_pmem_entry_t* entry = (bake_pmem_entry_t*)context;
    int                ret;
    /* create a fileset */
    ret = remi_fileset_create("bake", entry->root, fileset);
    if (ret != REMI_SUCCESS) {
        ret = BAKE_ERR_REMI;
        goto error;
    }

    /* fill the fileset */
    ret = remi_fileset_register_file(*fileset, entry->filename);
    if (ret != REMI_SUCCESS) {
        ret = BAKE_ERR_REMI;
        goto error;
    }

finish:
    return ret;
error:
    remi_fileset_free(*fileset);
    *fileset = NULL;
    goto finish;
}
#endif

bake_backend g_bake_pmem_backend = {
    .name                       = "pmem",
    ._initialize                = bake_pmem_backend_initialize,
    ._finalize                  = bake_pmem_backend_finalize,
    ._create                    = bake_pmem_create,
    ._write_raw                 = bake_pmem_write_raw,
    ._write_bulk                = bake_pmem_write_bulk,
    ._read_raw                  = bake_pmem_read_raw,
    ._read_bulk                 = bake_pmem_read_bulk,
    ._persist                   = bake_pmem_persist,
    ._create_write_persist_raw  = bake_pmem_create_write_persist_raw,
    ._create_write_persist_bulk = bake_pmem_create_write_persist_bulk,
    ._get_region_size           = bake_pmem_get_region_size,
    ._get_region_data           = bake_pmem_get_region_data,
    ._remove                    = bake_pmem_remove,
    ._migrate_region            = bake_pmem_migrate_region,
    ._create_raw_target         = bake_pmem_makepool,
#ifdef USE_REMI
    ._create_fileset = bake_pmem_create_fileset,
#endif
};

static void xfer_ult(void* _args)
{
    struct xfer_args* args       = _args;
    hg_bulk_t         local_bulk = HG_BULK_NULL;
    size_t            this_size;
    char*             this_local_ptr;
    void*             local_bulk_ptr;
    size_t            this_remote_offset;
    size_t            tmp_buf_size;
    hg_uint32_t       tmp_count;
    int               turn_out_the_lights = 0;
    int               ret;

    /* Set up a loop here.  It may or may not get used; just depends on
     * timing of whether this ULT gets through a cycle before other ULTs
     * start running.  We don't care which ULT does the next chunk.
     */
    ABT_mutex_lock(args->mutex);
    while (args->bytes_issued < args->bulk_size && !args->ret) {
        /* calculate what work we will do in this cycle */
        if ((args->bulk_size - args->bytes_issued) > args->poolset_max_size)
            this_size = args->poolset_max_size;
        else
            this_size = args->bulk_size - args->bytes_issued;
        this_local_ptr     = args->local_ptr + args->bytes_issued;
        this_remote_offset = args->remote_offset + args->bytes_issued;

        /* update state */
        args->bytes_issued += this_size;

        /* drop mutex while we work on our local piece */
        ABT_mutex_unlock(args->mutex);

        /* get buffer */
        ret = margo_bulk_poolset_get(args->poolset, this_size, &local_bulk);
        if (ret != 0 && args->ret == 0) {
            args->ret = ret;
            goto finished;
        }

        /* find pointer of memory in buffer */
        ret = margo_bulk_access(local_bulk, 0, this_size, HG_BULK_READWRITE, 1,
                                &local_bulk_ptr, &tmp_buf_size, &tmp_count);
        /* shouldn't ever fail in this use case */
        assert(ret == 0);

        /* do the rdma transfer */
        ret = margo_bulk_transfer(args->mid, HG_BULK_PULL, args->remote_addr,
                                  args->remote_bulk, this_remote_offset,
                                  local_bulk, 0, this_size);
        if (ret != 0 && args->ret == 0) {
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
    if (local_bulk != HG_BULK_NULL)
        margo_bulk_poolset_release(args->poolset, local_bulk);
    ABT_mutex_lock(args->mutex);
    args->ults_active--;
    if (!args->ults_active) turn_out_the_lights = 1;
    ABT_mutex_unlock(args->mutex);

    /* last ULT to exit needs to clean up some resources, one else around
     * to touch mutex at this point
     */
    if (turn_out_the_lights) {
        ABT_mutex_free(&args->mutex);
        ABT_eventual_set(args->eventual, NULL, 0);
    }

    return;
}

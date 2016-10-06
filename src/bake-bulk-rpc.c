/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <libpmemobj.h>

#include "bake-bulk-rpc.h"
#include "bake-pool.h"

/* TODO: this should not be global in the long run; server may provide access
 * to multiple targets
 */
extern PMEMobjpool *g_pmem_pool;
extern struct bake_bulk_root *g_bake_bulk_root;

/* definition of internal region_id_t identifier for libpmemobj back end */
typedef struct {
    PMEMoid oid;
    uint64_t size;
} pmemobj_region_id_t;

/* service a remote RPC that instructs the server daemon to shut down */
static void bake_bulk_shutdown_ult(hg_handle_t handle)
{
    hg_return_t hret;
    struct hg_info *hgi;
    margo_instance_id mid;

    // printf("Got RPC request to shutdown.\n");

    hgi = HG_Get_info(handle);
    assert(hgi);
    mid = margo_hg_class_to_instance(hgi->hg_class);

    hret = margo_respond(mid, handle, NULL);
    assert(hret == HG_SUCCESS);

    HG_Destroy(handle);

    /* NOTE: we assume that the server daemon is using
     * margo_wait_for_finalize() to suspend until this RPC executes, so there
     * is no need to send any extra signal to notify it.
     */
    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_shutdown_ult)

/* service a remote RPC that creates a bulk region */
static void bake_bulk_create_ult(hg_handle_t handle)
{
    bake_bulk_create_out_t out;
    bake_bulk_create_in_t in;
    hg_return_t hret;
    pmemobj_region_id_t* prid;

    /* TODO: this check needs to be somewhere else */
    assert(sizeof(pmemobj_region_id_t) <= BAKE_BULK_REGION_ID_DATA_SIZE);
    // printf("Got RPC request to create bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)out.rid.data;
    prid->size = in.region_size;
    out.ret = pmemobj_alloc(g_pmem_pool, &prid->oid, in.region_size, 0, NULL, NULL);

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_create_ult)

/* service a remote RPC that writes to a bulk region */
static void bake_bulk_write_ult(hg_handle_t handle)
{
    bake_bulk_write_out_t out;
    bake_bulk_write_in_t in;
    hg_return_t hret;
    char* buffer = NULL;
    hg_size_t size;
    hg_bulk_pool_set_t *ps = NULL;
    hg_bulk_au_t au;
    void *pool_bulk_buf = NULL;
    hg_size_t pool_bulk_size;
    hg_uint32_t pool_bulk_segments_found;
    struct hg_info *hgi;
    margo_instance_id mid;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to write bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hgi = HG_Get_info(handle);
    assert(hgi);
    mid = margo_hg_class_to_instance(hgi->hg_class);

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    buffer = pmemobj_direct(prid->oid);
    if(!buffer)
    {
        out.ret = -1;
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    size = in.region_size;

    ps = (poolset_wr == NULL) ? poolset_rw : poolset_wr;
    assert(ps != NULL);
    au = hg_bulk_pool_set_get_alloc(ps, size, (void**)&buffer);
    assert(au.bulk != HG_BULK_NULL);

    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr, in.bulk_handle,
        in.bulk_offset, au.bulk, 0, size);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        hg_bulk_pool_set_release_au(ps, au);

        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    if (au.from_pool) {
        pool_bulk_segments_found = 0;
        hret = HG_Bulk_access(au.bulk, 0, size, hg_bulk_pool_set_get_flag(ps),
                1, &pool_bulk_buf, &pool_bulk_size, &pool_bulk_segments_found);
        assert(hret == HG_SUCCESS && pool_bulk_buf != NULL);
        memcpy(buffer, pool_bulk_buf, size);
    }
    hg_bulk_pool_set_release_au(ps, au);

    out.ret = 0;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_write_ult)


/* service a remote RPC that writes to a bulk region in eager mode */
static void bake_bulk_eager_write_ult(hg_handle_t handle)
{
    bake_bulk_eager_write_out_t out;
    bake_bulk_eager_write_in_t in;
    hg_return_t hret;
    char* buffer;
    struct hg_info *hgi;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to write bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hgi = HG_Get_info(handle);
    assert(hgi);

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    buffer = pmemobj_direct(prid->oid);
    if(!buffer)
    {
        out.ret = -1;
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    memcpy(buffer, in.buffer, in.size);

    out.ret = 0;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_eager_write_ult)

/* service a remote RPC that persists to a bulk region */
static void bake_bulk_persist_ult(hg_handle_t handle)
{
    bake_bulk_persist_out_t out;
    bake_bulk_persist_in_t in;
    hg_return_t hret;
    char* buffer;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to persist bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    buffer = pmemobj_direct(prid->oid);
    if(!buffer)
    {
        out.ret = -1;
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    /* TODO: should this have an abt shim in case it blocks? */
    pmemobj_persist(g_pmem_pool, buffer, prid->size);

    out.ret = 0;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_persist_ult)

/* service a remote RPC that retrieves the size of a bulk region */
static void bake_bulk_get_size_ult(hg_handle_t handle)
{
    bake_bulk_get_size_out_t out;
    bake_bulk_get_size_in_t in;
    hg_return_t hret;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to get_size bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* kind of cheating here; the size is encoded in the RID */
    out.size = prid->size;
    out.ret = 0;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_get_size_ult)

/* service a remote RPC for a no-op */
static void bake_bulk_noop_ult(hg_handle_t handle)
{
    // printf("Got RPC request to noop bulk region.\n");

    HG_Respond(handle, NULL, NULL, NULL);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_noop_ult)

/* TODO consolidate with write handler; read and write are nearly identical */
/* service a remote RPC that reads to a bulk region */
static void bake_bulk_read_ult(hg_handle_t handle)
{
    bake_bulk_read_out_t out;
    bake_bulk_read_in_t in;
    hg_return_t hret;
    char* buffer;
    hg_size_t size;
    hg_bulk_pool_set_t *ps = NULL;
    hg_bulk_au_t au;
    void *pool_bulk_buf = NULL;
    hg_size_t pool_bulk_size;
    hg_uint32_t pool_bulk_segments_found;
    struct hg_info *hgi;
    margo_instance_id mid;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to read bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hgi = HG_Get_info(handle);
    assert(hgi);
    mid = margo_hg_class_to_instance(hgi->hg_class);

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    buffer = pmemobj_direct(prid->oid);
    if(!buffer)
    {
        out.ret = -1;
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    size = in.region_size;

    ps = (poolset_rd == NULL) ? poolset_rw : poolset_rd;
    assert(ps != NULL);
    au = hg_bulk_pool_set_get_alloc(ps, size, (void**)&buffer);
    assert(au.bulk != HG_BULK_NULL);

    if (au.from_pool) {
        hret = HG_Bulk_access(au.bulk, 0, size, HG_BULK_READ_ONLY, 1,
                &pool_bulk_buf, &pool_bulk_size, &pool_bulk_segments_found);
        assert(hret == HG_SUCCESS &&
                size <= pool_bulk_size &&
                pool_bulk_segments_found == 1);
        memcpy(pool_bulk_buf, buffer, size);
    }

    hret = margo_bulk_transfer(mid, HG_BULK_PUSH, hgi->addr, in.bulk_handle,
        in.bulk_offset, au.bulk, 0, size);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        hg_bulk_pool_set_release_au(ps, au);
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    hg_bulk_pool_set_release_au(ps, au);

    out.ret = 0;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_read_ult)


/* service a remote RPC that reads to a bulk region and eagerly sends
 * response */
static void bake_bulk_eager_read_ult(hg_handle_t handle)
{
    bake_bulk_eager_read_out_t out;
    bake_bulk_eager_read_in_t in;
    hg_return_t hret;
    char* buffer;
    struct hg_info *hgi;
    pmemobj_region_id_t* prid;

    // printf("Got RPC request to read bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    hgi = HG_Get_info(handle);
    assert(hgi);

    hret = HG_Get_input(handle, &in);
    if(hret != HG_SUCCESS)
    {
        out.ret = -1;
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    prid = (pmemobj_region_id_t*)in.rid.data;

    /* find memory address for target object */
    buffer = pmemobj_direct(prid->oid);
    if(!buffer)
    {
        out.ret = -1;
        HG_Free_input(handle, &in);
        HG_Respond(handle, NULL, NULL, &out);
        HG_Destroy(handle);
        return;
    }

    out.ret = 0;
    out.buffer = buffer;
    out.size = in.size;

    HG_Free_input(handle, &in);
    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_eager_read_ult)

/* service a remote RPC that probes for a target id */
static void bake_bulk_probe_ult(hg_handle_t handle)
{
    bake_bulk_probe_out_t out;

    // printf("Got RPC request to probe bulk region.\n");
    
    memset(&out, 0, sizeof(out));

    out.ret = 0;
    out.bti = g_bake_bulk_root->target_id;

    HG_Respond(handle, NULL, NULL, &out);
    HG_Destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(bake_bulk_probe_ult)



/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <assert.h>
#include <margo.h>
#include <bake-client.h>
#include "uthash.h"
#include "bake-rpc.h"

#define BAKE_DEFAULT_EAGER_LIMIT 2048

/* Refers to a single Margo initialization, for now this is shared by
 * all remote BAKE targets.  In the future we probably need to support
 * multiple in case we run atop more than one transport at a time.
 */
struct bake_client
{
    margo_instance_id mid;  

    hg_id_t bake_probe_id;
    hg_id_t bake_create_id;
    hg_id_t bake_eager_write_id;
    hg_id_t bake_eager_read_id;
    hg_id_t bake_write_id;
    hg_id_t bake_persist_id;
    hg_id_t bake_create_write_persist_id;
    hg_id_t bake_get_size_id;
    hg_id_t bake_get_data_id;
    hg_id_t bake_read_id;
    hg_id_t bake_noop_id;
    hg_id_t bake_remove_id;
    hg_id_t bake_migrate_region_id;
    hg_id_t bake_migrate_target_id;

    uint64_t num_provider_handles;
};

struct bake_provider_handle {
    struct bake_client* client;
    hg_addr_t           addr;
    uint16_t            provider_id;
    uint64_t            refcount;
    uint64_t            eager_limit;
};

static int bake_client_register(bake_client_t client, margo_instance_id mid)
{
    client->mid = mid;

    /* check if RPCs have already been registered */
    hg_bool_t flag;
    hg_id_t id;
    margo_registered_name(mid, "bake_probe_rpc", &id, &flag);

    if(flag == HG_TRUE) { /* RPCs already registered */

        margo_registered_name(mid, "bake_probe_rpc",                &client->bake_probe_id,                &flag);
        margo_registered_name(mid, "bake_create_rpc",               &client->bake_create_id,               &flag);
        margo_registered_name(mid, "bake_write_rpc",                &client->bake_write_id,                &flag);
        margo_registered_name(mid, "bake_eager_write_rpc",          &client->bake_eager_write_id,          &flag);
        margo_registered_name(mid, "bake_eager_read_rpc",           &client->bake_eager_read_id,           &flag);
        margo_registered_name(mid, "bake_persist_rpc",              &client->bake_persist_id,              &flag);
        margo_registered_name(mid, "bake_create_write_persist_rpc", &client->bake_create_write_persist_id, &flag);
        margo_registered_name(mid, "bake_get_size_rpc",             &client->bake_get_size_id,             &flag);
        margo_registered_name(mid, "bake_get_data_rpc",             &client->bake_get_data_id,             &flag);
        margo_registered_name(mid, "bake_read_rpc",                 &client->bake_read_id,                 &flag);
        margo_registered_name(mid, "bake_noop_rpc",                 &client->bake_noop_id,                 &flag);
        margo_registered_name(mid, "bake_remove_rpc",               &client->bake_remove_id,               &flag);
        margo_registered_name(mid, "bake_migrate_region_rpc",       &client->bake_migrate_region_id,       &flag);
        margo_registered_name(mid, "bake_migrate_target_rpc",       &client->bake_migrate_target_id,       &flag);

    } else { /* RPCs not already registered */

        client->bake_probe_id = 
            MARGO_REGISTER(mid, "bake_probe_rpc",
                    bake_probe_in_t, bake_probe_out_t, NULL);
        client->bake_create_id = 
            MARGO_REGISTER(mid, "bake_create_rpc",
                    bake_create_in_t, bake_create_out_t, NULL);
        client->bake_write_id = 
            MARGO_REGISTER(mid, "bake_write_rpc",
                    bake_write_in_t, bake_write_out_t, NULL);
        client->bake_eager_write_id = 
            MARGO_REGISTER(mid, "bake_eager_write_rpc",
                    bake_eager_write_in_t, bake_eager_write_out_t, NULL);
        client->bake_eager_read_id = 
            MARGO_REGISTER(mid, "bake_eager_read_rpc",
                    bake_eager_read_in_t, bake_eager_read_out_t, NULL);
        client->bake_persist_id = 
            MARGO_REGISTER(mid, "bake_persist_rpc",
                    bake_persist_in_t, bake_persist_out_t, NULL);
        client->bake_create_write_persist_id =
            MARGO_REGISTER(mid, "bake_create_write_persist_rpc",
                    bake_create_write_persist_in_t, bake_create_write_persist_out_t, NULL);
        client->bake_get_size_id = 
            MARGO_REGISTER(mid, "bake_get_size_rpc",
                    bake_get_size_in_t, bake_get_size_out_t, NULL);
        client->bake_get_data_id =
            MARGO_REGISTER(mid, "bake_get_data_rpc",
                    bake_get_data_in_t, bake_get_data_out_t, NULL);
        client->bake_read_id = 
            MARGO_REGISTER(mid, "bake_read_rpc",
                    bake_read_in_t, bake_read_out_t, NULL);
        client->bake_noop_id = 
            MARGO_REGISTER(mid, "bake_noop_rpc",
                    void, void, NULL);
        client->bake_remove_id =
            MARGO_REGISTER(mid, "bake_remove_rpc",
                    bake_remove_in_t, bake_remove_out_t, NULL);
        client->bake_migrate_region_id =
            MARGO_REGISTER(mid, "bake_migrate_region_rpc",
                    bake_migrate_region_in_t, bake_migrate_region_out_t, NULL);
        client->bake_migrate_target_id =
            MARGO_REGISTER(mid, "bake_migrate_target_rpc",
                    bake_migrate_target_in_t, bake_migrate_target_out_t, NULL);
    }

    return BAKE_SUCCESS;
}

int bake_client_init(margo_instance_id mid, bake_client_t* client)
{
    bake_client_t c = (bake_client_t)calloc(1, sizeof(*c));
    if(!c) return BAKE_ERR_ALLOCATION;

    c->num_provider_handles = 0;

    int ret = bake_client_register(c, mid);
    if(ret != BAKE_SUCCESS) return ret;

    *client = c;
    return BAKE_SUCCESS;
}

int bake_client_finalize(bake_client_t client)
{
    if(client->num_provider_handles != 0) {
        fprintf(stderr, 
                "[BAKE] Warning: %d provider handles not released before bake_client_finalize was called\n",
                client->num_provider_handles);
    }
    free(client);
    return BAKE_SUCCESS;
}

int bake_probe(
    bake_provider_handle_t provider,
    uint64_t max_targets,
    bake_target_id_t *bti,
    uint64_t* num_targets)
{
    hg_return_t hret;
    int ret;
    bake_probe_in_t in;
    bake_probe_out_t out;
    hg_handle_t handle;

    if(bti == NULL) max_targets = 0;
    in.max_targets = max_targets;

    /* create handle */
    hret = margo_create(
                provider->client->mid, 
                provider->addr, 
                provider->client->bake_probe_id, 
                &handle);

    if(hret != HG_SUCCESS) return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS) {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS) {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;

    if(ret == HG_SUCCESS) {
        if(max_targets == 0) {
            *num_targets = out.num_targets;
        } else {
            uint64_t s = out.num_targets > max_targets ? max_targets : out.num_targets;
            if(s > 0) {
                memcpy(bti, out.targets, sizeof(*bti)*s);
            }
            *num_targets = s;
        }
    }

    margo_free_output(handle, &out);
    margo_destroy(handle);

    return ret;
}

int bake_provider_handle_create(
        bake_client_t client,
        hg_addr_t addr,
        uint16_t provider_id,
        bake_provider_handle_t* handle)
{
    if(client == BAKE_CLIENT_NULL) return BAKE_ERR_INVALID_ARG;

    bake_provider_handle_t provider = 
        (bake_provider_handle_t)calloc(1, sizeof(*provider));

    if(!provider) return BAKE_ERR_ALLOCATION;

    hg_return_t ret = margo_addr_dup(client->mid, addr, &(provider->addr));
    if(ret != HG_SUCCESS) {
        free(provider);
        return BAKE_ERR_MERCURY;
    }
    
    provider->client   = client;
    provider->provider_id = provider_id;
    provider->refcount = 1;
    provider->eager_limit = BAKE_DEFAULT_EAGER_LIMIT;

    client->num_provider_handles += 1;

    *handle = provider;
    return BAKE_SUCCESS;
}

int bake_provider_handle_get_eager_limit(bake_provider_handle_t handle, uint64_t* limit)
{
    if(handle == BAKE_PROVIDER_HANDLE_NULL) return BAKE_ERR_INVALID_ARG;
    *limit = handle->eager_limit;
    return BAKE_SUCCESS;
}

int bake_provider_handle_set_eager_limit(bake_provider_handle_t handle, uint64_t limit)
{
    if(handle == BAKE_PROVIDER_HANDLE_NULL) return BAKE_ERR_INVALID_ARG;
    handle->eager_limit = limit;
    return BAKE_SUCCESS;
}

int bake_provider_handle_ref_incr(bake_provider_handle_t handle)
{
    if(handle == BAKE_PROVIDER_HANDLE_NULL) return BAKE_ERR_INVALID_ARG;
    handle->refcount += 1;
    return BAKE_SUCCESS;
}

int bake_provider_handle_release(bake_provider_handle_t handle)
{
    if(handle == BAKE_PROVIDER_HANDLE_NULL) return BAKE_ERR_INVALID_ARG;
    handle->refcount -= 1;
    if(handle->refcount == 0) {
        margo_addr_free(handle->client->mid, handle->addr);
        handle->client->num_provider_handles -= 1;
        free(handle);
    }
    return BAKE_SUCCESS;
}
  
int bake_shutdown_service(bake_client_t client, hg_addr_t addr)
{
    return margo_shutdown_remote_instance(client->mid, addr);
}

static int bake_eager_write(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    void const *buf,
    uint64_t buf_size)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_eager_write_in_t in;
    bake_eager_write_out_t out;
    int ret;

    in.rid = rid;
    in.region_offset = region_offset;
    in.size = buf_size;
    in.buffer = (char*)buf;
  
    hret = margo_create(provider->client->mid, provider->addr, 
                provider->client->bake_eager_write_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }
    
    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);

    return ret;
}

int bake_write(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    void const *buf,
    uint64_t buf_size)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_write_in_t in;
    bake_write_out_t out;
    int ret;

    if(buf_size <= provider->eager_limit)
        return(bake_eager_write(provider, rid, region_offset, buf, buf_size));

    in.rid = rid;
    in.region_offset = region_offset;
    in.bulk_offset = 0;
    in.bulk_size = buf_size;
    in.remote_addr_str = NULL; /* set remote_addr to NULL to disable proxy write */

    hret = margo_bulk_create(provider->client->mid, 1, (void**)(&buf), &buf_size, 
        HG_BULK_READ_ONLY, &in.bulk_handle);
    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;
   
    hret = margo_create(provider->client->mid, provider->addr, 
        provider->client->bake_write_id, &handle);

    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }
    
    ret = out.ret;

    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    return ret;
}

int bake_proxy_write(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    hg_bulk_t remote_bulk,
    uint64_t remote_offset,
    const char* remote_addr,
    uint64_t size)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_write_in_t in;
    bake_write_out_t out;
    int ret;

    in.rid = rid;
    in.region_offset = region_offset;
    in.bulk_handle = remote_bulk;
    in.bulk_offset = remote_offset;
    in.bulk_size = size;
    in.remote_addr_str = (char*)remote_addr;

    hret = margo_create(provider->client->mid, provider->addr,
        provider->client->bake_write_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {   
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }
    
    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {   
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_create(
    bake_provider_handle_t provider,
    bake_target_id_t bti,
    uint64_t region_size,
    bake_region_id_t *rid)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_create_in_t in;
    bake_create_out_t out;
    int ret = 0;

    in.bti = bti;
    in.region_size = region_size;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_create_id, &handle);

    if(hret != HG_SUCCESS) {
        return BAKE_ERR_MERCURY;
    }

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    if(ret == BAKE_SUCCESS)
        *rid = out.rid;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}


int bake_persist(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    size_t offset,
    size_t size)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_persist_in_t in;
    bake_persist_out_t out;
    int ret;

    in.rid = rid;
    in.offset = offset;
    in.size = size;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_persist_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_create_write_persist(
    bake_provider_handle_t provider,
    bake_target_id_t bti,
    void const *buf,
    uint64_t buf_size,
    bake_region_id_t *rid)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_create_write_persist_in_t in;
    bake_create_write_persist_out_t out;
    int ret;

    /* XXX eager path? */

    in.bti = bti;
    in.bulk_offset = 0;
    in.bulk_size = buf_size;
    in.remote_addr_str = NULL; /* set remote_addr to NULL to disable proxy write */

    hret = margo_bulk_create(provider->client->mid, 1, (void**)(&buf), &buf_size,
        HG_BULK_READ_ONLY, &in.bulk_handle);
    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_create_write_persist_id, &handle);

    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    if(ret == 0)
        *rid = out.rid;

    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    return(ret);
}

int bake_create_write_persist_proxy(
    bake_provider_handle_t provider,
    bake_target_id_t bti,
    hg_bulk_t remote_bulk,
    uint64_t remote_offset,
    const char* remote_addr,
    uint64_t size,
    bake_region_id_t *rid)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_create_write_persist_in_t in;
    bake_create_write_persist_out_t out;
    int ret;

    in.bti = bti;
    in.bulk_handle = remote_bulk;
    in.bulk_offset = remote_offset;
    in.bulk_size = size;
    in.remote_addr_str = (char*)remote_addr;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_create_write_persist_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    if(ret == BAKE_SUCCESS)
        *rid = out.rid;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_get_size(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t *region_size)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_get_size_in_t in;
    bake_get_size_out_t out;
    int ret;

    in.rid = rid;

    hret = margo_create(provider->client->mid, provider->addr,
        provider->client->bake_get_size_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    *region_size = out.size;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_get_data(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    void** ptr)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_get_data_in_t in;
    bake_get_data_out_t out;
    int ret;

    // make sure the target provider is on the same address space
    hg_addr_t self_addr;
    if(HG_SUCCESS != margo_addr_self(provider->client->mid, &self_addr)) return -1;
    hg_addr_t trgt_addr = provider->addr;
    hg_size_t addr_size = 128;
    char self_addr_str[128];
    char trgt_addr_str[128];

    if(HG_SUCCESS != margo_addr_to_string(provider->client->mid, self_addr_str, &addr_size, self_addr)) {
        margo_addr_free(provider->client->mid, self_addr);
        return BAKE_ERR_MERCURY;
    }
    if(HG_SUCCESS != margo_addr_to_string(provider->client->mid, trgt_addr_str, &addr_size, trgt_addr)) {
        margo_addr_free(provider->client->mid, self_addr);
        return BAKE_ERR_MERCURY;
    }
    if(strcmp(self_addr_str, trgt_addr_str) != 0) {
        margo_addr_free(provider->client->mid, self_addr);
        return BAKE_ERR_MERCURY;
    }
    margo_addr_free(provider->client->mid, self_addr);

    in.rid = rid;

    hret = margo_create(provider->client->mid, provider->addr,
        provider->client->bake_get_data_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    *ptr = (void*)out.ptr;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_migrate_region(
        bake_provider_handle_t source,
        bake_region_id_t source_rid,
        size_t region_size,
        int remove_source,
        const char* dest_addr,
        uint16_t dest_provider_id,
        bake_target_id_t dest_target_id,
        bake_region_id_t* dest_rid)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_migrate_region_in_t in;
    bake_migrate_region_out_t out;
    int ret;

    in.source_rid       = source_rid;
    in.region_size      = region_size;
    in.remove_src       = remove_source;
    in.dest_addr        = dest_addr;
    in.dest_provider_id = dest_provider_id;
    in.dest_target_id   = dest_target_id;

    hret = margo_create(source->client->mid, source->addr,
            source->client->bake_migrate_region_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(source->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    if(ret == BAKE_SUCCESS)
        *dest_rid = out.dest_rid;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_migrate_target(
        bake_provider_handle_t source,
        bake_target_id_t src_target_id,
        int remove_source,
        const char* dest_addr,
        uint16_t dest_provider_id,
        const char* dest_root)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_migrate_target_in_t in;
    bake_migrate_target_out_t out;
    int ret;

    in.target_id        = src_target_id;
    in.remove_src       = remove_source;
    in.dest_remi_addr   = dest_addr;
    in.dest_remi_provider_id = dest_provider_id;
    in.dest_root        = dest_root;

    hret = margo_create(source->client->mid, source->addr,
            source->client->bake_migrate_target_id, &handle);
    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(source->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_noop(bake_provider_handle_t provider)
{
    hg_return_t hret;
    hg_handle_t handle;

    hret = margo_create(provider->client->mid, provider->addr,
        provider->client->bake_noop_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, NULL);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    margo_destroy(handle);
    return BAKE_SUCCESS;
}

static int bake_eager_read(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    void *buf,
    uint64_t buf_size,
    uint64_t* bytes_read)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_eager_read_in_t in;
    bake_eager_read_out_t out;
    int ret;

    in.rid = rid;
    in.region_offset = region_offset;
    in.size = buf_size;

    hret = margo_create(provider->client->mid, provider->addr,
        provider->client->bake_eager_read_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;
  
    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }
    
    ret = out.ret;
    if(ret == 0)
        memcpy(buf, out.buffer, out.size);
    *bytes_read = out.size;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_read(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    void *buf,
    uint64_t buf_size,
    uint64_t* bytes_read)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_read_in_t in;
    bake_read_out_t out;
    int ret;

    if(buf_size <= provider->eager_limit)
        return(bake_eager_read(provider, rid, region_offset, buf, buf_size, bytes_read));

    in.rid = rid;
    in.region_offset = region_offset;
    in.bulk_offset = 0;
    in.bulk_size = buf_size;
    in.remote_addr_str = NULL; /* set remote_addr to NULL to disable proxy read */

    hret = margo_bulk_create(provider->client->mid, 1, (void**)(&buf), &buf_size, 
        HG_BULK_WRITE_ONLY, &in.bulk_handle);
    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;
   
    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_read_id, &handle);

    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_bulk_free(in.bulk_handle);
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }
    
    ret = out.ret;
    *bytes_read = out.size;

    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    return(ret);
}

int bake_proxy_read(
    bake_provider_handle_t provider,
    bake_region_id_t rid,
    uint64_t region_offset,
    hg_bulk_t remote_bulk,
    uint64_t remote_offset,
    const char* remote_addr,
    uint64_t size,
    uint64_t* bytes_read)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_read_in_t in;
    bake_read_out_t out;
    int ret;

    in.rid = rid;
    in.region_offset = region_offset;
    in.bulk_handle = remote_bulk;
    in.bulk_offset = remote_offset;
    in.bulk_size = size;
    in.remote_addr_str = (char*)remote_addr;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_read_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;
    *bytes_read = out.size;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

int bake_remove(
    bake_provider_handle_t provider,
    bake_region_id_t rid)
{
    hg_return_t hret;
    hg_handle_t handle;
    bake_remove_in_t in;
    bake_remove_out_t out;
    int ret;

    in.rid = rid;

    hret = margo_create(provider->client->mid, provider->addr,
            provider->client->bake_remove_id, &handle);

    if(hret != HG_SUCCESS)
        return BAKE_ERR_MERCURY;

    hret = margo_provider_forward(provider->provider_id, handle, &in);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return BAKE_ERR_MERCURY;
    }

    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);
    return(ret);
}

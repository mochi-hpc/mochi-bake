/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_BULK_RPC
#define __BAKE_BULK_RPC

#include <margo.h>
#include <bake-bulk.h>

/* encoders for bake-specific types */
static inline hg_return_t hg_proc_bake_target_id_t(hg_proc_t proc, bake_target_id_t *bti);
static inline hg_return_t hg_proc_bake_bulk_region_id_t(hg_proc_t proc, bake_bulk_region_id_t *rid);
/* shutdown */
DECLARE_MARGO_RPC_HANDLER(bake_bulk_shutdown_ult)

/* bulk create */
MERCURY_GEN_PROC(bake_bulk_create_in_t,
    ((bake_target_id_t)(bti))\
    ((uint64_t)(region_size)))
MERCURY_GEN_PROC(bake_bulk_create_out_t,
    ((int32_t)(ret))\
    ((bake_bulk_region_id_t)(rid)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_create_ult)

/* bulk write */
MERCURY_GEN_PROC(bake_bulk_write_in_t,
    ((bake_target_id_t)(bti))\
    ((bake_bulk_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(bake_bulk_write_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_write_ult)

/* bulk eager write */
typedef struct 
{
    bake_target_id_t bti;
    bake_bulk_region_id_t rid;
    uint64_t region_offset;
    uint32_t size;
    char * buffer;
} bake_bulk_eager_write_in_t;
static inline hg_return_t hg_proc_bake_bulk_eager_write_in_t(hg_proc_t proc, void *v_out_p);
MERCURY_GEN_PROC(bake_bulk_eager_write_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_eager_write_ult)


/* bulk persist */
MERCURY_GEN_PROC(bake_bulk_persist_in_t,
    ((bake_target_id_t)(bti))\
    ((bake_bulk_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_bulk_persist_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_persist_ult)

/* bulk get size */
MERCURY_GEN_PROC(bake_bulk_get_size_in_t,
    ((bake_target_id_t)(bti))\
    ((bake_bulk_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_bulk_get_size_out_t,
    ((int32_t)(ret))\
    ((uint64_t)(size)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_get_size_ult)

/* bulk read */
MERCURY_GEN_PROC(bake_bulk_read_in_t,
    ((bake_target_id_t)(bti))\
    ((bake_bulk_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((hg_bulk_t)(bulk_handle)))
MERCURY_GEN_PROC(bake_bulk_read_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_read_ult)

/* bulk eager read */
MERCURY_GEN_PROC(bake_bulk_eager_read_in_t,
    ((bake_target_id_t)(bti))\
    ((bake_bulk_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((uint32_t)(size)))
typedef struct 
{
    int32_t ret;
    uint32_t size;
    char * buffer;
} bake_bulk_eager_read_out_t;
static inline hg_return_t hg_proc_bake_bulk_eager_read_out_t(hg_proc_t proc, void *v_out_p);
DECLARE_MARGO_RPC_HANDLER(bake_bulk_eager_read_ult)

/* bulk probe */
MERCURY_GEN_PROC(bake_bulk_probe_out_t,
    ((int32_t)(ret))\
    ((bake_target_id_t)(bti)))
DECLARE_MARGO_RPC_HANDLER(bake_bulk_probe_ult)

/* noop */
DECLARE_MARGO_RPC_HANDLER(bake_bulk_noop_ult)

/* TODO: this should be somewhere else, just putting in this header for
 * convenience right now.  The type should only be visible to the server
 * daemon and the rpc handlers.
 */
struct bake_bulk_root
{
    bake_target_id_t target_id;
};


/* TODO: where should the encoder defs live?  Not in bake-bulk-rpc.c because 
 * we don't really need the rpc handlers to be linked into clients...
 */
static inline hg_return_t hg_proc_bake_bulk_region_id_t(hg_proc_t proc, bake_bulk_region_id_t *rid)
{
    /* TODO: update later depending on final region_id_t type */
    /* TODO: need separate encoders for different backend types */
    int i;
    hg_return_t ret;

    hg_proc_hg_uint32_t(proc, &rid->type);
    for(i=0; i<BAKE_BULK_REGION_ID_DATA_SIZE; i++)
    {
        ret = hg_proc_hg_uint8_t(proc, (uint8_t*)&rid->data[i]);
        if(ret != HG_SUCCESS)
            return(ret);
    }
    return(HG_SUCCESS);
}

static inline hg_return_t hg_proc_bake_target_id_t(hg_proc_t proc, bake_target_id_t *bti)
{
    /* TODO: make this portable */
    return(hg_proc_memcpy(proc, bti->id, sizeof(bti->id)));
}

static inline hg_return_t hg_proc_bake_bulk_eager_write_in_t(hg_proc_t proc, void *v_out_p)
{
    /* TODO: error checking */
    bake_bulk_eager_write_in_t *in = v_out_p;

    hg_proc_bake_target_id_t(proc, &in->bti);
    hg_proc_bake_bulk_region_id_t(proc, &in->rid);
    hg_proc_uint64_t(proc, &in->region_offset);
    hg_proc_uint32_t(proc, &in->size);
    if(hg_proc_get_op(proc) == HG_DECODE)
        hg_proc_memcpy_decode_in_place(proc, (void**)(&in->buffer), in->size);
    else
        hg_proc_memcpy(proc, in->buffer, in->size);

    return(HG_SUCCESS);
}


static inline hg_return_t hg_proc_bake_bulk_eager_read_out_t(hg_proc_t proc, void *v_out_p)
{
    /* TODO: error checking */
    bake_bulk_eager_read_out_t *out = v_out_p;

    hg_proc_int32_t(proc, &out->ret);
    hg_proc_uint32_t(proc, &out->size);
    if(hg_proc_get_op(proc) == HG_DECODE)
        hg_proc_memcpy_decode_in_place(proc, (void**)(&out->buffer), out->size);
    else
        hg_proc_memcpy(proc, out->buffer, out->size);

    return(HG_SUCCESS);
}

#endif /* __BAKE_BULK_RPC */

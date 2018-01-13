/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_RPC
#define __BAKE_RPC

#include <uuid/uuid.h>
#include <margo.h>
#include <mercury_proc_string.h>
#include <bake.h>

/* encoders for BAKE-specific types */
static inline hg_return_t hg_proc_bake_target_id_t(hg_proc_t proc, bake_target_id_t *bti);
static inline hg_return_t hg_proc_bake_region_id_t(hg_proc_t proc, bake_region_id_t *rid);
static inline hg_return_t hg_proc_bake_probe_out_t(hg_proc_t proc, void* out);
/* BAKE shutdown */
DECLARE_MARGO_RPC_HANDLER(bake_shutdown_ult)

/* BAKE create */
MERCURY_GEN_PROC(bake_create_in_t,
    ((bake_target_id_t)(bti))\
    ((uint64_t)(region_size)))
MERCURY_GEN_PROC(bake_create_out_t,
    ((int32_t)(ret))\
    ((bake_region_id_t)(rid)))
DECLARE_MARGO_RPC_HANDLER(bake_create_ult)

/* BAKE write */
MERCURY_GEN_PROC(bake_write_in_t,
/*    ((bake_target_id_t)(bti))\ */
    ((bake_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((hg_bulk_t)(bulk_handle))\
    ((uint64_t)(bulk_offset))\
    ((uint64_t)(bulk_size))\
    ((hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_write_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_write_ult)

/* BAKE eager write */
typedef struct 
{
/*    bake_target_id_t bti; */
    bake_region_id_t rid;
    uint64_t region_offset;
    uint32_t size;
    char * buffer;
} bake_eager_write_in_t;
static inline hg_return_t hg_proc_bake_eager_write_in_t(hg_proc_t proc, void *v_out_p);
MERCURY_GEN_PROC(bake_eager_write_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_eager_write_ult)

/* BAKE persist */
MERCURY_GEN_PROC(bake_persist_in_t,
/*    ((bake_target_id_t)(bti))\ */
    ((bake_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_persist_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_persist_ult)

/* BAKE create/write/persist */
MERCURY_GEN_PROC(bake_create_write_persist_in_t,
    ((bake_target_id_t)(bti))\
    ((uint64_t)(region_size))\
    ((uint64_t)(region_offset))\
    ((hg_bulk_t)(bulk_handle))\
    ((uint64_t)(bulk_offset))\
    ((uint64_t)(bulk_size))\
    ((hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_create_write_persist_out_t,
    ((int32_t)(ret))\
    ((bake_region_id_t)(rid)))
DECLARE_MARGO_RPC_HANDLER(bake_create_write_persist_ult)

/* BAKE get size */
MERCURY_GEN_PROC(bake_get_size_in_t,
/*    ((bake_target_id_t)(bti))\ */
    ((bake_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_get_size_out_t,
    ((int32_t)(ret))\
    ((uint64_t)(size)))
DECLARE_MARGO_RPC_HANDLER(bake_get_size_ult)

/* BAKE read */
MERCURY_GEN_PROC(bake_read_in_t,
/*    ((bake_target_id_t)(bti))\ */
    ((bake_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((hg_bulk_t)(bulk_handle))\
    ((uint64_t)(bulk_offset))\
    ((uint64_t)(bulk_size))\
    ((hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_read_out_t,
    ((int32_t)(ret)))
DECLARE_MARGO_RPC_HANDLER(bake_read_ult)

/* BAKE eager read */
MERCURY_GEN_PROC(bake_eager_read_in_t,
/*    ((bake_target_id_t)(bti))\ */
    ((bake_region_id_t)(rid))\
    ((uint64_t)(region_offset))\
    ((uint32_t)(size)))
typedef struct 
{
    int32_t ret;
    uint32_t size;
    char * buffer;
} bake_eager_read_out_t;
static inline hg_return_t hg_proc_bake_eager_read_out_t(hg_proc_t proc, void *v_out_p);
DECLARE_MARGO_RPC_HANDLER(bake_eager_read_ult)

/* BAKE probe */
MERCURY_GEN_PROC(bake_probe_in_t,
                ((uint64_t)(max_targets)))
//MERCURY_GEN_PROC(bake_probe_out_t,
//    ((int32_t)(ret))\
//    ((bake_target_id_t)(bti)))
typedef struct
{
    int32_t ret;
    uint64_t num_targets;
    bake_target_id_t* targets;
} bake_probe_out_t;
DECLARE_MARGO_RPC_HANDLER(bake_probe_ult)

/* BAKE noop */
DECLARE_MARGO_RPC_HANDLER(bake_noop_ult)


static inline hg_return_t hg_proc_bake_region_id_t(hg_proc_t proc, bake_region_id_t *rid)
{
    /* TODO: update later depending on final region_id_t type */
    /* TODO: need separate encoders for different backend types */
    int i;
    hg_return_t ret;

    hg_proc_hg_uint32_t(proc, &rid->type);
    for(i=0; i<BAKE_REGION_ID_DATA_SIZE; i++)
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
    return(hg_proc_memcpy(proc, bti, sizeof(*bti)));
}

static inline hg_return_t hg_proc_bake_eager_write_in_t(hg_proc_t proc, void *v_out_p)
{
    /* TODO: error checking */
    bake_eager_write_in_t *in = v_out_p;
    void *buf = NULL;

/*    hg_proc_bake_target_id_t(proc, &in->bti); */
    hg_proc_bake_region_id_t(proc, &in->rid);
    hg_proc_uint64_t(proc, &in->region_offset);
    hg_proc_uint32_t(proc, &in->size);
    if(in->size)
    {
        buf = hg_proc_save_ptr(proc, in->size);
        if(hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, in->buffer, in->size);
        if(hg_proc_get_op(proc) == HG_DECODE)
            in->buffer = buf;
        hg_proc_restore_ptr(proc, buf, in->size);
    }

    return(HG_SUCCESS);
}


static inline hg_return_t hg_proc_bake_eager_read_out_t(hg_proc_t proc, void *v_out_p)
{
    /* TODO: error checking */
    bake_eager_read_out_t *out = v_out_p;
    void *buf = NULL;

    hg_proc_int32_t(proc, &out->ret);
    hg_proc_uint32_t(proc, &out->size);
    if(out->size)
    {
        buf = hg_proc_save_ptr(proc, out->size);
        if(hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, out->buffer, out->size);
        if(hg_proc_get_op(proc) == HG_DECODE)
            out->buffer = buf;
        hg_proc_restore_ptr(proc, buf, out->size);
    }

    return(HG_SUCCESS);
}

static inline hg_return_t hg_proc_bake_probe_out_t(hg_proc_t proc, void* data)
{
    bake_probe_out_t* out = (bake_probe_out_t*)data;
    void* buf = NULL;

    hg_proc_int32_t(proc, &out->ret);
    hg_proc_uint64_t(proc, &out->num_targets);
    if(out->num_targets)
    {
        buf = hg_proc_save_ptr(proc, out->num_targets * sizeof(bake_target_id_t));
        if(hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, out->targets, out->num_targets * sizeof(bake_target_id_t));
        if(hg_proc_get_op(proc) == HG_DECODE)
            out->targets = buf;
        hg_proc_restore_ptr(proc, buf, out->num_targets * sizeof(bake_target_id_t));
    }
    return HG_SUCCESS;
}

#endif /* __BAKE_RPC */

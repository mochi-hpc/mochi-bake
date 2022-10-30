/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_RPC
#define __BAKE_RPC

#include <uuid.h>
#include <margo.h>
#include <mercury_proc_string.h>
#include <bake.h>

/* encoders for BAKE-specific types */
static inline hg_return_t hg_proc_bake_target_id_t(hg_proc_t         proc,
                                                   bake_target_id_t* bti);
static inline hg_return_t hg_proc_bake_region_id_t(hg_proc_t         proc,
                                                   bake_region_id_t* rid);
static inline hg_return_t hg_proc_bake_probe_out_t(hg_proc_t proc, void* out);

/* BAKE create */
MERCURY_GEN_PROC(bake_create_in_t,
                 ((bake_target_id_t)(bti))((uint64_t)(region_size)))
MERCURY_GEN_PROC(bake_create_out_t, ((int32_t)(ret))((bake_region_id_t)(rid)))

/* BAKE write */
MERCURY_GEN_PROC(bake_write_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid))(
                     (uint64_t)(region_offset))((hg_bulk_t)(bulk_handle))(
                     (uint64_t)(bulk_offset))((uint64_t)(bulk_size))(
                     (hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_write_out_t, ((int32_t)(ret)))

/* BAKE eager write */
typedef struct {
    bake_target_id_t bti;
    bake_region_id_t rid;
    uint64_t         region_offset;
    uint64_t         size;
    char*            buffer;
} bake_eager_write_in_t;
static inline hg_return_t hg_proc_bake_eager_write_in_t(hg_proc_t proc,
                                                        void*     v_out_p);
MERCURY_GEN_PROC(bake_eager_write_out_t, ((int32_t)(ret)))

/* BAKE persist */
MERCURY_GEN_PROC(bake_persist_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid))(
                     (uint64_t)(offset))((uint64_t)(size)))
MERCURY_GEN_PROC(bake_persist_out_t, ((int32_t)(ret)))

/* BAKE create/write/persist */
MERCURY_GEN_PROC(bake_create_write_persist_in_t,
                 ((bake_target_id_t)(bti))((uint64_t)(region_size))(
                     (hg_bulk_t)(bulk_handle))((uint64_t)(bulk_offset))(
                     (uint64_t)(bulk_size))((hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_create_write_persist_out_t,
                 ((int32_t)(ret))((bake_region_id_t)(rid)))

/* BAKE eager create/write/persist */
typedef struct {
    bake_target_id_t bti;
    uint64_t         size;
    char*            buffer;
} bake_eager_create_write_persist_in_t;
static inline hg_return_t
hg_proc_bake_eager_create_write_persist_in_t(hg_proc_t proc, void* v_out_p);
MERCURY_GEN_PROC(bake_eager_create_write_persist_out_t,
                 ((int32_t)(ret))((bake_region_id_t)(rid)))

/* BAKE get size */
MERCURY_GEN_PROC(bake_get_size_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_get_size_out_t, ((int32_t)(ret))((uint64_t)(size)))

/* BAKE get data */
MERCURY_GEN_PROC(bake_get_data_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_get_data_out_t, ((int32_t)(ret))((uint64_t)(ptr)))

/* BAKE read */
MERCURY_GEN_PROC(bake_read_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid))(
                     (uint64_t)(region_offset))((hg_bulk_t)(bulk_handle))(
                     (uint64_t)(bulk_offset))((uint64_t)(bulk_size))(
                     (hg_string_t)(remote_addr_str)))
MERCURY_GEN_PROC(bake_read_out_t, ((hg_size_t)(size))((int32_t)(ret)))

/* BAKE eager read */
MERCURY_GEN_PROC(bake_eager_read_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid))(
                     (uint64_t)(region_offset))((uint64_t)(size)))
typedef struct {
    int32_t  ret;
    uint64_t size;
    char*    buffer;
} bake_eager_read_out_t;
static inline hg_return_t hg_proc_bake_eager_read_out_t(hg_proc_t proc,
                                                        void*     v_out_p);

/* BAKE probe */
MERCURY_GEN_PROC(bake_probe_in_t, ((uint64_t)(max_targets)))
typedef struct {
    int32_t           ret;
    uint64_t          num_targets;
    bake_target_id_t* targets;
} bake_probe_out_t;

/* BAKE remove */
MERCURY_GEN_PROC(bake_remove_in_t,
                 ((bake_target_id_t)(bti))((bake_region_id_t)(rid)))
MERCURY_GEN_PROC(bake_remove_out_t, ((int32_t)(ret)))

/* BAKE migrate region */
MERCURY_GEN_PROC(
    bake_migrate_region_in_t,
    ((bake_target_id_t)(bti))((bake_region_id_t)(source_rid))(
        (uint64_t)(region_size))((int32_t)(remove_src))(
        (hg_const_string_t)(dest_addr))((uint16_t)(dest_provider_id))(
        (bake_target_id_t)(dest_target_id)))
MERCURY_GEN_PROC(bake_migrate_region_out_t,
                 ((int32_t)(ret))((bake_region_id_t)(dest_rid)))

/* BAKE migrate target */
MERCURY_GEN_PROC(
    bake_migrate_target_in_t,
    ((bake_target_id_t)(bti))((int32_t)(remove_src))(
        (hg_const_string_t)(dest_remi_addr))((uint16_t)(dest_remi_provider_id))(
        (hg_const_string_t)(dest_root)))
MERCURY_GEN_PROC(bake_migrate_target_out_t, ((int32_t)(ret)))

static inline hg_return_t hg_proc_bake_region_id_t(hg_proc_t         proc,
                                                   bake_region_id_t* rid)
{
    /* TODO: update later depending on final region_id_t type */
    /* TODO: need separate encoders for different backend types */
    int         i;
    hg_return_t ret;

    hg_proc_hg_uint32_t(proc, &rid->type);
    for (i = 0; i < BAKE_REGION_ID_DATA_SIZE; i++) {
        ret = hg_proc_hg_uint8_t(proc, (uint8_t*)&rid->data[i]);
        if (ret != HG_SUCCESS) return (ret);
    }
    return (HG_SUCCESS);
}

static inline hg_return_t hg_proc_bake_target_id_t(hg_proc_t         proc,
                                                   bake_target_id_t* bti)
{
    /* TODO: make this portable */
    return (hg_proc_memcpy(proc, bti, sizeof(*bti)));
}

static inline hg_return_t hg_proc_bake_eager_write_in_t(hg_proc_t proc,
                                                        void*     v_out_p)
{
    /* TODO: error checking */
    bake_eager_write_in_t* in  = v_out_p;
    void*                  buf = NULL;

    hg_proc_bake_target_id_t(proc, &in->bti);
    hg_proc_bake_region_id_t(proc, &in->rid);
    hg_proc_uint64_t(proc, &in->region_offset);
    hg_proc_uint64_t(proc, &in->size);
    if (in->size
        && (hg_proc_get_op(proc) == HG_ENCODE
            || hg_proc_get_op(proc) == HG_DECODE)) {
        buf = hg_proc_save_ptr(proc, in->size);
        if (hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, in->buffer, in->size);
        if (hg_proc_get_op(proc) == HG_DECODE) in->buffer = buf;
        hg_proc_restore_ptr(proc, buf, in->size);
    }

    return (HG_SUCCESS);
}

static inline hg_return_t
hg_proc_bake_eager_create_write_persist_in_t(hg_proc_t proc, void* v_out_p)
{
    /* TODO: error checking */
    bake_eager_create_write_persist_in_t* in  = v_out_p;
    void*                                 buf = NULL;

    hg_proc_bake_target_id_t(proc, &in->bti);
    hg_proc_uint64_t(proc, &in->size);
    if (in->size
        && (hg_proc_get_op(proc) == HG_ENCODE
            || hg_proc_get_op(proc) == HG_DECODE)) {
        buf = hg_proc_save_ptr(proc, in->size);
        if (hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, in->buffer, in->size);
        if (hg_proc_get_op(proc) == HG_DECODE) in->buffer = buf;
        hg_proc_restore_ptr(proc, buf, in->size);
    }

    return (HG_SUCCESS);
}

static inline hg_return_t hg_proc_bake_eager_read_out_t(hg_proc_t proc,
                                                        void*     v_out_p)
{
    /* TODO: error checking */
    bake_eager_read_out_t* out = v_out_p;
    void*                  buf = NULL;

    hg_proc_int32_t(proc, &out->ret);
    hg_proc_uint32_t(proc, &out->size);
    if (out->size
        && (hg_proc_get_op(proc) == HG_ENCODE
            || hg_proc_get_op(proc) == HG_DECODE)) {
        buf = hg_proc_save_ptr(proc, out->size);
        if (hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, out->buffer, out->size);
        if (hg_proc_get_op(proc) == HG_DECODE) out->buffer = buf;
        hg_proc_restore_ptr(proc, buf, out->size);
    }

    return (HG_SUCCESS);
}

static inline hg_return_t hg_proc_bake_probe_out_t(hg_proc_t proc, void* data)
{
    bake_probe_out_t* out = (bake_probe_out_t*)data;
    void*             buf = NULL;

    hg_proc_int32_t(proc, &out->ret);
    hg_proc_uint64_t(proc, &out->num_targets);
    if (out->num_targets
        && (hg_proc_get_op(proc) == HG_ENCODE
            || hg_proc_get_op(proc) == HG_DECODE)) {
        buf = hg_proc_save_ptr(proc,
                               out->num_targets * sizeof(bake_target_id_t));
        if (hg_proc_get_op(proc) == HG_ENCODE)
            memcpy(buf, out->targets,
                   out->num_targets * sizeof(bake_target_id_t));
        if (hg_proc_get_op(proc) == HG_DECODE) out->targets = buf;
        hg_proc_restore_ptr(proc, buf,
                            out->num_targets * sizeof(bake_target_id_t));
    }
    return HG_SUCCESS;
}

#endif /* __BAKE_RPC */

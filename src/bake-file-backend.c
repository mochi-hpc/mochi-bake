#include <assert.h>
#include <abt-io.h>
#include "bake-config.h"
#include "bake.h"
#include "bake-rpc.h"
#include "bake-server.h"
#include "bake-provider.h"
#include "bake-backend.h"

/* bake-file-backend
 *
 * This is an implemenation of a back end for the Bake provider that stores
 * all data in normal POSIX files.  All data is stored in a single
 * block-aligned, log-structured, file and accessed using directio through
 * the abt-io library.
 */

/* definition of BAKE root data structure (just a uuid for now) */
typedef struct
{
    bake_target_id_t pool_id;
} bake_root_t;

/* definition of internal BAKE region_id_t identifier for file back end */
typedef struct
{
    off_t offset;
    size_t log_entry_size;
} file_region_id_t;

typedef struct {
    char data[1];
} region_content_t;

typedef struct {
    bake_provider_t provider;  /* TODO: is this needed here? */
    int log_fd;       /* file descriptor for log */
    off_t log_offset; /* next available unused offset in log */
    ABT_mutex log_offset_mutex; /* protects the above during concurrent region creation */
    abt_io_instance_id abtioi;  /* abt-io instance used by this provider */
    bake_root_t* file_root;
    char* root;
    char* filename;
} bake_file_entry_t;

/* TODO: figure out how to modularize this; conflicts with pmem one right
 * now
 */
#if 0
int bake_makepool(
        const char *pool_name,
        size_t pool_size,
        mode_t pool_mode)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_initialize(bake_provider_t provider,
                                        const char* path,
                                        bake_target_id_t *target,
                                        backend_context_t *context)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}
////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_finalize(backend_context_t context)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_create(backend_context_t context,
                            size_t size,
                            bake_region_id_t *rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_raw(backend_context_t context,
                               bake_region_id_t rid,
                               size_t offset,
                               size_t size,
                               const void* data)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_bulk(backend_context_t context,
                                bake_region_id_t rid,
                                size_t region_offset,
                                size_t size,
                                hg_bulk_t bulk,
                                hg_addr_t source,
                                size_t bulk_offset)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_read_raw(backend_context_t context,
                              bake_region_id_t rid,
                              size_t offset,
                              size_t size,
                              void** data,
                              uint64_t* data_size,
                              free_fn* free_data)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_read_bulk(backend_context_t context,
                               bake_region_id_t rid,
                               size_t region_offset,
                               size_t size,
                               hg_bulk_t bulk,
                               hg_addr_t source,
                               size_t bulk_offset,
                               size_t* bytes_read)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_persist(backend_context_t context,
                             bake_region_id_t rid,
                             size_t offset,
                             size_t size)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_create_write_persist_raw(backend_context_t context,
                                              const void* data,
                                              size_t size,
                                              bake_region_id_t *rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_create_write_persist_bulk(backend_context_t context,
                                               hg_bulk_t bulk,
                                               hg_addr_t source,
                                               size_t bulk_offset,
                                               size_t size,
                                               bake_region_id_t *rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_get_region_size(backend_context_t context,
                                     bake_region_id_t rid,
                                     size_t* size)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_get_region_data(backend_context_t context,
                                     bake_region_id_t rid,
                                     void** data)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_remove(backend_context_t context,
                            bake_region_id_t rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_migrate_region(backend_context_t context,
                                    bake_region_id_t source_rid,
                                    size_t region_size,
                                    int remove_source,
                                    const char* dest_addr_str,
                                    uint16_t dest_provider_id,
                                    bake_target_id_t dest_target_id,
                                    bake_region_id_t *dest_rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_set_conf(backend_context_t context,
                              const char* key,
                              const char* value)
{
    return 0;
}

bake_backend g_bake_file_backend = {
    .name                       = "file",
    ._initialize                = bake_file_backend_initialize,
    ._finalize                  = bake_file_backend_finalize,
    ._create                    = bake_file_create,
    ._write_raw                 = bake_file_write_raw,
    ._write_bulk                = bake_file_write_bulk,
    ._read_raw                  = bake_file_read_raw,
    ._read_bulk                 = bake_file_read_bulk,
    ._persist                   = bake_file_persist,
    ._create_write_persist_raw  = bake_file_create_write_persist_raw,
    ._create_write_persist_bulk = bake_file_create_write_persist_bulk,
    ._get_region_size           = bake_file_get_region_size,
    ._get_region_data           = bake_file_get_region_data,
    ._remove                    = bake_file_remove,
    ._migrate_region            = bake_file_migrate_region,
#ifdef USE_REMI
    ._create_fileset            = bake_file_create_fileset,
#endif
    ._set_conf                  = bake_file_set_conf
};


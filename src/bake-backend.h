#ifndef __BAKE_BACKEND_H
#define __BAKE_BACKEND_H

#include "bake-server.h"
#include "bake.h"

typedef struct bake_provider* bake_provider_t;
typedef void*                 backend_context_t;

typedef int (*bake_backend_initialize_fn)(bake_provider_t    provider,
                                          const char*        path,
                                          bake_target_id_t*  target,
                                          backend_context_t* context);

typedef int (*bake_backend_finalize_fn)(backend_context_t context);

typedef int (*bake_create_fn)(backend_context_t context,
                              size_t            size,
                              bake_region_id_t* rid);

typedef int (*bake_write_raw_fn)(backend_context_t context,
                                 bake_region_id_t  rid,
                                 size_t            offset,
                                 size_t            size,
                                 const void*       data);

typedef int (*bake_write_bulk_fn)(backend_context_t context,
                                  bake_region_id_t  rid,
                                  size_t            region_offset,
                                  size_t            size,
                                  hg_bulk_t         bulk,
                                  hg_addr_t         source,
                                  size_t            bulk_offset);

typedef void (*free_fn)(void*);

typedef int (*bake_read_raw_fn)(backend_context_t context,
                                bake_region_id_t  rid,
                                size_t            offset,
                                size_t            size,
                                void**            data,
                                uint64_t*         bytes_available,
                                free_fn*          free_data);

typedef int (*bake_read_bulk_fn)(backend_context_t context,
                                 bake_region_id_t  rid,
                                 size_t            region_offset,
                                 size_t            size,
                                 hg_bulk_t         bulk,
                                 hg_addr_t         source,
                                 size_t            bulk_offset,
                                 size_t*           bytes_read);

typedef int (*bake_persist_fn)(backend_context_t context,
                               bake_region_id_t  rid,
                               size_t            offset,
                               size_t            size);

typedef int (*bake_create_write_persist_raw_fn)(backend_context_t context,
                                                const void*       data,
                                                size_t            size,
                                                bake_region_id_t* rid);

typedef int (*bake_create_write_persist_bulk_fn)(backend_context_t context,
                                                 hg_bulk_t         bulk,
                                                 hg_addr_t         source,
                                                 size_t            bulk_offset,
                                                 size_t            size,
                                                 bake_region_id_t* rid);

typedef int (*bake_get_region_size_fn)(backend_context_t context,
                                       bake_region_id_t  rid,
                                       size_t*           size);

typedef int (*bake_get_region_data_fn)(backend_context_t context,
                                       bake_region_id_t  rid,
                                       void**            data);

typedef int (*bake_remove_fn)(backend_context_t context, bake_region_id_t rid);

typedef int (*bake_migrate_region_fn)(backend_context_t context,
                                      bake_region_id_t  source_rid,
                                      size_t            region_size,
                                      int               remove_source,
                                      const char*       dest_addr,
                                      uint16_t          dest_provider_id,
                                      bake_target_id_t  dest_target_id,
                                      bake_region_id_t* dest_rid);

#ifdef USE_REMI
typedef int (*bake_create_fileset_fn)(backend_context_t context,
                                      remi_fileset_t*   fileset);
#endif

typedef int (*bake_set_conf_fn)(backend_context_t context,
                                const char*       key,
                                const char*       value);

typedef struct bake_backend {
    const char*                       name;
    bake_backend_initialize_fn        _initialize;
    bake_backend_finalize_fn          _finalize;
    bake_create_fn                    _create;
    bake_write_raw_fn                 _write_raw;
    bake_write_bulk_fn                _write_bulk;
    bake_read_raw_fn                  _read_raw;
    bake_read_bulk_fn                 _read_bulk;
    bake_persist_fn                   _persist;
    bake_create_write_persist_raw_fn  _create_write_persist_raw;
    bake_create_write_persist_bulk_fn _create_write_persist_bulk;
    bake_get_region_size_fn           _get_region_size;
    bake_get_region_data_fn           _get_region_data;
    bake_remove_fn                    _remove;
    bake_migrate_region_fn            _migrate_region;
#ifdef USE_REMI
    bake_create_fileset_fn _create_fileset;
#endif
    bake_set_conf_fn _set_conf;
} bake_backend;

typedef bake_backend* bake_backend_t;

#endif

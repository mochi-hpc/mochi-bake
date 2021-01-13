/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __PROXY_RPC
#define __PROXY_RPC

#include <margo.h>
#include <mercury_proc_string.h>

MERCURY_GEN_PROC(proxy_write_in_t,
                 ((hg_bulk_t)(bulk_handle))((uint64_t)(bulk_offset))(
                     (uint64_t)(bulk_size))((hg_const_string_t)(bulk_addr)))
MERCURY_GEN_PROC(proxy_write_out_t, ((int32_t)(ret)))

MERCURY_GEN_PROC(proxy_read_in_t,
                 ((hg_bulk_t)(bulk_handle))((uint64_t)(bulk_offset))(
                     (uint64_t)(bulk_size))((hg_const_string_t)(bulk_addr)))
MERCURY_GEN_PROC(proxy_read_out_t, ((int32_t)(ret)))

#endif /* __PROXY_RPC */

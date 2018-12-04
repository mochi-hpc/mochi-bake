/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_CLIENT_H
#define __BAKE_CLIENT_H

#include <stdint.h>
#include <margo.h>
#include <bake.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAKE_CLIENT_NULL ((bake_client_t)NULL)
#define BAKE_PROVIDER_HANDLE_NULL ((bake_provider_handle_t)NULL)

typedef struct bake_client* bake_client_t;
typedef struct bake_provider_handle* bake_provider_handle_t;

/**
 * Creates a BAKE client attached to the given margo instance.
 * This will effectively register the RPC needed by BAKE into
 * the margo instance. The client must be freed with
 * bake_client_finalize.
 *
 * @param[in] mid margo instance
 * @param[out] client resulting bake client object
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_client_init(margo_instance_id mid, bake_client_t* client);

/**
 * Finalizes a BAKE client.
 * WARNING: This function must not be called after Margo has been
 * finalized. If you need to finalize a BAKE client when Margo is
 * finalized, use margo_push_finalize_callback.
 *
 * @param client BAKE client to destroy
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_client_finalize(bake_client_t client);

/**
 * Creates a provider handle to point to a particular BAKE provider.
 *
 * @param client client managing the provider handle
 * @param addr address of the provider
 * @param provider_id id of the provider
 * @param handle resulting handle
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_provider_handle_create(
        bake_client_t client,
        hg_addr_t addr,
        uint16_t provider_id,
        bake_provider_handle_t* handle);

/**
 * Increment the reference counter of the provider handle
 *
 * @param handle provider handle
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_provider_handle_ref_incr(bake_provider_handle_t handle);

/**
 * Get the limit (in bytes) bellow which this provider handle will use
 * eager mode (i.e. packing data into the RPC instead of using RDMA). 
 *
 * @param[in] handle provider handle
 * @param[out] limit limit
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_provider_handle_get_eager_limit(bake_provider_handle_t handle, uint64_t* limit);

/**
 * Set the limit (in bytes) bellow which this provider handle will use
 * eager mode (i.e. packing data into the RPC instead of using RDMA).
 *
 * @param[in] handle provider handle
 * @param[in] limit limit
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_provider_handle_set_eager_limit(bake_provider_handle_t handle, uint64_t limit);

/**
 * Decrement the reference counter of the provider handle,
 * effectively freeing the provider handle when the reference count
 * is down to 0.
 *
 * @param handle provider handle
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_provider_handle_release(bake_provider_handle_t handle);

/**
 * Obtains available BAKE targets from a give provider.
 * If bake_target_id_t is NULL, max_targets is ignored and the
 * function returns the number of targets available in num_targets.
 *
 * @param [in] provider provider handle
 * @param [in] max_targets maximum number of targets to retrieve
 * @param [out] bti array of BAKE target identifiers with enough space for max_targets
 * @param [out] num_targets number of targets returned (at most max_targets)
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_probe(
        bake_provider_handle_t provider,
        uint64_t max_targets,
        bake_target_id_t* bti,
        uint64_t* num_targets);
  
/**
 * Creates a bounded-size BAKE data region. The resulting region can be
 * written using BAKE write operations, and can be persisted (once writes
 * are complete) with a a BAKE persist operation.  The region is not valid
 * for read access until persisted.
 *
 * @param [in] provider provider handle
 * @param [in] bti BAKE target identifier
 * @param [in] region_size size of region to be created
 * @param [out] rid identifier for new region
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_create(
        bake_provider_handle_t provider,
        bake_target_id_t bti,
        uint64_t region_size,
        bake_region_id_t *rid);
 
/**
 * Writes into a BAKE region that was previously created with bake_create().
 * Result is not guaranteed to be persistent until explicit
 * bake_persist() call.
 *
 * Results are undefined if multiple writers (from same process or different
 * processes) perform overlapping writes.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [in] region_offset offset into the target region to write
 * @param [in] buf local memory buffer to write
 * @param [in] buf_size size of local memory buffer to write
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_write(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        uint64_t region_offset,
        void const *buf,
        uint64_t buf_size);

/**
 * Writes data into a previously created BAKE region like bake_write(),
 * except the write is performed on behalf of some remote entity.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [in] region_offset offset into the target region to write
 * @param [in] remote_bulk bulk_handle for remote data region to write from
 * @param [in] remote_offset offset in the remote bulk handle to write from
 * @param [in] remote_addr address string of the remote target to write from
 * @param [in] size size to write from remote bulk handle
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_proxy_write(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        uint64_t region_offset,
        hg_bulk_t remote_bulk,
        uint64_t remote_offset,
        const char* remote_addr,
        uint64_t size);

/**
 * Persists a BAKE region. The region is considered immutable at this point 
 * and reads may be performed on the region.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [in] offset offset in the region
 * @param [in] size of the region
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_persist(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        size_t offset,
        size_t size);

/**
 * Creates a bounded-size BAKE region, writes data into it, and persists
 * the reason all in one call/RPC (and thus 1 RTT).
 *
 * @param [in] provider provider handle
 * @param [in] bti BAKE target identifier
 * @param [in] buf local memory buffer to write
 * @param [in] buf_size size of local memory buffer to write
 * @param [out] rid identifier for new region
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_create_write_persist(
        bake_provider_handle_t provider,
        bake_target_id_t bti,
        void const *buf,
        uint64_t buf_size,
        bake_region_id_t *rid);

/**
 *
 * @param [in] provider provider handle
 * @param [in] bti BAKE target identifier
 * @param [in] remote_bulk bulk_handle for remote data region to write from
 * @param [in] remote_offset offset in the remote bulk handle to write from
 * @param [in] remote_addr address string of the remote target to write from
 * @param [in] size size to write from remote bulk handle
 * @param [out] rid identifier for new region
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_create_write_persist_proxy(
        bake_provider_handle_t provider,
        bake_target_id_t bti,
        hg_bulk_t remote_bulk,
        uint64_t remote_offset,
        const char* remote_addr,
        uint64_t size,
        bake_region_id_t *rid);

/**
 * Checks the size of an existing BAKE region.
 * This function only works if Bake has been compiled with --enable-sizecheck,
 * otherwise Bake has no way of knowing the size of regions and it is up to
 * the user to track the region sizes in some other ways.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [out] size size of region
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_get_size(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        uint64_t *size);

/**
 * Gets the raw pointer of an existing BAKE region.
 * This pointer is valid in the address space of the
 * targeted BAKE provider. This function is meant to
 * be used when the client is co-located with the
 * BAKE provider and lives in the same address space
 * (e.g. active storage scenarios where we want to
 * access a piece of data without making a copy).
 * This function will return an error if the caller
 * is not running at the same address as the provider.
 *
 * Note that if the data pointed to is modified by the
 * client, the client will need to call bake_persist
 * to make the provider persist the changes.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [out] ptr pointer to the address of the data
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_get_data(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        void** ptr);

/**
 * Reads from a BAKE region that was previously persisted with bake_persist().
 *
 * NOTE: for now at least, this call does not support "short" reads.  It
 * either succeeds in reading the requested size or not.
 *
 * @param [in] provider provider handle
 * @param [in] rid region identifier
 * @param [in] region_offset offset into the target region to read from
 * @param [in] buf local memory buffer read into
 * @param [in] buf_size size of local memory buffer to read into
 * @param [out] bytes_read number of bytes effectively read into the buffer
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_read(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        uint64_t region_offset,
        void *buf,
        uint64_t buf_size,
        uint64_t* bytes_read);

/**
 * Reads data from a previously persisted BAKE region like bake_read(),
 * except the read is performed on behalf of some remote entity.
 *
 * @param [in] provider provider handle
 * @param [in] rid identifier for region
 * @param [in] region_offset offset into the target region to write
 * @param [in] remote_bulk bulk_handle for remote data region to read to
 * @param [in] remote_offset offset in the remote bulk handle to read to
 * @param [in] remote_addr address string of the remote target to read to
 * @param [in] size size to read to remote bulk handle
 * @param [out] bytes_read number of bytes effectively read
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_proxy_read(
        bake_provider_handle_t provider,
        bake_region_id_t rid,
        uint64_t region_offset,
        hg_bulk_t remote_bulk,
        uint64_t remote_offset,
        const char* remote_addr,
        uint64_t size,
        uint64_t* bytes_read);

/**
 * @brief Requests the source provider to migrate a particular
 * region (source_rid) to a destination provider. After the call,
 * the designated region will have been removed from the source
 * and the dest_rid parameter will be set to the new region id
 * in the destination provider.
 *
 * @param source Source provider.
 * @param source_rid Region to migrate.
 * @param region_size Size of the region to migrate.
 * @param remove_source Whether the source region should be removed.
 * @param dest_addr Address of the destination provider.
 * @param dest_provider_id Id of the destination provider.
 * @param dest_target_id Destination target.
 * @param dest_rid Resulting region id in the destination target.
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_migrate_region(
        bake_provider_handle_t source,
        bake_region_id_t source_rid,
        size_t region_size,
        int remove_source,
        const char* dest_addr,
        uint16_t dest_provider_id,
        bake_target_id_t dest_target_id,
        bake_region_id_t* dest_rid);

/**
 * @brief Migrates a full target from a provider to another.
 *
 * @param source Provider initially managing the target
 * @param src_target_id Source target it.
 * @param remove_source Whether the source target should be removed.
 * @param dest_addr Address of the destination provider.
 * @param dest_provider_id Provider id of the destination provider.
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_migrate_target(
        bake_provider_handle_t source,
        bake_target_id_t src_target_id,
        int remove_source,
        const char* dest_addr,
        uint16_t dest_provider_id,
        const char* dest_root);

/**
 * Shuts down a remote BAKE service (given an address).
 * This will shutdown all the providers on the target address.
 * 
 * @param [in] client BAKE client
 * @param [in] addr address of the server 
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_shutdown_service(
        bake_client_t client,
        hg_addr_t addr);

/**
 * Issues a BAKE no-op operation.
 *
 * @param [in] provider provider handle
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_noop(bake_provider_handle_t provider);

/**
 * Removes a previously persisted BAKE region and frees its associated memory.
 * 
 * @param provider Provider in which to remove the region.
 * @param rid Region to remove.
 *
 * @return BAKE_SUCCESS or corresponding error code.
 */
int bake_remove(
        bake_provider_handle_t provider,
        bake_region_id_t rid);

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_CLIENT_H */

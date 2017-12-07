/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_BULK_H
#define __BAKE_BULK_H

#include <uuid/uuid.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Persistent, universal, opaque identifier for a BAKE target.
 * Remains constant if instance is opened, closed, or migrated.
 */
typedef struct {
    uuid_t id;
} bake_target_id_t;

/**
 * Persistent, opaque identifier for a bulk region within a BAKE target.
 */
#define BAKE_BULK_REGION_ID_DATA_SIZE 24
typedef struct {
    uint32_t type;
    char data[BAKE_BULK_REGION_ID_DATA_SIZE];
} bake_bulk_region_id_t;

#if 0

/// ==== Some high-level goals ====
// - abstract particular keyval service being used
// - use same API path for intra-process, intra-node, and inter-node
//   communication
// - non-blocking for anything that might touch network/storage.
// - simple core functionality - save advanced functionality for when we're
//   further along with the project
 
/// ==== Initialization ====
 
#include <stdint.h>
 
// A bake_instance_t manages a storage resource (logically a one-to-one mapping)
// and optionally provides remote access to them through Mercury, encapsulating the
// communication context under which it was created.
typedef struct bake_instance_t bake_instance_t;
 
// An asynchronous request handle.
// Note that all BAKE operations except for local initialization/finalization
// are non-blocking.
//
// TODO: Whether this is simply a shim over a mercury/evfibers/etc.
// datatype or will take the form of a callback function called by
// Mercury is unknown.
typedef struct bake_request_t bake_request_t;
// Indicator to have the corresponding bake ops be blocking
#define BAKE_OP_BLOCKING ((bake_request_t*)NULL)
 
// Return types
typedef int bake_return_t;
 
#if 0
// NOTE: mercury types are just placeholders. The interaction between mercury,
// threading, and BAKE is somewhat up in the air at this point. Could
// potentially be much different if we go the multi-process route vs. the
// multi-thread route.
#include <mercury-service.h>
#endif

// ### included in mercury-service.h ###
// Abstraction for an instance of whatever our mercury layer ends up looking
// like, possibly including things like thread/process resources, the "self"
// addr(s), hg/na contexts, etc.
typedef struct mercury_instance_t mercury_instance_t;
// in the case of initializing a purely local bake service, won't need a
// corresponding mercury instance
#define MERCURY_INSTANCE_NONE ((mercury_instance_t*)NULL)
// Abstraction for a mercury address (could simply be an na_addr_t)
typedef struct mercury_address_t mercury_address_t;
// ### end mercury-service.h inclusions ###
 
// Configuration for the bulk storage and KV components, respectively. Again,
// won't know exactly what these will look like but will probably be a shim for
// whatever the underyling tech is (i.e. leveldb/rocksdb tunables)
typedef struct bake_bulk_options_t bake_bulk_options_t;
#define BAKE_BULK_OPTIONS_DEFAULT ((bake_bulk_options_t*) NULL)
 
// Initialize a bake instance and expose accessible storage to API users. The
// instance registers RPCs and recieves requests through the provided mercury
// instance, unless it is MERCURY_INSTANCE_NONE, in which case only in-process
// requests can be made.
//
// NOTE: this function is not reentrant
//
// TODO: determine how control flow looks after a "server" is initialized. Is
// it a thread and the calling context just sleeps until the service is
// finalized? Or is there an event loop that the caller must enter? (This
// doesn't count the option of someone using the localized bake service
// directly)
// TODO: options for initializing multiple targets on the same node?
bake_return_t bake_init_instance(
        bake_bulk_options_t const *bulk_opts,
#if 0
        bake_kv_options_t const *kv_opts,
#endif
        mercury_instance_t *comm_instance,
        bake_instance_t **bake_instance);
 
// Obtain identifying information for a bake instance through the provided
// mercury address. Registers operation forwards and callbacks through the
// provided mercury instance.
// TODO: options for initializing multiple targets on the same node?
bake_return_t bake_probe_instance(
        mercury_instance_t *comm_instance,
        mercury_address_t *dest,
        bake_instance_t **target,
        bake_request_t *req);
 
// Finalize a bake instance
bake_return_t bake_finalize(bake_instance_t *instance);
 
/// ==== Bulk operations ====
 
// Opaque handle for a bulk region.
// Regions are independent blobs of data and have the following semantics:
// - regions are first created. Regions can be bounded-size or
//   unbounded-size. Bounding allows the implementation to take various
//   optimization shortcuts. After creation, the regions are considered
//   "open"
// - open regions may be written to. Concurrent writes into the
//   region are sequentially consistent iff they are non-overlapping. There are
//   no durability gurantees for open regions.
// - after writing to a region is finished, the region is persisted, putting it
//   in a closed state. The region will no longer service writes, but can
//   service reads
// - deletion of regions is accomplished in the following manner. Regions are
//   deprecated, marking those regions for deletion at some point in the
//   future. Deprecated regions may still service read requests. Regions can
//   only be deprecated if they are in a closed state. Regions are removed from
//   the namespace and possibly deleted during an explicit garbage collection
//   call.
//
// TODO: determine interaction between bulk regions and kvs, particularly
// w.r.t. index management and container movement between targets
// TODO: define serialization semantics, sharing of regions (if at all)
typedef struct bake_bulk_region_t bake_bulk_region_t;
 
// Unique integer identifier corresponding to a bulk region. These IDs may be
// used to lookup bulk regions.
//
// NOTE: the difference between a region_t and a region_id_t is that
// the region_t may store under the hood storage information (file
// descriptor, offset/size, etc.) while a region ID is a simple, "stash-able"
// quantity. It may not be necessary to make this distinction, however.
typedef uint64_t bake_bulk_region_id_t;
 
// Create a bounded bulk data region for writing, storing a handle to the
// resulting region in *region.
//
// After creation, regions are considered "open", and writes can be
// performed. Reads cannot be performed until the region is
// persisted.
bake_return_t bake_bulk_create(
        bake_instance_t *target,
        uint64_t region_size,
        bake_bulk_region_t **region,
        bake_request_t *req);
 
// Performs a write into the bulk region, updating the file pointer.
// No guarantees on data persistence at this point, though mercury RPCs/bulk
// transfers may be issued.
//
// In the case of bounded regions,
// out of bounds writes will be detected at the client and fail.
bake_return_t bake_bulk_write(
        bake_bulk_region_t *region,
        uint64_t region_offset,
        void const *buf,
        uint64_t buf_size,
        bake_request_t *req);
// TODO: write variations: writev, etc. (should we have an implicit "file
// pointer"?)
 
// Persist a bulk region. The region is considered immutable at this point and
// reads may be performed on the region
bake_return_t bake_bulk_persist(
        bake_bulk_region_t *region,
        bake_request_t *req);
 
// Read from a bulk region. Reads cannot occur for open regions.
// The read_len output parameter returns the actual data read, in the case of
// short reads
bake_return_t bake_bulk_read(
        bake_bulk_region_t const *region,
        uint64_t region_offset,
        void * buf,
        uint64_t buf_size,
        uint64_t *read_len,
        bake_request_t *req);
// TODO: read variations - readv, etc. (should we have an implicit "file
// pointer"?)
 
// Obtain a unique ID corresponding to the provided region that can later be searched
bake_return_t bake_bulk_region_get_id(
        bake_bulk_region_t const *region,
        bake_bulk_region_id_t *id);
 
// Look up a region from a target given its unique ID
bake_return_t bake_bulk_region_lookup(
        bake_instance_t *target,
        bake_bulk_region_t **region,
        bake_bulk_region_id_t const *id,
        bake_request_t *req);
// TODO: batch versions of lookup, lookup+read version to cut down on RPC
// round-trips
 
// Free a region handle (not the underlying region)
void bake_bulk_region_free(bake_bulk_region_t *region);
 
// Mark a bulk region for future removal from the store via garbage collection.
bake_return_t bake_bulk_region_deprecate(
        bake_bulk_region_t *region,
        bake_request_t *req);
 
// Garbage-collect the bulk store and de-register deprecated regions from their
// associated containers. After this point, deprecated regions will not be
// visible to callers.
//
// TODO: there will most certainly be synchronization constraints here having
// to do with creating/persisting new regions, deprecating existing regions,
// and exporting containers. We'll need to define them explicitly at some point.
bake_return_t bake_bulk_gc(
        bake_instance_t *target,
        bake_request_t *req);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_BULK_H */

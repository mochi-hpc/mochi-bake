/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_SERVER_H
#define __BAKE_SERVER_H

#include <abt-io.h>
#include <margo.h>
#include <libpmemobj.h>
#include <bake.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAKE_PROVIDER_ID_DEFAULT 0
#define BAKE_PROVIDER_IGNORE     NULL

typedef struct bake_provider* bake_provider_t;

/**
 * The bake_provider_init_info structure can be passed in to the
 * bake_provider_register() function to configure the provider. The struct
 * can be memset to zero to use default values.
 */
struct bake_provider_init_info {
    const char* json_config; /* optional JSON-formatted string */
    ABT_pool    rpc_pool;    /* optional pool on which to run RPC handlers */
    abt_io_instance_id aid; /* optional abt-io instance, used by file backend */
    void*              remi_provider; /* optional REMI provider */
    void*              remi_client;   /* optional REMI client */
};

#define BAKE_PROVIDER_INIT_INFO_INITIALIZER                   \
    {                                                         \
        NULL, ABT_POOL_NULL, ABT_IO_INSTANCE_NULL, NULL, NULL \
    }

/**
 * Initializes a BAKE provider.
 *
 * @param[in] mid Margo instance identifier
 * @param[in] provider_id provider id
 * @param[in] pool Pool on which to run the RPC handlers
 * @param[in] target_name path to PMEM backend file
 * @param[out] provider resulting provider
 * @returns 0 on success, -1 otherwise
 */
int bake_provider_register(margo_instance_id                     mid,
                           uint16_t                              provider_id,
                           const struct bake_provider_init_info* args,
                           bake_provider_t*                      provider);

/**
 * @brief Deregisters the provider.
 *
 * @param provider Provider to deregister.
 *
 * @return 0 on success, -1 otherwise.
 */
int bake_provider_deregister(bake_provider_t provider);

/**
 * Makes the provider start managing a target.  The target must have already
 * been created in the past.
 *
 * @param provider Bake provider
 * @param target_name path to pmem target
 * @param target_id resulting id identifying the target
 *
 * @return BAKE_SUCCESS or BAKE_ERR*
 */
int bake_provider_attach_target(bake_provider_t   provider,
                                const char*       target_name,
                                bake_target_id_t* target_id);

/**
 * Create a new target that did not yet exist and begin managing it.
 *
 * @param provider Bake provider
 * @param target_name path to pmem target
 * @param[in] size size of the created target (may be ignored for target
 * types that can be extended or use a fixed size physical device)
 * @param target_id resulting id identifying the target
 *
 * @return BAKE_SUCCESS or BAKE_ERR*
 */
int bake_provider_create_target(bake_provider_t   provider,
                                const char*       target_name,
                                size_t            size,
                                bake_target_id_t* target_id);

/**
 * Makes the provider stop managing a target.
 *
 * @param provider Bake provider
 * @param target_id id of the target to remove
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_detach_target(bake_provider_t  provider,
                                bake_target_id_t target_id);

/**
 * Removes all the targets associated with a provider.
 *
 * @param provider Bake provider
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_detach_all_targets(bake_provider_t provider);

/**
 * Returns the number of targets that this provider manages.
 *
 * @param provider Bake provider
 * @param num_targets resulting number of targets
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_count_targets(bake_provider_t provider,
                                uint64_t*       num_targets);

/**
 * List the target ids of the targets managed by this provider.
 * The targets array must be pre-allocated with at least enough
 * space to hold all the targets (use bake_provider_count_targets
 * to know how many storage targets are managed).
 *
 * @param provider Bake provider
 * @param targets resulting targer ids
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_list_targets(bake_provider_t   provider,
                               bake_target_id_t* targets);

/**
 * Retrieves complete configuration of bake provider, encoded as json
 *
 * @param [in] provider bake provider
 * @returns null terminated string that must be free'd by caller
 */
char* bake_provider_get_config(bake_provider_t provider);

/**
 * Creates a raw storage target, not connected to a provider.  This would
 * mainly be used by external utilities, not a server daemon itself.
 *
 * @param[in] path path to storage target (could be a file, directory, or
 * device depending on the backend type)
 * @param[in] size size of the created target (may be ignored for target
 * types that can be extended or use a fixed size physical device)
 *
 * @returns 0 on success, -1 otherwise
 */
int bake_create_raw_target(const char* path, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_SERVER_H */

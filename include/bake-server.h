/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_SERVER_H
#define __BAKE_SERVER_H

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
 * Creates a BAKE pool to use for backend PMEM storage.
 *
 * NOTE: This function must be called on a pool before the pool
 * can be passed to 'bake_provider_register'.
 *
 * @param[in] pool_name path to PMEM backend file
 * @param[in] pool_size size of the created pool
 * @returns 0 on success, -1 otherwise
 */
int bake_makepool(const char* pool_name, size_t pool_size);

/**
 * The bake_provider_init_info structure can be passed in to the
 * bake_provider_register() function to configure the provider. The struct
 * can be memset to zero to use default values.
 */
struct bake_provider_init_info {
    const char* json_config; /* JSON-formatted string */
    ABT_pool    rpc_pool;    /* pool on which to run RPC handlers */
};

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
 * Makes the provider start managing a target.
 * The target must have been previously created with bake_makepool,
 * and it should not be managed by another provider (whether in this
 * proccess or another).
 *
 * @param provider Bake provider
 * @param target_name path to pmem target
 * @param target_id resulting id identifying the target
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_attach_target(bake_provider_t   provider,
                                const char*       target_name,
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

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_SERVER_H */

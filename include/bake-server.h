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

#define BAKE_ABT_POOL_DEFAULT ABT_POOL_NULL
#define BAKE_PROVIDER_ID_DEFAULT 0
#define BAKE_PROVIDER_IGNORE NULL

typedef struct bake_provider* bake_provider_t;

/**
 * Creates a BAKE pool to use for backend PMEM storage.
 * 
 * NOTE: This function must be called on a pool before the pool
 * can be passed to 'bake_provider_register'.
 *
 * @param[in] pool_name path to PMEM backend file
 * @param[in] pool_size size of the created pool
 * @param[in] pool_mode mode of the created pool
 * @returns 0 on success, -1 otherwise
 */
int bake_makepool(
    const char *pool_name,
    size_t pool_size,
    mode_t pool_mode);

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
int bake_provider_register(
        margo_instance_id mid,
        uint16_t provider_id,
        ABT_pool pool,
        bake_provider_t* provider);

/**
 * @brief Deregisters and destroys the provider.
 *
 * @param provider Provider to deregister and destroy.
 *
 * @return 0 on success, -1 otherwise.
 */
int bake_provider_destroy(
        bake_provider_t provider);

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
int bake_provider_add_storage_target(
        bake_provider_t provider,
        const char *target_name,
        bake_target_id_t* target_id);

/**
 * Makes the provider stop managing a target.
 *
 * @param provider Bake provider
 * @param target_id id of the target to remove
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_remove_storage_target(
        bake_provider_t provider,
        bake_target_id_t target_id);

/**
 * Removes all the targets associated with a provider.
 *
 * @param provider Bake provider
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_remove_all_storage_targets(
        bake_provider_t provider);

/**
 * Returns the number of targets that this provider manages.
 *
 * @param provider Bake provider
 * @param num_targets resulting number of targets
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_count_storage_targets(
        bake_provider_t provider,
        uint64_t* num_targets);

/**
 * List the target ids of the targets managed by this provider.
 * The targets array must be pre-allocated with at least enough
 * space to hold all the targets (use bake_provider_count_storage_targets
 * to know how many storage targets are managed).
 *
 * @param provider Bake provider
 * @param targets resulting targer ids
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_list_storage_targets(
        bake_provider_t provider,
        bake_target_id_t* targets);

/* TODO: the following configuration management functions would ideally be
 * split off into a dedicated component.  Treating this as a prototype for
 * now.
 */

/**
 * @brief Set configuration parameters as string key/value pairs
 *
 * @param provider Bake provider
 * @param key Configuration key
 * @param value Configuratiion value
 *
 * @return 0 on success, -1 on failure
 */
int bake_provider_set_conf(
        bake_provider_t provider,
        const char *key,
        const char *value);

/**
 * @brief Set configuration parameters for a target.
 *
 * @param provider Bake provider
 * @param tid Bake target id
 * @param key Configuration key
 * @param value Configuration value
 *
 * @return 0 on success, -1 on failure
 */
int bake_target_set_conf(
        bake_provider_t provider,
        bake_target_id_t tid,
        const char* key,
        const char* value);

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_SERVER_H */

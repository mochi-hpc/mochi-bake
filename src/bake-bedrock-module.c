/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <string.h>
#include <bedrock/module.h>
#include <abt-io.h>

#include "bake-server.h"
#include "bake-client.h"
#include "bake-macros.h"

static int bake_register_provider(bedrock_args_t             args,
                                  bedrock_module_provider_t* provider)
{
    int                            ret;
    struct bake_provider_init_info bpargs = {0};
    margo_instance_id              mid = bedrock_args_get_margo_instance(args);
    uint16_t    provider_id            = bedrock_args_get_provider_id(args);
    ABT_pool    pool                   = bedrock_args_get_pool(args);
    const char* config                 = bedrock_args_get_config(args);
    const char* name                   = bedrock_args_get_name(args);

    if (bedrock_args_get_num_dependencies(args, "abt_io")) {
        bpargs.aid = bedrock_args_get_dependency(args, "abt_io", 0);
    } else {
        bpargs.aid = ABT_IO_INSTANCE_NULL;
    }

    if (bedrock_args_get_num_dependencies(args, "remi_provider")) {
        bpargs.remi_provider
            = bedrock_args_get_dependency(args, "remi_provider", 0);
    } else {
        bpargs.remi_provider = NULL;
    }

    if (bedrock_args_get_num_dependencies(args, "remi_client")) {
        bpargs.remi_client
            = bedrock_args_get_dependency(args, "remi_client", 0);
    } else {
        bpargs.remi_client = NULL;
    }

    BAKE_TRACE(mid, "bake_register_provider()");
    BAKE_TRACE(mid, " -> mid           = %p", (void*)mid);
    BAKE_TRACE(mid, " -> provider id   = %d", provider_id);
    BAKE_TRACE(mid, " -> pool          = %p", (void*)pool);
    BAKE_TRACE(mid, " -> config        = %s", config);
    BAKE_TRACE(mid, " -> name          = %s", name);
    BAKE_TRACE(mid, " -> abt_io        = %p", bpargs.aid);
    BAKE_TRACE(mid, " -> remi_provider = %p", bpargs.remi_provider);
    BAKE_TRACE(mid, " -> remi_client   = %p", bpargs.remi_client);

    bpargs.json_config = config;
    ret                = bake_provider_register(mid, provider_id, &bpargs,
                                 (bake_provider_t*)provider);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

static int bake_deregister_provider(bedrock_module_provider_t provider)
{
    int ret;

    ret = bake_provider_deregister(provider);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

static char* bake_get_provider_config(bedrock_module_provider_t provider)
{
    return (bake_provider_get_config(provider));
}

static int bake_init_client(bedrock_args_t           args,
                            bedrock_module_client_t* client)
{
    int ret;

    margo_instance_id mid = bedrock_args_get_margo_instance(args);
    BAKE_TRACE(mid, "bake_init_client()");

    ret = bake_client_init(mid, (bake_client_t*)client);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

static int bake_finalize_client(bedrock_module_client_t client)
{
    int ret;

    ret = bake_client_finalize(client);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

static char* bake_get_client_config(bedrock_module_client_t client)
{
    return strdup("{}");
}

static int bake_create_provider_handle(bedrock_module_client_t client,
                                       hg_addr_t               address,
                                       uint16_t                provider_id,
                                       bedrock_module_provider_handle_t* ph)
{
    int ret;

    ret = bake_provider_handle_create(client, address, provider_id,
                                      (bake_provider_handle_t*)ph);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

static int bake_destroy_provider_handle(bedrock_module_provider_handle_t ph)
{
    int ret;

    ret = bake_provider_handle_release(ph);
    if (ret < 0) return (-1);

    return BEDROCK_SUCCESS;
}

/* an optional abt-io dependency can be specified
 * - only used by some backends (the file one, specifically)
 * - if needed by not provided as a dependency, then the backend will create
 *   one of it's own implicitly
 */
struct bedrock_dependency bake_provider_deps[4]
    = {{"abt_io", "abt_io", 0},
       {"remi_provider", "remi", 0},
       {"remi_client", "remi", 0},
       BEDROCK_NO_MORE_DEPENDENCIES};

struct bedrock_dependency bake_client_deps[1] = {BEDROCK_NO_MORE_DEPENDENCIES};

static struct bedrock_module bake
    = {.register_provider       = bake_register_provider,
       .deregister_provider     = bake_deregister_provider,
       .get_provider_config     = bake_get_provider_config,
       .init_client             = bake_init_client,
       .finalize_client         = bake_finalize_client,
       .get_client_config       = bake_get_client_config,
       .create_provider_handle  = bake_create_provider_handle,
       .destroy_provider_handle = bake_destroy_provider_handle,
       .provider_dependencies   = bake_provider_deps,
       .client_dependencies     = bake_client_deps};

BEDROCK_REGISTER_MODULE(bake, bake)

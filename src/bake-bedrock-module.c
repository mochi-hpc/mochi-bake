/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <bedrock/module.h>
#include <string.h>

#include "bake-server.h"
#include "bake-client.h"

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

    BAKE_TRACE(mid, "bake_register_provider()");
    BAKE_INFO(mid, " -> mid         = %p", (void*)mid);
    BAKE_INFO(mid, " -> provider id = %d", provider_id);
    BAKE_INFO(mid, " -> pool        = %p", (void*)pool);
    BAKE_INFO(mid, " -> config      = %s", config);
    BAKE_INFO(mid, " -> name        = %s", name);

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

static int bake_init_client(margo_instance_id        mid,
                            bedrock_module_client_t* client)
{
    int ret;

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

static struct bedrock_module bake
    = {.register_provider       = bake_register_provider,
       .deregister_provider     = bake_deregister_provider,
       .get_provider_config     = bake_get_provider_config,
       .init_client             = bake_init_client,
       .finalize_client         = bake_finalize_client,
       .create_provider_handle  = bake_create_provider_handle,
       .destroy_provider_handle = bake_destroy_provider_handle,
       .dependencies            = NULL};

BEDROCK_REGISTER_MODULE(bake, bake)

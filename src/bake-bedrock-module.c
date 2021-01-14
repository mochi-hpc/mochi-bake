/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <bedrock/module.h>
#include <string.h>

static int bake_register_provider(bedrock_args_t             args,
                                  bedrock_module_provider_t* provider)
{
    margo_instance_id mid         = bedrock_args_get_margo_instance(args);
    uint16_t          provider_id = bedrock_args_get_provider_id(args);
    ABT_pool          pool        = bedrock_args_get_pool(args);
    const char*       config      = bedrock_args_get_config(args);
    const char*       name        = bedrock_args_get_name(args);

    *provider = strdup("bake:provider");
    printf("Registered a provider from bake\n");
    printf(" -> mid         = %p\n", (void*)mid);
    printf(" -> provider id = %d\n", provider_id);
    printf(" -> pool        = %p\n", (void*)pool);
    printf(" -> config      = %s\n", config);
    printf(" -> name        = %s\n", name);
    return BEDROCK_SUCCESS;
}

static int bake_deregister_provider(bedrock_module_provider_t provider)
{
    free(provider);
    printf("Deregistered a provider from bake\n");
    return BEDROCK_SUCCESS;
}

static char* bake_get_provider_config(bedrock_module_provider_t provider)
{
    (void)provider;
    return strdup("{}");
}

static int bake_init_client(margo_instance_id        mid,
                            bedrock_module_client_t* client)
{
    *client = strdup("bake:client");
    printf("Registered a client from bake\n");
    printf(" -> mid = %p\n", (void*)mid);
    return BEDROCK_SUCCESS;
}

static int bake_finalize_client(bedrock_module_client_t client)
{
    free(client);
    printf("Finalized a client from bake\n");
    return BEDROCK_SUCCESS;
}

static int bake_create_provider_handle(bedrock_module_client_t client,
                                       hg_addr_t               address,
                                       uint16_t                provider_id,
                                       bedrock_module_provider_handle_t* ph)
{
    (void)client;
    (void)address;
    (void)provider_id;
    *ph = strdup("bake:provider_handle");
    printf("Created provider handle from bake\n");
    return BEDROCK_SUCCESS;
}

static int bake_destroy_provider_handle(bedrock_module_provider_handle_t ph)
{
    free(ph);
    printf("Destroyed provider handle from bake\n");
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

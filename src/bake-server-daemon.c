/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <margo.h>
#include <mochi-cfg.h>
#include <bake-server.h>

/* NOTE: in the following we don't specify all of the sub-component
 * parameters; just things that might be overridden by command line or are
 * mandatory for this daemon.
 *
 * "bake_providers" is an array to support multiple providers, each possibly
 * has it's own configuration
 *
 * "targets" is also an array to support multiple targets per provider
 */
# define BAKE_SERVER_DAEMON_DEFAULT_CFG \
"{   \"bake-server-daemon\": {" \
"       \"host_file\": \"\"," \
"       \"bake_providers\": []," \
"       \"margo\": {" \
"             \"mercury\": {" \
"                  \"addr_str\": \"na+sm://\"," \
"                  \"server_mode\": 1" \
"}}}}"

typedef enum {
    MODE_TARGETS   = 0,
    MODE_PROVIDERS = 1
} mplex_mode_t;

struct options
{
    char *json_input;
    char *listen_addr_str;
    unsigned num_pools;
    char **bake_pools;
    char *host_file;
    int pipeline_enabled;
    mplex_mode_t mplex_mode;
};

#define MAX_PROVIDERS 256
static bake_provider_t g_provider_array[MAX_PROVIDERS];
static int g_provider_array_size = 0;

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: bake-server-daemon [OPTIONS] <bake_pool1> <bake_pool2> ...\n");
    fprintf(stderr, "       bake_pool is the path to the BAKE pool\n");
    fprintf(stderr, "           (prepend pmem: or file: to specify backend format)\n");
    fprintf(stderr, "       [-l listen_addr] is the Mercury address to listen on\n");
    fprintf(stderr, "       [-f filename] to write the server address to a file\n");
    fprintf(stderr, "       [-m mode] multiplexing mode (providers or targets) for managing multiple pools (default is targets)\n");
    fprintf(stderr, "       [-p] enable pipelining\n");
    fprintf(stderr, "       [-j filename] json configuration file \n");
    fprintf(stderr, "Example: ./bake-server-daemon tcp://localhost:1234 /dev/shm/foo.dat /dev/shm/bar.dat\n");
    return;
}

static json_t* resolve_json(struct options *opts)
{
    json_t* cfg;
    json_t* margo_cfg;
    json_t* hg_cfg;
    json_t* prov_array;
    json_t* prov_cfg;
    json_t* target_array;
    int i;

    /* Concept: we have 3 sources of configuration parameters to consider,
     * in order of precedence:
     * - explicit command line arguments
     * - json configuration file
     * - default parameters
     *
     * We accomplish this by first parsing config file (if present) and
     * setting remaining defaults.  Then we explicitly set extra parameters
     * that may have been set on command line.
     */

    /* read json input config file and populate missing defaults */
    if(opts->json_input)
        cfg = mochi_cfg_get_component_file(opts->json_input, "bake-server-daemon", BAKE_SERVER_DAEMON_DEFAULT_CFG);
    else
        cfg = mochi_cfg_get_component("{\"bake-server-daemon\":{}}", "bake-server-daemon", BAKE_SERVER_DAEMON_DEFAULT_CFG);

    /* set parameters from command line */
    if(opts->listen_addr_str)
    {
        mochi_cfg_get_object(cfg, "margo", &margo_cfg);
        mochi_cfg_get_object(margo_cfg, "mercury", &hg_cfg);
        mochi_cfg_set_value_string(hg_cfg, "addr_str", opts->listen_addr_str);
    }
    if(opts->host_file)
        mochi_cfg_set_value_string(cfg, "host_file", opts->host_file);

    /* construct json to represent provider and target hierarchy */
    /* If mplex_mode is "TARGETS", then there is one provider with N
     * targets.  If mplex_mode is "PROVIDERS", then there are N providers
     * each with one target.
     * NOTE: we just append here; there is no effort to determine if command
     * line arguments have duplicated something that was already in the json
     * NOTE: we also only apply the -p pipelining argument to
     * providers that are specified on the command line.
     */
    /* TODO: error handling below */
    prov_array = json_object_get(cfg, "bake_providers");
    if(opts->mplex_mode == MODE_TARGETS)
    {
        prov_cfg = json_object();
        target_array = json_array();
        for(i=0; i<opts->num_pools; i++)
        {
            json_array_append(target_array, json_string(opts->bake_pools[i]));
        }
        json_object_set(prov_cfg, "targets", target_array);
        json_object_set(prov_cfg, "pipeline_enable", json_integer(opts->pipeline_enabled));
        json_array_append(prov_array, prov_cfg);
    }
    else
    {
        for(i=0; i<opts->num_pools; i++)
        {
            prov_cfg = json_object();
            target_array = json_array();
            json_array_append(target_array, json_string(opts->bake_pools[i]));
            json_object_set(prov_cfg, "targets", target_array);
            json_object_set(prov_cfg, "pipeline_enable", json_integer(opts->pipeline_enabled));
            json_array_append(prov_array, prov_cfg);
        }
    }

    return(cfg);
}

static void parse_args(int argc, char **argv, struct options *opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    /* get options */
    while((opt = getopt(argc, argv, "l:j:f:m:p")) != -1)
    {
        switch(opt)
        {
            case 'l':
                opts->listen_addr_str = optarg;
                break;
            case 'f':
                opts->host_file = optarg;
                break;
            case 'j':
                opts->json_input = optarg;
                break;
            case 'm':
                if(0 == strcmp(optarg, "targets"))
                    opts->mplex_mode = MODE_TARGETS;
                else if(0 == strcmp(optarg, "providers"))
                    opts->mplex_mode = MODE_PROVIDERS;
                else {
                    fprintf(stderr, "Unrecognized multiplexing mode \"%s\"\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'p':
                opts->pipeline_enabled = 1;
                break;
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    /* get required arguments after options */
    if((argc - optind) < 1)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->num_pools = argc - optind;
    opts->bake_pools = calloc(opts->num_pools, sizeof(char*));
    int i;
    for(i=0; i < opts->num_pools; i++) {
        opts->bake_pools[i] = argv[optind++];
    }

    return;
}

int main(int argc, char **argv)
{
    struct options opts;
    margo_instance_id mid;
    int ret;
    json_t* cfg = NULL;
    json_t* margo_cfg = NULL;
    char *cfg_str;
    char *margo_cfg_str;
    const char *host_file;
    size_t prov_index, target_index;
    json_t *prov_value, *target_value;
    char *prov_cfg_string;
    int i;

    parse_args(argc, argv, &opts);

    cfg = resolve_json(&opts);
    if(!cfg)
    {
        fprintf(stderr, "Error: unable to resolve json and command line arguments.\n");
        return(-1);
    }

    /* start margo */
    mochi_cfg_get_object(cfg, "margo", &margo_cfg);
    cfg_str = mochi_cfg_emit(margo_cfg, "margo");
    mid = margo_init_json(cfg_str);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        return(-1);
    }
    free(cfg_str);

    margo_enable_remote_shutdown(mid);

    mochi_cfg_get_value_string(cfg, "host_file", &host_file);
    if(host_file && strlen(host_file))
    {
        /* write the server address to file if requested */
        FILE *fp;
        hg_addr_t self_addr;
        char self_addr_str[128];
        hg_size_t self_addr_str_sz = 128;
        hg_return_t hret;

        /* figure out what address this server is listening on */
        hret = margo_addr_self(mid, &self_addr);
        if(hret != HG_SUCCESS)
        {
            fprintf(stderr, "Error: margo_addr_self()\n");
            margo_finalize(mid);
            return(-1);
        }
        hret = margo_addr_to_string(mid, self_addr_str, &self_addr_str_sz, self_addr);
        if(hret != HG_SUCCESS)
        {
            fprintf(stderr, "Error: margo_addr_to_string()\n");
            margo_addr_free(mid, self_addr);
            margo_finalize(mid);
            return(-1);
        }
        margo_addr_free(mid, self_addr);

        fp = fopen(host_file, "w");
        if(!fp)
        {
            perror("fopen");
            margo_finalize(mid);
            return(-1);
        }

        fprintf(fp, "%s", self_addr_str);
        fclose(fp);
    }

    /* TODO: error handling? */
    json_array_foreach(
    json_object_get(cfg, "bake_providers"), prov_index, prov_value)
    {
        bake_target_id_t tid;

        prov_cfg_string = mochi_cfg_emit(prov_value, NULL);
        if(g_provider_array_size == MAX_PROVIDERS)
        {
            fprintf(stderr, "Error: hit provider limit of %d\n", MAX_PROVIDERS);
            margo_finalize(mid);
            return(-1);
        }
        ret = bake_provider_register_json(mid, prov_index+1,
            BAKE_ABT_POOL_DEFAULT,
            &g_provider_array[g_provider_array_size],
            prov_cfg_string);
        if(ret != 0)
        {
            bake_perror( "Error: bake_provider_register_json()", ret);
            margo_finalize(mid);
            return(-1);
        }
        free(prov_cfg_string);

        json_array_foreach(
        json_object_get(prov_value, "targets"), target_index, target_value)
        {
            ret = bake_provider_add_storage_target(
                g_provider_array[g_provider_array_size], json_string_value(target_value), &tid);
            if(ret != 0)
            {
                bake_perror("Error: bake_provider_add_storage_target()", ret);
                margo_finalize(mid);
                return(-1);
            }

            printf("Provider %lu managing new target %s at multiplex id %lu\n", prov_index, json_string_value(target_value), prov_index+1);
        }
        g_provider_array_size++;
    }

    /* TODO: bundle the following stuff into a helper function so that it
     * could be re-used in other situations
     */

    /* update top level json with current runtime settings from margo */
    margo_cfg_str = margo_get_config(mid);
    ret = mochi_cfg_set_object_by_string(cfg, "margo", margo_cfg_str);
    free(margo_cfg_str);

    /* iterate through providers and populate json with current state */
    json_array_clear(json_object_get(cfg, "bake_providers"));
    for(i=0; i<g_provider_array_size; i++)
    {
        char* prov_cfg_string = bake_provider_get_config(g_provider_array[i]);
        if(prov_cfg_string)
        {
            mochi_cfg_append_array_by_string(cfg, "bake_providers", prov_cfg_string);
            free(prov_cfg_string);
        }
    }

    /* display full json for the daemon */
    cfg_str = mochi_cfg_emit(cfg, "bake-server-daemon");
    printf("%s\n", cfg_str);
    free(cfg_str);

    /* suspend until the BAKE server gets a shutdown signal from the client */
    margo_wait_for_finalize(mid);

    free(opts.bake_pools);

    return(0);
}

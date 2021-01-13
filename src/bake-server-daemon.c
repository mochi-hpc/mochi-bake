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
#include <bake-server.h>

typedef enum
{
    MODE_TARGETS   = 0,
    MODE_PROVIDERS = 1
} mplex_mode_t;

struct options {
    char*        listen_addr_str;
    unsigned     num_pools;
    char**       bake_pools;
    char*        host_file;
    int          pipeline_enabled;
    mplex_mode_t mplex_mode;
};

static void usage(int argc, char** argv)
{
    fprintf(stderr,
            "Usage: bake-server-daemon [OPTIONS] <listen_addr> <bake_pool1> "
            "<bake_pool2> ...\n");
    fprintf(stderr, "       listen_addr is the Mercury address to listen on\n");
    fprintf(stderr, "       bake_pool is the path to the BAKE pool\n");
    fprintf(stderr,
            "           (prepend pmem: or file: to specify backend format)\n");
    fprintf(stderr,
            "       [-f filename] to write the server address to a file\n");
    fprintf(stderr,
            "       [-m mode] multiplexing mode (providers or targets) for "
            "managing multiple pools (default is targets)\n");
    fprintf(stderr, "       [-p] enable pipelining\n");
    fprintf(stderr,
            "Example: ./bake-server-daemon tcp://localhost:1234 "
            "/dev/shm/foo.dat /dev/shm/bar.dat\n");
    return;
}

static void parse_args(int argc, char** argv, struct options* opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    /* get options */
    while ((opt = getopt(argc, argv, "f:m:p")) != -1) {
        switch (opt) {
        case 'f':
            opts->host_file = optarg;
            break;
        case 'm':
            if (0 == strcmp(optarg, "targets"))
                opts->mplex_mode = MODE_TARGETS;
            else if (0 == strcmp(optarg, "providers"))
                opts->mplex_mode = MODE_PROVIDERS;
            else {
                fprintf(stderr, "Unrecognized multiplexing mode \"%s\"\n",
                        optarg);
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
    if ((argc - optind) < 2) {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->num_pools       = argc - optind - 1;
    opts->listen_addr_str = argv[optind++];
    opts->bake_pools      = calloc(opts->num_pools, sizeof(char*));
    int i;
    for (i = 0; i < opts->num_pools; i++) {
        opts->bake_pools[i] = argv[optind++];
    }

    return;
}

int main(int argc, char** argv)
{
    struct options    opts;
    margo_instance_id mid;
    int               ret;

    parse_args(argc, argv, &opts);

    /* start margo */
    /* use the main xstream for driving progress and executing rpc handlers */
    mid = margo_init(opts.listen_addr_str, MARGO_SERVER_MODE, 0, -1);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }

    margo_enable_remote_shutdown(mid);

    if (opts.host_file) {
        /* write the server address to file if requested */
        FILE*       fp;
        hg_addr_t   self_addr;
        char        self_addr_str[128];
        hg_size_t   self_addr_str_sz = 128;
        hg_return_t hret;

        /* figure out what address this server is listening on */
        hret = margo_addr_self(mid, &self_addr);
        if (hret != HG_SUCCESS) {
            fprintf(stderr, "Error: margo_addr_self()\n");
            margo_finalize(mid);
            return (-1);
        }
        hret = margo_addr_to_string(mid, self_addr_str, &self_addr_str_sz,
                                    self_addr);
        if (hret != HG_SUCCESS) {
            fprintf(stderr, "Error: margo_addr_to_string()\n");
            margo_addr_free(mid, self_addr);
            margo_finalize(mid);
            return (-1);
        }
        margo_addr_free(mid, self_addr);

        fp = fopen(opts.host_file, "w");
        if (!fp) {
            perror("fopen");
            margo_finalize(mid);
            return (-1);
        }

        fprintf(fp, "%s", self_addr_str);
        fclose(fp);
    }

    /* initialize the BAKE server */
    if (opts.mplex_mode == MODE_PROVIDERS) {
        int i;
        for (i = 0; i < opts.num_pools; i++) {
            bake_provider_t  provider;
            bake_target_id_t tid;
            ret = bake_provider_register(mid, i + 1, BAKE_ABT_POOL_DEFAULT,
                                         &provider);

            if (ret != 0) {
                bake_perror("Error: bake_provider_register()", ret);
                margo_finalize(mid);
                return (-1);
            }

            if (opts.pipeline_enabled)
                bake_provider_set_conf(provider, "pipeline_enabled", "1");

            ret = bake_provider_add_storage_target(provider, opts.bake_pools[i],
                                                   &tid);

            if (ret != 0) {
                bake_perror("Error: bake_provider_add_storage_target()", ret);
                margo_finalize(mid);
                return (-1);
            }

            printf("Provider %d managing new target at multiplex id %d\n", i,
                   i + 1);
        }

    } else {

        int             i;
        bake_provider_t provider;
        ret = bake_provider_register(mid, 1, BAKE_ABT_POOL_DEFAULT, &provider);

        if (ret != 0) {
            bake_perror("Error: bake_provider_register()", ret);
            margo_finalize(mid);
            return (-1);
        }

        if (opts.pipeline_enabled)
            bake_provider_set_conf(provider, "pipeline_enabled", "1");

        for (i = 0; i < opts.num_pools; i++) {
            bake_target_id_t tid;
            ret = bake_provider_add_storage_target(provider, opts.bake_pools[i],
                                                   &tid);

            if (ret != 0) {
                bake_perror("Error: bake_provider_add_storage_target()", ret);
                margo_finalize(mid);
                return (-1);
            }

            printf("Provider 0 managing new target at multiplex id %d\n", 1);
        }
    }

    /* suspend until the BAKE server gets a shutdown signal from the client */
    margo_wait_for_finalize(mid);

    free(opts.bake_pools);

    return (0);
}

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
#include <libpmemobj.h>
#include <bake-server.h>

struct options
{
    char *listen_addr_str;
    char *bake_pool;
    char *host_file;
};

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: bake-server-daemon [OPTIONS] <listen_addr> <bake_pool>\n");
    fprintf(stderr, "       listen_addr is the Mercury address to listen on\n");
    fprintf(stderr, "       bake_pool is the path to the BAKE pool\n");
    fprintf(stderr, "       [-f filename] to write the server address to a file\n");
    fprintf(stderr, "Example: ./bake-server-daemon tcp://localhost:1234 /dev/shm/foo.dat\n");
    return;
}

static void parse_args(int argc, char **argv, struct options *opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    /* get options */
    while((opt = getopt(argc, argv, "f:")) != -1)
    {
        switch(opt)
        {
            case 'f':
                opts->host_file = optarg;
                break;
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    /* get required arguments after options */
    if((argc - optind) != 2)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->listen_addr_str = argv[optind++];
    opts->bake_pool = argv[optind++];

    return;
}

int main(int argc, char **argv) 
{
    struct options opts;
    margo_instance_id mid;
    int ret;

    parse_args(argc, argv, &opts);

    /* start margo */
    /* use the main xstream for driving progress and executing rpc handlers */
    mid = margo_init(opts.listen_addr_str, MARGO_SERVER_MODE, 0, -1);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        return(-1);
    }

    if(opts.host_file)
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

        fp = fopen(opts.host_file, "w");
        if(!fp)
        {
            perror("fopen");
            margo_finalize(mid);
            return(-1);
        }

        fprintf(fp, "%s", self_addr_str);
        fclose(fp);
    }

    /* initialize the BAKE server */
    ret = bake_server_init(mid, opts.bake_pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: bake_server_init()\n");
        margo_finalize(mid);
        return(-1);
    }

    /* suspend until the BAKE server gets a shutdown signal from the client */
    margo_wait_for_finalize(mid);

    return(0);
}

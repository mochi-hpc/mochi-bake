/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <bake-server.h>

struct options
{
    char *pmem_pool;
    size_t pool_size;
    mode_t pool_mode;
};

void usage(int argc, char *argv[])
{
    fprintf(stderr, "Usage: bake-mkpool [OPTIONS] <pmem_pool>\n");
    fprintf(stderr, "       pmem_pool is the path to the pmemobj pool to create\n");
    fprintf(stderr, "       [-s size] create pool file named <pmem_pool> with specified size (K, M, G, etc. suffixes allowed)\n");
    fprintf(stderr, "Example: ./bake-mkpool -s 16M /dev/shm/foo.dat\n");
    fprintf(stderr, "Note: if -s is not specified, then target file must already exist with desired size.\n");
    return;
}

int parse_size(char *str, size_t *size_out)
{
    const char *suffixes[] = { "B", "K", "M", "G", "T", "P" };
    size_t size_mults[] = { 1ULL, 1ULL << 10, 1ULL << 20, 1ULL << 30, 1ULL << 40, 1ULL << 50 };
    size_t size;
    char suff[2] = {0};
    int i;
    int ret;

    ret = sscanf(str, "%zu%2s", &size, suff);
    if(ret == 1)
    {
        *size_out = size;
        return(0);
    }
    else if(ret == 2)
    {
        for(i = 0; i < 6; i++)
        {
            if(strcmp(suffixes[i], suff) == 0)
            {
                *size_out = (size * size_mults[i]);
                return(0);
            }
        }
    }
    return(-1);
}

void parse_args(int argc, char *argv[], struct options *opts)
{
    int opt;
    int ret;

    /* set default options */
    memset(opts, 0, sizeof(*opts));
    opts->pool_mode = 0664;

    /* get options */
    while((opt = getopt(argc, argv, "s:")) != -1)
    {
        switch(opt)
        {
            case 's':
                ret = parse_size(optarg, &opts->pool_size);
                if(ret != 0)
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    /* get required arguments after options */
    if((argc - optind) != 1)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->pmem_pool = argv[optind++];

    return;
}

int main(int argc, char *argv[])
{
    struct options opts;
    int ret;

    parse_args(argc, argv, &opts);

    ret = bake_makepool(opts.pmem_pool, opts.pool_size, opts.pool_mode);

    return(ret);
}

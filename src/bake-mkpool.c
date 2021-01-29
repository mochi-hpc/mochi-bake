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
#include <libpmemobj.h>

#include "bake-backend.h"

extern bake_backend g_bake_pmem_backend;
extern bake_backend g_bake_file_backend;

struct options {
    char*  pmem_pool;
    size_t pool_size;
};

void usage(int argc, char* argv[])
{
    fprintf(stderr, "Usage: bake-mkpool [OPTIONS] <path>\n");
    fprintf(stderr,
            "       path may be a file, directory, or device depending on the "
            "backend.\n");
    fprintf(stderr,
            "           (prepend pmem: or file: to specify backend format)\n");
    fprintf(stderr,
            "       [-s size] create pool file named <pmem_pool> with "
            "specified size (K, M, G, etc. suffixes allowed)\n");
    fprintf(stderr, "Example: ./bake-mkpool -s 16M /dev/shm/foo.dat\n");
    fprintf(stderr,
            "-s may be omitted if backend supports extending space, or if pool "
            "is being created on existing fixed-size device.\n");
    return;
}

int parse_size(char* str, size_t* size_out)
{
    const char* suffixes[] = {"B", "K", "M", "G", "T", "P"};
    size_t      size_mults[]
        = {1ULL, 1ULL << 10, 1ULL << 20, 1ULL << 30, 1ULL << 40, 1ULL << 50};
    size_t size;
    char   suff[2] = {0};
    int    i;
    int    ret;

    ret = sscanf(str, "%zu%1s", &size, suff);
    if (ret == 1) {
        *size_out = size;
        return (0);
    } else if (ret == 2) {
        for (i = 0; i < 6; i++) {
            if (strcmp(suffixes[i], suff) == 0) {
                *size_out = (size * size_mults[i]);
                return (0);
            }
        }
    }
    return (-1);
}

void parse_args(int argc, char* argv[], struct options* opts)
{
    int opt;
    int ret;

    /* set default options */
    memset(opts, 0, sizeof(*opts));

    /* get options */
    while ((opt = getopt(argc, argv, "s:")) != -1) {
        switch (opt) {
        case 's':
            ret = parse_size(optarg, &opts->pool_size);
            if (ret != 0) {
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
    if ((argc - optind) != 1) {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->pmem_pool = argv[optind++];

    return;
}

int main(int argc, char* argv[])
{
    struct options opts;
    int            ret;
    char*          backend_type = NULL;

    parse_args(argc, argv, &opts);

    /* figure out the backend by searching until the ":" in the file name */
    char* tmp = strchr(opts.pmem_pool, ':');
    if (tmp != NULL) {
        backend_type = strdup(opts.pmem_pool);
        backend_type[(unsigned long)(tmp - opts.pmem_pool)] = '\0';
        opts.pmem_pool                                      = tmp + 1;
    } else {
        backend_type = strdup("pmem");
    }

    if (strcmp(backend_type, "pmem") == 0) {
        ret = g_bake_pmem_backend._create_raw_target(opts.pmem_pool,
                                                     opts.pool_size);
    } else if (strcmp(backend_type, "file") == 0) {
        ret = g_bake_file_backend._create_raw_target(opts.pmem_pool,
                                                     opts.pool_size);
    } else {
        fprintf(stderr, "ERROR: unknown backend type \"%s\"\n", backend_type);
        free(backend_type);
        return BAKE_ERR_BACKEND_TYPE;
    }

    free(backend_type);
    return (ret);
}

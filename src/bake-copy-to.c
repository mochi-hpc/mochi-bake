/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "bake-client.h"

/* client program that will copy a POSIX file into a BAKE region */

int main(int argc, char **argv) 
{
    int i;
    char cli_addr_prefix[64] = {0};
    char *svr_addr_str;
    hg_addr_t svr_addr;
    margo_instance_id mid;
    bake_client_t bcl;
    bake_provider_handle_t bph;
    uint64_t num_targets;
    bake_target_id_t bti;
    uint8_t mplex_id;
    hg_return_t hret;
    int ret;
    bake_region_id_t rid;
    int fd;
    struct stat statbuf;
    char* local_region;
    int region_fd;
    char region_file[128];
    uint64_t  check_size;
 
    if(argc != 4)
    {
        fprintf(stderr, "Usage: bake-copy-to <local file> <server addr> <mplex id>\n");
        fprintf(stderr, "  Example: ./bake-copy-to /tmp/foo.dat tcp://localhost:1234 1\n");
        return(-1);
    }
    svr_addr_str = argv[2];
    mplex_id = atoi(argv[3]);

    fd = open(argv[1], O_RDONLY);
    if(fd < 0)
    {
        perror("open");
        return(-1);
    }
    ret = fstat(fd, &statbuf);
    if(ret < 0)
    {
        perror("fstat");
        close(fd);
        return(-1);
    }

    local_region = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(local_region == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return(-1);
    }

    /* initialize Margo using the transport portion of the server
     * address (i.e., the part before the first : character if present)
     */
    for(i=0; (i<63 && svr_addr_str[i] != '\0' && svr_addr_str[i] != ':'); i++)
        cli_addr_prefix[i] = svr_addr_str[i];

    mid = margo_init(cli_addr_prefix, MARGO_CLIENT_MODE, 0, -1);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        munmap(local_region, statbuf.st_size);
        close(fd);
        return -1;
    }

    ret = bake_client_init(mid, &bcl);
    if(ret != 0)
    {
        fprintf(stderr, "Error: bake_client_init()\n");
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        return -1;
    }

    hret = margo_addr_lookup(mid, svr_addr_str, &svr_addr);
    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_lookup()\n");
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        return(-1);
    }

    ret = bake_provider_handle_create(bcl, svr_addr, mplex_id, &bph);
    if(ret != 0)
    {
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_provider_handle_create()\n");
        return(-1);
    }

    ret = bake_probe(bph, 1, &bti, &num_targets);
    if(ret < 0)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_probe()\n");
        return(-1);
    }

    /* create region */
    ret = bake_create(bph, bti, statbuf.st_size, &rid);
    if(ret != 0)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_create()\n");
        return(-1);
    }

    /* transfer data */
    ret = bake_write(
        bph,
        rid,
        0,
        local_region,
        statbuf.st_size);
    if(ret != 0)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_write()\n");
        return(-1);
    }

    munmap(local_region, statbuf.st_size);
    close(fd);

    ret = bake_persist(bph, rid);
    if(ret != 0)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        fprintf(stderr, "Error: bake_persist()\n");
        return(-1);
    }

    /* safety check size */
    ret = bake_get_size(bph, rid, &check_size);
    if(ret != 0)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        fprintf(stderr, "Error: bake_get_size()\n");
        return(-1);
    }
   
    bake_provider_handle_release(bph);
    margo_addr_free(mid, svr_addr);
    bake_client_finalize(bcl);
    margo_finalize(mid);

    if(check_size != statbuf.st_size)
    {
        fprintf(stderr, "Error: size mismatch!\n");
        return(-1);
    }

    sprintf(region_file, "/tmp/bb-copy-rid.XXXXXX");
    region_fd = mkstemp(region_file);
    if(region_fd < 0)
    {
        perror("mkstemp");
    }
    else
    {
        ret = write(region_fd, &rid, sizeof(rid));
        if(ret < 0)
        {
            perror("write");
        }
        else
        {
            printf("RID written to %s\n", region_file);
            close(region_fd);
        }
    }
   
    return(0);
}


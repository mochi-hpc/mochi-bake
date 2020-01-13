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

/* client program that will copy a BAKE region out to a POSIX file */

int main(int argc, char **argv) 
{
    int i;
    char cli_addr_prefix[64] = {0};
    char *svr_addr_str;
    hg_addr_t svr_addr;
    margo_instance_id mid;
    bake_client_t bcl;
    bake_provider_handle_t bph;
    uint8_t mplex_id;
    hg_return_t hret;
    int ret;
    bake_target_id_t tid;
    bake_region_id_t rid;
    int fd;
    char* local_region;
    int region_fd;
    uint64_t size;
    char region_str[128];
 
    if(argc != 6)
    {
        fprintf(stderr, "Usage: bake-copy-from <server addr> <mplex id> <identifier file> <output file> <size>\n");
        fprintf(stderr, "  Example: ./bake-copy-from tcp://localhost:1234 3 /tmp/bb-copy-rid.0GjOlu /tmp/output.dat 256\n");
        return(-1);
    }
    svr_addr_str = argv[1];
    mplex_id = atoi(argv[2]);
    size = atol(argv[5]);

    /* initialize Margo using the transport portion of the server
     * address (i.e., the part before the first : character if present)
     */
    for(i=0; (i<63 && svr_addr_str[i] != '\0' && svr_addr_str[i] != ':'); i++)
        cli_addr_prefix[i] = svr_addr_str[i];

    mid = margo_init(cli_addr_prefix, MARGO_CLIENT_MODE, 0, -1);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        return -1;
    }

    ret = bake_client_init(mid, &bcl);
    if(ret != 0)
    {
        bake_perror( "Error: bake_client_init()", ret);
        margo_finalize(mid);
        return -1;
    }

    hret = margo_addr_lookup(mid, svr_addr_str, &svr_addr);
    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_lookup()\n");
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    ret = bake_provider_handle_create(bcl, svr_addr, mplex_id, &bph);
    if(ret < 0)
    {
        bake_perror("Error: bake_provider_handle_create()", ret);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    region_fd = open(argv[3], O_RDONLY);
    if(region_fd < 0)
    {
        perror("open rid");
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    ret = read(region_fd, &tid, sizeof(tid));
    if(ret != sizeof(tid))
    {
        perror("read");
        close(region_fd);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    ret = read(region_fd, &rid, sizeof(rid));
    if(ret != sizeof(rid))
    {
        perror("read");
        close(region_fd);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }
    close(region_fd);

    bake_print_dbg_region_id_t(region_str, 127, rid);
    printf("# will read bake region %s\n", region_str);

#ifdef USE_SIZECHECK_HEADERS
    uint64_t check_size;
    ret = bake_get_size(bph, tid, rid, &check_size);
    if(ret != 0)
    {
        bake_perror("Error: bake_get_size()", ret);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }
    if(check_size != size) {
        fprintf(stderr, "Error: incorrect size provided\n");
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }
#endif

    fd = open(argv[4], O_RDWR|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR);
    if(fd < 0)
    {
        perror("open output");
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    ret = ftruncate(fd, size);
    if(ret < 0)
    {
        perror("ftruncate");
        close(fd);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    local_region = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
    if(local_region == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /* transfer data */
    uint64_t bytes_read;
    ret = bake_read(
        bph,
        tid,
        rid,
        0,
        local_region,
        size,
        &bytes_read);
    if(ret != 0)
    {
        munmap(local_region, size);
        close(fd);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        bake_perror("Error: bake_read()", ret);
        return(-1);
    }

    munmap(local_region, size);
    close(fd);
    bake_provider_handle_release(bph);
    margo_addr_free(mid, svr_addr);
    bake_client_finalize(bcl);
    margo_finalize(mid);

    return(0);
}


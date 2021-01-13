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

int main(int argc, char** argv)
{
    int                    i;
    char                   cli_addr_prefix[64] = {0};
    char*                  svr_addr_str;
    hg_addr_t              svr_addr;
    margo_instance_id      mid;
    bake_client_t          bcl;
    bake_provider_handle_t bph;
    uint8_t                mplex_id;
    uint32_t               target_number;
    hg_return_t            hret;
    int                    ret;
    bake_region_id_t       rid;
    int                    fd;
    struct stat            statbuf;
    char*                  local_region;
    int                    region_fd;
    char                   region_file[128];
    char                   region_str[128];
#ifdef USE_SIZECHECK_HEADERS
    uint64_t check_size;
#endif

    if (argc != 5) {
        fprintf(stderr,
                "Usage: bake-copy-to <local file> <server addr> <provider id> "
                "<target number>\n");
        fprintf(stderr,
                "  Example: ./bake-copy-to /tmp/foo.dat tcp://localhost:1234 1 "
                "3\n");
        return (-1);
    }
    svr_addr_str  = argv[2];
    mplex_id      = atoi(argv[3]);
    target_number = atoi(argv[4]);

    uint64_t         num_targets;
    bake_target_id_t bti[target_number];

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return (-1);
    }
    ret = fstat(fd, &statbuf);
    if (ret < 0) {
        perror("fstat");
        close(fd);
        return (-1);
    }

    local_region = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (local_region == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return (-1);
    }

    /* initialize Margo using the transport portion of the server
     * address (i.e., the part before the first : character if present)
     */
    for (i = 0; (i < 63 && svr_addr_str[i] != '\0' && svr_addr_str[i] != ':');
         i++)
        cli_addr_prefix[i] = svr_addr_str[i];

    mid = margo_init(cli_addr_prefix, MARGO_CLIENT_MODE, 0, -1);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        munmap(local_region, statbuf.st_size);
        close(fd);
        return -1;
    }

    ret = bake_client_init(mid, &bcl);
    if (ret != 0) {
        bake_perror("Error: bake_client_init()", ret);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        return -1;
    }

    hret = margo_addr_lookup(mid, svr_addr_str, &svr_addr);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_lookup()\n");
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        return (-1);
    }

    ret = bake_provider_handle_create(bcl, svr_addr, mplex_id, &bph);
    if (ret != 0) {
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        bake_perror("Error: bake_provider_handle_create()", ret);
        return (-1);
    }

    ret = bake_probe(bph, target_number, bti, &num_targets);
    if (ret < 0) {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        bake_perror("Error: bake_probe()", ret);
        return (-1);
    }

    if (num_targets < target_number) {
        fprintf(stderr, "Error: provider has only %llu storage targets\n",
                (long long unsigned int)num_targets);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        return -1;
    }

    /* create region */
    ret = bake_create(bph, bti[target_number - 1], statbuf.st_size, &rid);
    if (ret != 0) {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        bake_perror("Error: bake_create()", ret);
        return (-1);
    }

    bake_region_id_to_string(rid, region_str, 128);
    printf("# created bake region %s\n", region_str);

    /* transfer data */
    ret = bake_write(bph, bti[target_number - 1], rid, 0, local_region,
                     statbuf.st_size);
    if (ret != 0) {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        munmap(local_region, statbuf.st_size);
        close(fd);
        bake_perror("bake_write():", ret);
        return (-1);
    }

    munmap(local_region, statbuf.st_size);
    close(fd);

    ret = bake_persist(bph, bti[target_number - 1], rid, 0, statbuf.st_size);
    if (ret != 0) {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        bake_perror("Error: bake_persist()", ret);
        return (-1);
    }

#ifdef USE_SIZECHECK_HEADERS
    /* safety check size */
    ret = bake_get_size(bph, bti[target_number - 1], rid, &check_size);
    if (ret != 0) {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        bake_perror("Error: bake_get_size()", ret);
        return (-1);
    }

    if (check_size != statbuf.st_size) {
        fprintf(stderr, "Error: size mismatch!\n");
        return (-1);
    }
#endif

    bake_provider_handle_release(bph);
    margo_addr_free(mid, svr_addr);
    bake_client_finalize(bcl);
    margo_finalize(mid);

    sprintf(region_file, "/tmp/bb-copy-rid.XXXXXX");
    region_fd = mkstemp(region_file);
    if (region_fd < 0) {
        perror("mkstemp");
    } else {
        ret = write(region_fd, &bti[target_number - 1],
                    sizeof(bti[target_number - 1]));
        if (ret < 0)
            perror("write");
        else {
            ret = write(region_fd, &rid, sizeof(rid));
            if (ret < 0) {
                perror("write");
            } else {
                printf("RID written to %s\n", region_file);
                close(region_fd);
            }
        }
    }

    return (0);
}

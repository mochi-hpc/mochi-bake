/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "abt.h"
#include "abt-snoozer.h"
#include "bake-bulk.h"

/* client program that will copy a bake bulk region out to a POSIX file */

int main(int argc, char **argv) 
{
    int ret;
    bake_target_id_t bti;
    bake_bulk_region_id_t rid;
    int fd;
    char* local_region;
    int region_fd;
    uint64_t check_size;
 
    if(argc != 4)
    {
        fprintf(stderr, "Usage: bb-copy-from <server addr> <identifier file> <output file>\n");
        fprintf(stderr, "  Example: ./bb-copy-from tcp://localhost:1234 /tmp/bb-copy-rid.0GjOlu /tmp/output.dat\n");
        return(-1);
    }       

    /* set up Argobots */
    ret = ABT_init(argc, argv);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_init()\n");
        return(-1);
    }
    ret = ABT_snoozer_xstream_self_set();
    if(ret != 0)
    {
        ABT_finalize();
        fprintf(stderr, "Error: ABT_snoozer_xstream_self_set()\n");
        return(-1);
    }

    ret = bake_probe_instance(argv[1], &bti);
    if(ret < 0)
    {
        ABT_finalize();
        fprintf(stderr, "Error: bake_probe_instance()\n");
        return(-1);
    }

    // FIXME: remove bake_init/finalize from the client API once we
    // figure out initialization API/semantics
    bake_init(bake_get_class());

    region_fd = open(argv[2], O_RDONLY);
    if(region_fd < 0)
    {
        perror("open rid");
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }

    ret = read(region_fd, &rid, sizeof(rid));
    if(ret != sizeof(rid))
    {
        perror("read");
        close(region_fd);
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }
    close(region_fd);

    ret = bake_bulk_get_size(bti, rid, &check_size);
    if(ret != 0)
    {
        fprintf(stderr, "Error: bake_bulk_get_size()\n");
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }

    fd = open(argv[3], O_RDWR|O_TRUNC|O_CREAT, S_IRUSR|S_IWUSR);
    if(fd < 0)
    {
        perror("open output");
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }

    ret = ftruncate(fd, check_size);
    if(ret < 0)
    {
        perror("ftruncate");
        close(fd);
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }

    local_region = mmap(NULL, check_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if(local_region == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        return(-1);
    }

    /* transfer data */
    ret = bake_bulk_read(
        bti,
        rid,
        0,
        local_region,
        check_size);
    if(ret != 0)
    {
        munmap(local_region, check_size);
        close(fd);
        bake_release_instance(bti);
        bake_finalize();
        ABT_finalize();
        fprintf(stderr, "Error: bake_bulk_read()\n");
        return(-1);
    }

    munmap(local_region, check_size);
    close(fd);
    bake_release_instance(bti);
    bake_finalize();
    ABT_finalize();

    return(0);
}


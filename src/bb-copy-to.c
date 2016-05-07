/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "abt.h"
#include "abt-snoozer.h"
#include "bake-bulk.h"

/* client program that will shut down a BAKE bulk server. */

int main(int argc, char **argv) 
{
    int ret;
    bake_target_id_t bti;
    bake_bulk_region_id_t rid;
    int fd;
    struct stat statbuf;
    char* local_region;
 
    if(argc != 3)
    {
        fprintf(stderr, "Usage: bb-copy-to <local file> <server addr>\n");
        fprintf(stderr, "  Example: ./bb-copy-to /tmp/foo.dat tcp://localhost:1234\n");
        return(-1);
    }       

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
    if(!local_region)
    {
        perror("mmap");
        close(fd);
        return(-1);
    }

    /* set up Argobots */
    ret = ABT_init(argc, argv);
    if(ret != 0)
    {
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: ABT_init()\n");
        return(-1);
    }
    ret = ABT_snoozer_xstream_self_set();
    if(ret != 0)
    {
        ABT_finalize();
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: ABT_snoozer_xstream_self_set()\n");
        return(-1);
    }

    ret = bake_probe_instance(argv[2], &bti);
    if(ret < 0)
    {
        ABT_finalize();
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_probe_instance()\n");
        return(-1);
    }

    /* create region */
    ret = bake_bulk_create(bti, statbuf.st_size, &rid);
    if(ret != 0)
    {
        bake_release_instance(bti);
        ABT_finalize();
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_bulk_create()\n");
        return(-1);
    }

    /* TODO: a way to print region id */

    /* transfer data */
    ret = bake_bulk_write(
        bti,
        rid,
        0,
        local_region,
        statbuf.st_size);
    if(ret != 0)
    {
        bake_release_instance(bti);
        ABT_finalize();
        munmap(local_region, statbuf.st_size);
        close(fd);
        fprintf(stderr, "Error: bake_bulk_write()\n");
        return(-1);
    }
   
    munmap(local_region, statbuf.st_size);
    close(fd);
    bake_release_instance(bti);
    ABT_finalize();

    return(0);
}


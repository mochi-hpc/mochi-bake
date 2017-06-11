/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "abt.h"
#include "abt-snoozer.h"
#include "bake-bulk.h"

static void bench_routine_write(bake_target_id_t bti, int iterations, double* measurement_array, int size);
static void bench_routine_read(bake_target_id_t bti, int iterations, double* measurement_array, int size);
static void bench_routine_noop(bake_target_id_t bti, int iterations, double* measurement_array);
static void bench_routine_print(const char* op, int size, int iterations, double* measurement_array);
static int measurement_cmp(const void* a, const void *b);

static double *measurement_array = NULL;
static bake_bulk_region_id_t rid;

int main(int argc, char **argv) 
{
    int ret;
    bake_target_id_t bti;
    int min_size, max_size, iterations, cur_size;
    hg_return_t hret;
    hg_size_t npools;
    hg_size_t count;
    hg_size_t size;
    hg_size_t multiple;
    hg_bulk_pool_thread_opt_t topt;
    hg_bulk_pool_set_t *poolset_read = NULL, *poolset_write = NULL;
 
    if(argc != 5 && argc != 10)
    {
        fprintf(stderr, "Usage: bb-latency-bench <server addr> <iterations> <min_sz> <max_sz> [<npools> <buffers per pool> <initial size> <size multiple> <concurrency mode>\n");
        fprintf(stderr, "  Example: ./bb-latency-bench tcp://localhost:1234 1000 4 32\n");
        return(-1);
    }       

    ret = sscanf(argv[2], "%d", &iterations);
    assert(ret == 1);

    ret = sscanf(argv[3], "%d", &min_size);
    assert(ret == 1);

    ret = sscanf(argv[4], "%d", &max_size);
    assert(ret == 1);

    measurement_array = malloc(sizeof(*measurement_array)*iterations);
    assert(measurement_array);

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

    /* set up bulk pool if asked for */
    /* TODO: sanity check the numbers */
    /*********************************/
    if (argc > 5) {
        npools   = atoi(argv[5]);
        count    = atoi(argv[6]);
        size     = atoi(argv[7]);
        multiple = atoi(argv[8]);
        if      (strcmp(argv[9], "HG")   == 0) topt = HG_BULK_POOL_THREAD_HG;
        else if (strcmp(argv[9], "ABT")  == 0) topt = HG_BULK_POOL_THREAD_ABT;
        else if (strcmp(argv[9], "NONE") == 0) topt = HG_BULK_POOL_THREAD_NONE;
        else {
            fprintf(stderr, "bad thread type argument %s\n", argv[9]);
            free(measurement_array);
            ABT_finalize();
            return(-1);
        }
        hret = hg_bulk_pool_set_create(bake_get_class(), npools, count, size,
                multiple, HG_BULK_READ_ONLY, topt, &poolset_read);
        if (hret != HG_SUCCESS) {
            fprintf(stderr, "failed to create bulk buffer pool\n");
            free(measurement_array);
            ABT_finalize();
            return(-1);
        }
        hret = hg_bulk_pool_set_create(bake_get_class(), npools, count, size,
                multiple, HG_BULK_WRITE_ONLY, topt, &poolset_write);
        if (hret != HG_SUCCESS) {
            fprintf(stderr, "failed to create bulk buffer pool\n");
            free(measurement_array);
            ABT_finalize();
            return(-1);
        }
        bake_set_buffer_pool_set(poolset_read);
        bake_set_buffer_pool_set(poolset_write);
    }

    printf("# <op> <iterations> <size> <min> <q1> <med> <avg> <q3> <max>\n");

    bench_routine_noop(bti, iterations, measurement_array);
    bench_routine_print("noop", 0, iterations, measurement_array);
    for(cur_size=min_size; cur_size <= max_size; cur_size *= 2)
    {
        bench_routine_write(bti, iterations, measurement_array, cur_size);
        bench_routine_print("write", cur_size, iterations, measurement_array);
        bench_routine_read(bti, iterations, measurement_array, cur_size);
        bench_routine_print("read", cur_size, iterations, measurement_array);
    }

    if (poolset_read != NULL) hg_bulk_pool_set_destroy(poolset_read);
    if (poolset_write != NULL) hg_bulk_pool_set_destroy(poolset_write);

    bake_release_instance(bti);

    ABT_finalize();

    free(measurement_array);

    return(0);
}

static double Wtime(void)
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return((double)tp.tv_sec + (double)(tp.tv_nsec) / (double)1000000000.0);
}

static void bench_routine_write(bake_target_id_t bti, int iterations, double *measurement_array, int size)
{
    int ret;
    double tm1, tm2;
    char *buffer;
    uint64_t region_offset = 0;
    uint64_t region_size = 0;
    int i;

    buffer = calloc(1, size);
    assert(buffer);

    region_size = (uint64_t)size * (uint64_t)iterations;

    /* create region */
    ret = bake_bulk_create(bti, region_size, &rid);
    assert(ret == 0);

    sleep(1);

    for(i=0; i<iterations; i++)
    {
        tm1 = Wtime();
        /* transfer data (writes) */
        ret = bake_bulk_write(
            bti,
            rid,
            region_offset,
            buffer,
            size);
        tm2 = Wtime();
        assert(ret == 0);
        region_offset += size;
        measurement_array[i] = tm2-tm1;
    }

    /* persist */
    ret = bake_bulk_persist(bti, rid);
    assert(ret == 0);

    free(buffer);

    return;
}

static void bench_routine_read(bake_target_id_t bti, int iterations, double *measurement_array, int size)
{
    int ret;
    double tm1, tm2;
    char *buffer;
    uint64_t region_offset = 0;
    int i;

    buffer = calloc(1, size);
    assert(buffer);

    sleep(1);

    for(i=0; i<iterations; i++)
    {
        tm1 = Wtime();
        /* transfer data (reads) */
        ret = bake_bulk_read(
            bti,
            rid,
            region_offset,
            buffer,
            size);
        tm2 = Wtime();
        assert(ret == 0);
        region_offset += size;
        measurement_array[i] = tm2-tm1;
    }

    free(buffer);

    return;
}

static void bench_routine_noop(bake_target_id_t bti, int iterations, double *measurement_array)
{
    int ret;
    double tm1, tm2;
    int i;

    sleep(1);

    for(i=0; i<iterations; i++)
    {
        tm1 = Wtime();
        /* noop */
        ret = bake_bulk_noop(bti);
        tm2 = Wtime();
        assert(ret == 0);

        measurement_array[i] = tm2-tm1;
    }

    return;
}

static int measurement_cmp(const void* a, const void *b)
{
    const double *d_a = a;
    const double *d_b = b;

    if(*d_a < *d_b)
        return(-1);
    else if(*d_a > *d_b)
        return(1);
    else
        return(0);
}

static void bench_routine_print(const char* op, int size, int iterations, double* measurement_array)
{
    double min, max, q1, q3, med, avg, sum;
    int bracket1, bracket2;
    int i;

    qsort(measurement_array, iterations, sizeof(double), measurement_cmp);

    min = measurement_array[0];
    max = measurement_array[iterations-1];

    sum = 0;
    for(i=0; i<iterations; i++)
    {
        sum += measurement_array[i];
    }
    avg = sum/(double)iterations;

    /* HACK: quartile logic breaks down for small iteration count, so just
     * disable for small counts */
    if (iterations < 5) {
        med = 0.0;
        q1 = 0.0;
        q3 = 0.0;
    }
    else {
        bracket1 = iterations/2;
        if(iterations%2)
            bracket2 = bracket1 + 1;
        else
            bracket2 = bracket1;
        med = (measurement_array[bracket1] +
                measurement_array[bracket2])/(double)2;

        bracket1 = iterations/4;
        if(iterations%4)
            bracket2 = bracket1 + 1;
        else
            bracket2 = bracket1;
        q1 = (measurement_array[bracket1] +
                measurement_array[bracket2])/(double)2;

        bracket1 *= 3;
        if(iterations%4)
            bracket2 = bracket1 + 1;
        else
            bracket2 = bracket1;
        q3 = (measurement_array[bracket1] +
                measurement_array[bracket2])/(double)2;
    }

    printf("%s\t%d\t%d\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f", op, iterations, size, min, q1, med, avg, q3, max);
    for(i=0; i<iterations; i++)
    {
        printf("\t%.9f", measurement_array[i]);
    }
    printf("\n");
    fflush(NULL);

    return;
}

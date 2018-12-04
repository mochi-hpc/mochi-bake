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

static void bench_routine_write(bake_provider_handle_t bph, bake_target_id_t bti, int iterations, double* measurement_array, int size);
static void bench_routine_read(bake_provider_handle_t bph, int iterations, double* measurement_array, int size);
static void bench_routine_noop(bake_provider_handle_t bph, int iterations, double* measurement_array);
static void bench_routine_print(const char* op, int size, int iterations, double* measurement_array);
static int measurement_cmp(const void* a, const void *b);

static double *measurement_array = NULL;
static bake_region_id_t rid;

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
    int min_size, max_size, iterations, cur_size;
 
    if(argc != 6)
    {
        fprintf(stderr, "Usage: bake-latency-bench <server addr> <mplex id> <iterations> <min_sz> <max_sz>\n");
        fprintf(stderr, "  Example: ./bake-latency-bench tcp://localhost:1234 3 1000 4 32\n");
        return(-1);
    }
    svr_addr_str = argv[1];
    mplex_id = atoi(argv[2]);

    ret = sscanf(argv[3], "%d", &iterations);
    assert(ret == 1);

    ret = sscanf(argv[4], "%d", &min_size);
    assert(ret == 1);

    ret = sscanf(argv[5], "%d", &max_size);
    assert(ret == 1);

    measurement_array = malloc(sizeof(*measurement_array)*iterations);
    assert(measurement_array);

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
        fprintf(stderr, "Error: bake_client_init()\n");
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

    ret = bake_probe(bph, 1, &bti, &num_targets);
    if(ret < 0)
    {
        fprintf(stderr, "Error: bake_probe()\n");
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    printf("# <op> <iterations> <size> <min> <q1> <med> <avg> <q3> <max>\n");

    bench_routine_noop(bph, iterations, measurement_array);
    bench_routine_print("noop", 0, iterations, measurement_array);
    for(cur_size=min_size; cur_size <= max_size; cur_size *= 2)
    {
        bench_routine_write(bph, bti, iterations, measurement_array, cur_size);
        bench_routine_print("write", cur_size, iterations, measurement_array);
        bench_routine_read(bph, iterations, measurement_array, cur_size);
        bench_routine_print("read", cur_size, iterations, measurement_array);
    }
    
    bake_provider_handle_release(bph);
    margo_addr_free(mid, svr_addr);
    bake_client_finalize(bcl);
    margo_finalize(mid);

    free(measurement_array);

    return(0);
}

static double Wtime(void)
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return((double)tp.tv_sec + (double)(tp.tv_nsec) / (double)1000000000.0);
}

static void bench_routine_write(bake_provider_handle_t bph, bake_target_id_t bti, int iterations, double *measurement_array, int size)
{
    int ret;
    double tm1, tm2;
    char *buffer;
    uint64_t region_offset = 0;
    int i;

    buffer = calloc(1, size);
    assert(buffer);

    /* create region */
    ret = bake_create(bph, bti, size*iterations, &rid);
    assert(ret == 0);

    sleep(1);

    for(i=0; i<iterations; i++)
    {
        tm1 = Wtime();
        /* transfer data (writes) */
        ret = bake_write(
            bph,
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
    ret = bake_persist(bph, rid, 0, size*iterations);
    assert(ret == 0);

    free(buffer);

    return;
}

static void bench_routine_read(bake_provider_handle_t bph, int iterations, double *measurement_array, int size)
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
        uint64_t bytes_read;
        ret = bake_read(
            bph,
            rid,
            region_offset,
            buffer,
            size,
            &bytes_read);
        tm2 = Wtime();
        assert(ret == 0);
        region_offset += size;
        measurement_array[i] = tm2-tm1;
    }

    free(buffer);

    return;
}

static void bench_routine_noop(bake_provider_handle_t bph, int iterations, double *measurement_array)
{
    int ret;
    double tm1, tm2;
    int i;

    sleep(1);

    for(i=0; i<iterations; i++)
    {
        tm1 = Wtime();
        /* noop */
        ret = bake_noop(bph);
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

    bracket1 = iterations/2;
    if(iterations%2)
        bracket2 = bracket1 + 1;
    else
        bracket2 = bracket1;
    med = (measurement_array[bracket1] + measurement_array[bracket2])/(double)2;

    bracket1 = iterations/4;
    if(iterations%4)
        bracket2 = bracket1 + 1;
    else
        bracket2 = bracket1;
    q1 = (measurement_array[bracket1] + measurement_array[bracket2])/(double)2;

    bracket1 *= 3;
    if(iterations%4)
        bracket2 = bracket1 + 1;
    else
        bracket2 = bracket1;
    q3 = (measurement_array[bracket1] + measurement_array[bracket2])/(double)2;

    printf("%s\t%d\t%d\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f", op, iterations, size, min, q1, med, avg, q3, max);
    for(i=0; i<iterations; i++)
    {
        printf("\t%.9f", measurement_array[i]);
    }
    printf("\n");
    fflush(NULL);

    return;
}

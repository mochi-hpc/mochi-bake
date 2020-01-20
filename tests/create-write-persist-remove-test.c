/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <mercury.h>
#include <abt.h>
#include <margo.h>

#include "bake-client.h"

#define ALLOC_BUF_SIZE 512


int main(int argc, char *argv[])
{
    int i;
    char cli_addr_prefix[64] = {0};
    char *bake_svr_addr_str;
    margo_instance_id mid;
    hg_addr_t svr_addr;
    uint8_t mplex_id;
    bake_client_t bcl;
    bake_provider_handle_t bph;
    uint64_t num_targets;
    bake_target_id_t bti;
    bake_region_id_t the_rid;
    const char *test_str = "This is a test string for create-write-persist test.";
    char *buf;
    uint64_t buf_size;
    hg_return_t hret;
    int ret;

    if(argc != 3)
    {
        fprintf(stderr, "Usage: create-write-persist-test <bake server addr> <mplex id>\n");
        fprintf(stderr, "  Example: ./create-write-persist-test tcp://localhost:1234 1\n");
        return(-1);
    }
    bake_svr_addr_str = argv[1];
    mplex_id = atoi(argv[2]);

    /* initialize Margo using the transport portion of the server
     * address (i.e., the part before the first : character if present)
     */
    for(i=0; (i<63 && bake_svr_addr_str[i] != '\0' && bake_svr_addr_str[i] != ':'); i++)
        cli_addr_prefix[i] = bake_svr_addr_str[i];

    /* start margo */
    mid = margo_init(cli_addr_prefix, MARGO_SERVER_MODE, 0, 0);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        return(-1);
    }

    ret = bake_client_init(mid, &bcl);
    if(ret != 0)
    {
        bake_perror( "Error: bake_client_init()", ret);
        margo_finalize(mid);
        return -1;
    }

    /* look up the BAKE server address */
    hret = margo_addr_lookup(mid, bake_svr_addr_str, &svr_addr);
    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_lookup()\n");
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /* create a BAKE provider handle */
    ret = bake_provider_handle_create(bcl, svr_addr, mplex_id, &bph);
    if(ret != 0)
    {
        bake_perror( "Error: bake_provider_handle_create()", ret);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }
    bake_provider_handle_set_eager_limit(bph, 0);

    /* obtain info on the server's BAKE target */
    ret = bake_probe(bph, 1, &bti, &num_targets);
    if(ret != 0)
    {
        bake_perror( "Error: bake_probe()", ret);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    buf = malloc(ALLOC_BUF_SIZE);
    if(!buf)
    {
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /**** write phase ****/

    /* copy the test string into a buffer and forward to the proxy server */
    strcpy(buf, test_str);
    buf_size = strlen(test_str) + 1;

    ret = bake_create_write_persist(bph, bti, buf, buf_size, &the_rid);
    if(ret != 0)
    {
        bake_perror("Error: bake_create_write_persist()", ret);
        free(buf);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /**** read-back phase ****/

    /* reset the buffer and read it back via BAKE */
    memset(buf, 0, ALLOC_BUF_SIZE);

    uint64_t bytes_read;
    ret = bake_read(bph, bti, the_rid, 0, buf, buf_size, &bytes_read);
    if(ret != 0)
    {
        bake_perror( "Error: bake_read()", ret);
        free(buf);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /* check to make sure we get back the string we expect */
    if(strcmp(buf, test_str) != 0)
    {
        fprintf(stderr, "Error: unexpected buffer contents returned from BAKE\n");
        free(buf);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    ret = bake_remove(bph, bti, the_rid);
    if (ret != 0)
    {
        bake_perror( "Error: unable to remove the created BAKE region", ret);
        free(buf);
        bake_provider_handle_release(bph);
        margo_addr_free(mid, svr_addr);
        bake_client_finalize(bcl);
        margo_finalize(mid);
        return(-1);
    }

    /* shutdown the server */
    ret = bake_shutdown_service(bcl, svr_addr);

    /**** cleanup ****/

    free(buf);
    bake_provider_handle_release(bph);
    margo_addr_free(mid, svr_addr);
    bake_client_finalize(bcl);
    margo_finalize(mid);
    return(ret);
}


/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <margo.h>
#include <libpmemobj.h>
#include <bake-client.h>

#include "proxy-rpc.h"

DECLARE_MARGO_RPC_HANDLER(proxy_write_ult)
DECLARE_MARGO_RPC_HANDLER(proxy_read_ult)
DECLARE_MARGO_RPC_HANDLER(proxy_shutdown_ult)

struct options
{
    char *listen_addr_str;
    char *bake_svr_addr_str;
    uint8_t bake_mplex_id;
    int batch_rpc;
    char *host_file;
};

struct proxy_server_context
{
    hg_addr_t        svr_addr;
    bake_client_t    bcl;
    bake_provider_handle_t svr_bph;
    bake_target_id_t svr_bti;
    bake_region_id_t the_rid;
    int batch_rpc;
};

static struct proxy_server_context *g_proxy_svr_ctx = NULL;

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: proxy-server-daemon [OPTIONS] <listen_addr> <bake_server_addr> <bake mplex id>\n");
    fprintf(stderr, "       listen_addr is the Mercury address to listen on\n");
    fprintf(stderr, "       bake_server_addr is the Mercury address of the BAKE server\n");
    fprintf(stderr, "       [-b] to batch the BAKE region create, write, and persist operations in one RPC\n");
    fprintf(stderr, "       [-f filename] to write the proxy server address to a file\n");
    fprintf(stderr, "Example: ./proxy-server-daemon na+sm na+sm://3005/0\n");
    return;
}

static void parse_args(int argc, char **argv, struct options *opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    /* get options */
    while((opt = getopt(argc, argv, "bf:")) != -1)
    {
        switch(opt)
        {
            case 'b':
                opts->batch_rpc = 1;
                break;
            case 'f':
                opts->host_file = optarg;
                break;
            default:
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    /* get required arguments after options */
    if((argc - optind) != 3)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    opts->listen_addr_str = argv[optind++];
    opts->bake_svr_addr_str = argv[optind++];
    opts->bake_mplex_id = atoi(argv[optind++]);

    return;
}

static void finalize_cb(void* data) {
    struct proxy_server_context* ctx = (struct proxy_server_context*)data;
    bake_provider_handle_release(ctx->svr_bph);
    bake_client_finalize(ctx->bcl);
}

int main(int argc, char **argv) 
{
    struct options opts;
    margo_instance_id mid;
    hg_addr_t bake_svr_addr;
    hg_return_t hret;
    int ret;

    parse_args(argc, argv, &opts);

    g_proxy_svr_ctx = malloc(sizeof(*g_proxy_svr_ctx));
    if(!g_proxy_svr_ctx)
        return(-1);
    memset(g_proxy_svr_ctx, 0, sizeof(*g_proxy_svr_ctx));
    g_proxy_svr_ctx->batch_rpc = opts.batch_rpc;

    /* start margo */
    mid = margo_init(opts.listen_addr_str, MARGO_SERVER_MODE, 0, -1);
    if(mid == MARGO_INSTANCE_NULL)
    {
        fprintf(stderr, "Error: margo_init()\n");
        return(-1);
    }

    if(opts.host_file)
    {
        /* write the server address to file if requested */
        FILE *fp;
        hg_addr_t self_addr;
        char self_addr_str[128];
        hg_size_t self_addr_str_sz = 128;
        hg_return_t hret;

        /* figure out what address this server is listening on */
        hret = margo_addr_self(mid, &self_addr);
        if(hret != HG_SUCCESS)
        {
            fprintf(stderr, "Error: margo_addr_self()\n");
            margo_finalize(mid);
            return(-1);
        }
        hret = margo_addr_to_string(mid, self_addr_str, &self_addr_str_sz, self_addr);
        if(hret != HG_SUCCESS)
        {
            fprintf(stderr, "Error: margo_addr_to_string()\n");
            margo_addr_free(mid, self_addr);
            margo_finalize(mid);
            return(-1);
        }
        margo_addr_free(mid, self_addr);

        fp = fopen(opts.host_file, "w");
        if(!fp)
        {
            perror("fopen");
            margo_finalize(mid);
            return(-1);
        }

        fprintf(fp, "%s", self_addr_str);
        fclose(fp);
    }

    /* creating BAKE client */
    ret = bake_client_init(mid, &g_proxy_svr_ctx->bcl);
    if(ret != 0)
    {
        bake_perror( "Error: bake_client_init()\n", ret);
        margo_finalize(mid);
        return -1;
    }

    margo_push_finalize_callback(mid, finalize_cb, (void*)(g_proxy_svr_ctx));

    /* lookup the BAKE server address */
    hret = margo_addr_lookup(mid, opts.bake_svr_addr_str, &bake_svr_addr);

    if(hret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: margo_addr_to_string()\n");
        bake_client_finalize(g_proxy_svr_ctx->bcl);
        margo_finalize(mid);
        return(-1);
    }
    g_proxy_svr_ctx->svr_addr = bake_svr_addr;

    /* create a BAKE provider handle */
    ret = bake_provider_handle_create(g_proxy_svr_ctx->bcl,
            bake_svr_addr, opts.bake_mplex_id, &g_proxy_svr_ctx->svr_bph);

    if(ret != 0)
    {
        bake_perror( "Error: bake_provider_handle_create()", ret);
        bake_client_finalize(g_proxy_svr_ctx->bcl);
        margo_finalize(mid);
        return -1;
    }

    /* probe the BAKE server for a target */
    uint64_t num_targets;
    ret = bake_probe(g_proxy_svr_ctx->svr_bph, 
                1, &g_proxy_svr_ctx->svr_bti, &num_targets);
    if(ret < 0)
    {
        bake_perror( "Error: bake_probe_instance()", ret);
        return(-1);
    }
    margo_addr_free(mid, bake_svr_addr);

    /* register proxy service RPCs */
    MARGO_REGISTER(mid, "proxy_write", proxy_write_in_t, proxy_write_out_t,
        proxy_write_ult);
    MARGO_REGISTER(mid, "proxy_read", proxy_read_in_t, proxy_read_out_t,
        proxy_read_ult);
    MARGO_REGISTER(mid, "proxy_shutdown", void, void,
        proxy_shutdown_ult);

    /* wait for the shutdown signal */
    margo_wait_for_finalize(mid);

    return(0);
}

static void proxy_write_ult(hg_handle_t handle)
{
    proxy_write_in_t in;
    proxy_write_out_t out;
    hg_return_t hret;
    int ret;

    assert(g_proxy_svr_ctx);

    /* get RPC input */
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    if(g_proxy_svr_ctx->batch_rpc)
    {
        /* create the BAKE region, write to it on behalf of the client,
         * and persist the region all in one RPC
         */
        ret = bake_create_write_persist_proxy(
            g_proxy_svr_ctx->svr_bph, g_proxy_svr_ctx->svr_bti,
            in.bulk_handle, in.bulk_offset, in.bulk_addr,
            in.bulk_size, &(g_proxy_svr_ctx->the_rid));
        assert(ret == 0);
    }
    else
    {
        /* create BAKE region to store this write in */
        ret = bake_create(
            g_proxy_svr_ctx->svr_bph,
            g_proxy_svr_ctx->svr_bti, in.bulk_size,
            &(g_proxy_svr_ctx->the_rid));
        assert(ret == 0);

        /* perform proxy write on behalf of client */
        ret = bake_proxy_write(g_proxy_svr_ctx->svr_bph, 
            g_proxy_svr_ctx->svr_bti, g_proxy_svr_ctx->the_rid,
            0, in.bulk_handle, in.bulk_offset, in.bulk_addr, in.bulk_size);
        assert(ret == 0);

        /* persist the BAKE region */
        ret = bake_persist(g_proxy_svr_ctx->svr_bph, 
                g_proxy_svr_ctx->svr_bti, g_proxy_svr_ctx->the_rid,
                0, in.bulk_size);
        assert(ret == 0);
    }

    /* set return value */
    out.ret = 0;

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_free_input(handle, &in);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(proxy_write_ult)

static void proxy_read_ult(hg_handle_t handle)
{
    proxy_read_in_t in;
    proxy_read_out_t out;
    hg_return_t hret;
    int ret;

    assert(g_proxy_svr_ctx);

    /* get RPC input */
    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    /* perform proxy write on behalf of client */
    uint64_t bytes_read;
    ret = bake_proxy_read(g_proxy_svr_ctx->svr_bph,
            g_proxy_svr_ctx->svr_bti, g_proxy_svr_ctx->the_rid,
        0, in.bulk_handle, in.bulk_offset, in.bulk_addr, in.bulk_size, &bytes_read);
    assert(ret == 0);

    /* set return value */
    out.ret = 0;

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_free_input(handle, &in);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(proxy_read_ult)

static void proxy_shutdown_ult(hg_handle_t handle)
{
    margo_instance_id mid;
    hg_return_t hret;

    assert(g_proxy_svr_ctx);

    /* get margo instance from handle */
    mid = margo_hg_handle_get_instance(handle);
    assert(mid != MARGO_INSTANCE_NULL);

    hret = margo_respond(handle, NULL);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);

    /* forward shutdown to the bake-bulk server */
    bake_shutdown_service(g_proxy_svr_ctx->bcl, g_proxy_svr_ctx->svr_addr);

    /* cleanup global state */
    free(g_proxy_svr_ctx);

    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(proxy_shutdown_ult)

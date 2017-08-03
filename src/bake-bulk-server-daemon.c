/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <abt.h>
#include <abt-snoozer.h>
#include <margo.h>
#include <libpmemobj.h>
#include <bake-bulk-server.h>

int main(int argc, char **argv) 
{
    int ret;
    margo_instance_id mid;
    ABT_xstream handler_xstream;
    ABT_pool handler_pool;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    char target_string[64];
    PMEMoid root_oid;
    PMEMobjpool *bb_pmem_pool = NULL;
    struct bake_bulk_root *bb_pmem_root = NULL;

    if(argc != 3)
    {
        fprintf(stderr, "Usage: bake-bulk-server <HG listening addr> <pmem pool>\n");
        fprintf(stderr, "  Example: ./bake-bulk-server tcp://localhost:1234 /dev/shm/foo.dat\n");
        return(-1);
    }

    /* open pmem pool */
    bb_pmem_pool = pmemobj_open(argv[2], NULL);
    if(!bb_pmem_pool)
    {
        fprintf(stderr, "pmemobj_open: %s\n", pmemobj_errormsg());
        return(-1);
    }
    
    /* find root */
    root_oid = pmemobj_root(bb_pmem_pool, sizeof(*bb_pmem_root));
    bb_pmem_root = pmemobj_direct(root_oid);
    if(uuid_is_null(bb_pmem_root->target_id.id))
    {
        uuid_generate(bb_pmem_root->target_id.id);
        pmemobj_persist(bb_pmem_pool, bb_pmem_root, sizeof(*bb_pmem_root));
    }
    uuid_unparse(bb_pmem_root->target_id.id, target_string);
    fprintf(stderr, "BAKE target ID: %s\n", target_string);

    /* boilerplate HG initialization steps */
    /***************************************/
    hg_class = HG_Init(argv[1], HG_TRUE);
    if(!hg_class)
    {
        fprintf(stderr, "Error: HG_Init()\n");
        return(-1);
    }
    hg_context = HG_Context_create(hg_class);
    if(!hg_context)
    {
        fprintf(stderr, "Error: HG_Context_create()\n");
        HG_Finalize(hg_class);
        return(-1);
    }

    /* set up argobots */
    /***************************************/
    ret = ABT_init(argc, argv);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_init()\n");
        return(-1);
    }

    /* set primary ES to idle without polling */
    ret = ABT_snoozer_xstream_self_set();
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_snoozer_xstream_self_set()\n");
        return(-1);
    }

    /* Find primary pool to use for running rpc handlers */
    ret = ABT_xstream_self(&handler_xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(-1);
    }
    ret = ABT_xstream_get_main_pools(handler_xstream, 1, &handler_pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(-1);
    }

    /* actually start margo */
    /* provide argobots pools for driving communication progress and
     * executing rpc handlers as well as class and context for Mercury
     * communication.
     */
    /***************************************/
    mid = margo_init_pool(handler_pool, handler_pool, hg_context);
    assert(mid);

    /* register the bake bulk server */
    bake_server_register(mid, bb_pmem_pool, bb_pmem_root);

    /* NOTE: at this point this server ULT has two options.  It can wait on
     * whatever mechanism it wants to (however long the daemon should run and
     * then call margo_finalize().  Otherwise, it can call
     * margo_wait_for_finalize() on the assumption that it should block until
     * some other entity calls margo_finalize().
     *
     * This example does the latter.  Margo will be finalized by a special
     * RPC from the client.
     *
     * This approach will allow the server to idle gracefully even when
     * executed in "single" mode, in which the main thread of the server
     * daemon and the progress thread for Mercury are executing in the same
     * ABT pool.
     */
    margo_wait_for_finalize(mid);

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    pmemobj_close(bb_pmem_pool);

    return(0);
}


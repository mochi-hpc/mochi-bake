/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* for O_DIRECT */
#define _GNU_SOURCE
#include <assert.h>
#include <abt-io.h>
#include "bake-config.h"
#include "bake.h"
#include "bake-rpc.h"
#include "bake-server.h"
#include "bake-provider.h"
#include "bake-backend.h"

/* bake-file-backend
 *
 * This is an implemenation of a back end for the Bake provider that stores
 * all data in normal POSIX files.  All data is stored in a single
 * block-aligned, log-structured, file and accessed using directio through
 * the abt-io library.
 */

/* TODO: determine proper alignment at runtime if possible */
#define BAKE_ALIGNMENT 512
#define BAKE_ALIGN_UP(x) ( (((unsigned long)(x)) + 511)  & (~(511)) )
#define BAKE_ALIGN_DOWN(x) ((unsigned long)(x) & (~(511)))

#define TRANSFER_DATA_READ 1
#define TRANSFER_DATA_WRITE 2

/* definition of BAKE root data structure (just a uuid for now) */
typedef struct
{
    bake_target_id_t pool_id;
} bake_root_t;

/* definition of internal BAKE region_id_t identifier for file back end */
typedef struct
{
    off_t log_entry_offset;
    size_t log_entry_size;
} file_region_id_t;

typedef struct {
    char data[1];
} region_content_t;

typedef struct {
    bake_provider_t provider;
    int log_fd;       /* file descriptor for log */
    off_t log_offset; /* next available unused offset in log */
    ABT_mutex log_offset_mutex; /* protects the above during concurrent region creation */
    abt_io_instance_id abtioi;  /* abt-io instance used by this provider */
    bake_root_t* file_root;
    char* root;
    char* filename;
} bake_file_entry_t;

static int transfer_data(
    bake_file_entry_t* entry,
    off_t log_entry_offset,
    size_t log_entry_size,
    uint64_t region_offset,
    hg_bulk_t remote_bulk,
    uint64_t remote_bulk_offset,
    uint64_t bulk_size,
    hg_addr_t src_addr,
    int op_flag);

/* TODO: reorganize this later into the "admin library" model */
int bake_file_makepool(
        const char *file_name,
        size_t file_size,
        mode_t file_mode)
{
    int fd = -1;
    bake_root_t *root;
    int ret;

    fd = open(file_name, O_EXCL|O_WRONLY|O_CREAT|O_DIRECT, file_mode);
    if(fd < 0)
    {
        int save_errno = errno;
        perror("open");
        if(save_errno == EINVAL)
            fprintf(stderr, "... does your file system support O_DIRECT? tmpfs does not.\n");
        return(BAKE_ERR_IO);
    }

    /* we'll put a full block at the front of the file, the first bytes of
     * which will contain the bake_root_t
     */
    ret = posix_memalign((void**)(&root), BAKE_ALIGNMENT, BAKE_ALIGNMENT);
    assert(ret == 0);
    memset(root, 0, BAKE_ALIGNMENT);

    /* store the target id for this bake pool at the root */
    uuid_generate(root->pool_id.id);

    ret = write(fd, root, BAKE_ALIGNMENT);
    if(ret != BAKE_ALIGNMENT)
    {
        perror("write");
        free(root);
        return(BAKE_ERR_IO);
    }
    free(root);

    close(fd);

    return BAKE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_initialize(bake_provider_t provider,
                                        const char* path,
                                        bake_target_id_t *target,
                                        backend_context_t *context)
{
    int ret = BAKE_SUCCESS;
    bake_file_entry_t* new_entry = calloc(1, sizeof(*new_entry));
    new_entry->provider = provider;
    new_entry->log_fd = -1;
    const char *tmp;
    ptrdiff_t d;

    if(!provider->config.pipeline_enable)
    {
        fprintf(stderr, "Error: The Bake file backend requires pipelining.\n");
        fprintf(stderr, "   Enable pipelining with -p on the bake-server-daemon command line or\n");
        fprintf(stderr, "   programmatically with bake_provider_set_conf(provider, \"pipeline_enabled\", \"1\")\n");
        return(BAKE_ERR_INVALID_ARG);
    }

    tmp = strrchr(path, '/');
    if(!tmp)
        tmp = path;
    new_entry->filename = strdup(tmp);
    d = tmp - path;
    new_entry->root = strndup(path, d);

    /* initialize an abt-io instance just for this target */
    /* TODO: make number of backing threads tunable */
    new_entry->abtioi = abt_io_init(8);
    if(!new_entry->abtioi)
    {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }

    new_entry->log_fd = abt_io_open(new_entry->abtioi,
        path, O_RDWR|O_DIRECT, 0);
    if(new_entry->log_fd < 0) {
        perror("open");
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    /* TODO: is this code path used for existing targets too?  If so, need
     * to set log offset appropriately
     */
    new_entry->log_offset = BAKE_ALIGNMENT;  /* skip over header info */
    ABT_mutex_create(&new_entry->log_offset_mutex);

    /* check to make sure the root is properly set */
    ret = posix_memalign((void**)(&new_entry->file_root),
        BAKE_ALIGNMENT, BAKE_ALIGNMENT);
    if(ret < 0)
    {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    ret = abt_io_pread(new_entry->abtioi, new_entry->log_fd,
        new_entry->file_root, BAKE_ALIGNMENT, 0);
    if(ret < 0)
    {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    *target = new_entry->file_root->pool_id;

    if(uuid_is_null(target->id))
    {
        fprintf(stderr, "Error: BAKE pool %s is not properly formatted\n", path);
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }

    fprintf(stderr, "WARNING: Bake file backend does not yet support the following:\n");
    fprintf(stderr, "    * writes to non-zero region offsets\n");

    *context = new_entry;
    return 0;

error_cleanup:
    if(new_entry)
    {
        if(new_entry->file_root)
            free(new_entry->file_root);
        if(new_entry->log_fd > -1)
            close(new_entry->log_fd);
        if(new_entry->abtioi)
            abt_io_finalize(new_entry->abtioi);
        if(new_entry->filename)
            free(new_entry->filename);
        if(new_entry->root)
            free(new_entry->root);
        free(new_entry);
    }
    return(ret);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_finalize(backend_context_t context)
{
    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    free(entry->file_root);
    close(entry->log_fd);
    abt_io_finalize(entry->abtioi);
    free(entry->filename);
    free(entry->root);
    free(entry);

    return BAKE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_create(backend_context_t context,
                            size_t size,
                            bake_region_id_t *rid)
{
    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    int ret;
    void *zero_block;
    file_region_id_t* frid = (file_region_id_t*)rid->data;

    assert(sizeof(file_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

    /* round up size for directio alignment */
    size = BAKE_ALIGN_UP(size);

    frid->log_entry_size = size;
    ABT_mutex_lock(entry->log_offset_mutex);
    frid->log_entry_offset = entry->log_offset;
    entry->log_offset += size;
    ABT_mutex_unlock(entry->log_offset_mutex);

    /* We write one empty block at the end of the log extent covered by this
     * region.  The goal is to extend the log file length (if
     * necessary) so that if the daemon crashes and restarts it will begin
     * allocating at the correct offset rather than possibly reusing space
     * that was promised to a previous region.
     *
     * Ideally this would just be a metadata update to the file system since
     * we don't care about data contents in this range, but it's not clear
     * that there is an fallocate() variant that will extend the file size
     * without allocating blocks.  So we write a block and sync.
     *
     * We write a full block to make sure it will work with O_DIRECT.
     */
    ret = posix_memalign(&zero_block, BAKE_ALIGNMENT, BAKE_ALIGNMENT);
    if(ret != 0)
        return(BAKE_ERR_IO);

    ret = abt_io_pwrite(entry->abtioi, entry->log_fd, zero_block,
        BAKE_ALIGNMENT, entry->log_offset-BAKE_ALIGNMENT);
    if(ret != BAKE_ALIGNMENT)
    {
        free(zero_block);
        return(BAKE_ERR_IO);
    }

    ret = abt_io_fdatasync(entry->abtioi, entry->log_fd);
    if(ret != 0)
    {
        free(zero_block);
        return(BAKE_ERR_IO);
    }

    free(zero_block);
    return(BAKE_SUCCESS);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_raw(backend_context_t context,
                               bake_region_id_t rid,
                               size_t offset,
                               size_t size,
                               const void* data)
{
    /* NOTES:
     * - this routine is most likely called in the eager write path
     * - the data buffer is already present, and is probably small, but it
     *   is very unlikely that the offset and size are both page aligned
     * - we therefore create an intermediate aligned buffer to copy through
     *   and write to the log
     */

    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    file_region_id_t* frid = (file_region_id_t*)rid.data;
    void* bounce_buffer;
    int ret;

    /* TODO: implement this.  For now we only handle writes beginning at
     * offset zero of a region.  Writes that begin elsewhere will require a
     * r/m/w to handle correctly, since there is no requirement that bake
     * write offsets are aligned.
     */
    if(offset != 0)
    {
        fprintf(stderr, "Error: Bake file backend does not yet support unaligned writes.\n");
        return(BAKE_ERR_OP_UNSUPPORTED);
    }

    if(size+offset > frid->log_entry_size)
    {
        /* caller is attempting to write more data into this region than was
         * allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }

    ret = posix_memalign(&bounce_buffer, BAKE_ALIGNMENT, BAKE_ALIGN_UP(size));
    if(ret != 0)
        return(BAKE_ERR_IO);

    memcpy(bounce_buffer, data, size);

    ret = abt_io_pwrite(entry->abtioi, entry->log_fd, bounce_buffer,
        BAKE_ALIGN_UP(size), frid->log_entry_offset);
    if(ret != BAKE_ALIGNMENT)
    {
        free(bounce_buffer);
        return(BAKE_ERR_IO);
    }

    free(bounce_buffer);

    return(BAKE_SUCCESS);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_bulk(backend_context_t context,
                                bake_region_id_t rid,
                                size_t region_offset,
                                size_t size,
                                hg_bulk_t bulk,
                                hg_addr_t source,
                                size_t bulk_offset)
{
    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    file_region_id_t* frid = (file_region_id_t*)rid.data;
    int ret;

    /* TODO: implement this.  For now we only handle writes beginning at
     * offset zero of a region.  Writes that begin elsewhere will require a
     * r/m/w to handle correctly, since there is no requirement that bake
     * write offsets are aligned.
     */
    if(region_offset != 0)
    {
        fprintf(stderr, "Error: Bake file backend does not yet support unaligned writes.\n");
        return(BAKE_ERR_OP_UNSUPPORTED);
    }

    ret = transfer_data(entry, frid->log_entry_offset, frid->log_entry_size,
        region_offset, bulk, bulk_offset, size, source, TRANSFER_DATA_WRITE);

    return(ret);
}

/* utility function used to free bounce buffers created by
 * bake_file_read_raw().  It is like a normal fre() except that it must
 * round down to block alignment to find the correct pointer to free.
 */
static void bake_file_read_raw_free(void* ptr)
{
    free((void*)(BAKE_ALIGN_DOWN(ptr)));
    return;
}

static int bake_file_read_raw(backend_context_t context,
                              bake_region_id_t rid,
                              size_t offset,
                              size_t size,
                              void** data,
                              uint64_t* data_size,
                              free_fn* free_data)
{
    /* NOTES:
     * - this routine is most likely called in the eager read path
     * - the api provides both a buffer pointer and a free function pointer
     *   to the caller. We take advantage of that to account for alignment
     *   within the log and bounce buffer.
     * - doing this with a single intermediate buffer and one I/O operation
     *   is fine, as we expect this to be a small access.
     */

    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    file_region_id_t* frid = (file_region_id_t*)rid.data;
    void* bounce_buffer;
    int ret;
    off_t natural_offset_start, natural_offset_end;
    off_t log_offset_start, log_offset_end;

    if(size+offset > frid->log_entry_size)
    {
        /* caller is attempting to read more data from this region than was
         * allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }

    /* not counting alignment, what portion of the log do we want? */
    natural_offset_start = frid->log_entry_offset + offset;
    natural_offset_end = natural_offset_start + size;
    /* align both to find log extent */
    log_offset_start = BAKE_ALIGN_DOWN(natural_offset_start);
    log_offset_end = BAKE_ALIGN_UP(natural_offset_end);

    /* create aligned bounce buffer large enough to hold log extent */
    ret = posix_memalign(&bounce_buffer, BAKE_ALIGNMENT, log_offset_end-log_offset_start);
    if(ret != 0)
        return(BAKE_ERR_IO);

    /* read extent from log */
    ret = abt_io_pread(entry->abtioi, entry->log_fd, bounce_buffer,
        log_offset_end-log_offset_start, log_offset_start);
    if(ret != BAKE_ALIGNMENT)
    {
        free(bounce_buffer);
        return(BAKE_ERR_IO);
    }

    /* give caller pointer to correct offset within log extent */
    *data = bounce_buffer + (natural_offset_start-log_offset_start);
    *data_size = size;
    /* free function is special; the caller cannot free the pointer it is
     * given above since it isn't necessarily the start addr of the bounce
     * buffer
     */
    *free_data = bake_file_read_raw_free;

    return(BAKE_SUCCESS);
}

static int bake_file_read_bulk(backend_context_t context,
                               bake_region_id_t rid,
                               size_t region_offset,
                               size_t size,
                               hg_bulk_t bulk,
                               hg_addr_t source,
                               size_t bulk_offset,
                               size_t* bytes_read)
{
    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    file_region_id_t* frid = (file_region_id_t*)rid.data;
    int ret;

    ret = transfer_data(entry, frid->log_entry_offset, frid->log_entry_size,
        region_offset, bulk, bulk_offset, size, source, TRANSFER_DATA_READ);

    return(ret);
}

static int bake_file_persist(backend_context_t context,
                             bake_region_id_t rid,
                             size_t offset,
                             size_t size)
{
    bake_file_entry_t *entry = (bake_file_entry_t*)context;
    int ret;

    /* NOTE: the size and offset doesn't matter.  There isn't any reasonably
     * portable function that can be used to sync portion of a log; we have
     * to sync the whole thing.
     */
    ret = abt_io_fdatasync(entry->abtioi, entry->log_fd);
    if(ret != 0)
        return(BAKE_ERR_IO);

    return BAKE_SUCCESS;
}

static int bake_file_create_write_persist_bulk(backend_context_t context,
                                               hg_bulk_t bulk,
                                               hg_addr_t source,
                                               size_t bulk_offset,
                                               size_t size,
                                               bake_region_id_t *rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_get_region_size(backend_context_t context,
                                     bake_region_id_t rid,
                                     size_t* size)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_get_region_data(backend_context_t context,
                                     bake_region_id_t rid,
                                     void** data)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_remove(backend_context_t context,
                            bake_region_id_t rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_migrate_region(backend_context_t context,
                                    bake_region_id_t source_rid,
                                    size_t region_size,
                                    int remove_source,
                                    const char* dest_addr_str,
                                    uint16_t dest_provider_id,
                                    bake_target_id_t dest_target_id,
                                    bake_region_id_t *dest_rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_set_conf(backend_context_t context,
                              const char* key,
                              const char* value)
{
    return 0;
}

bake_backend g_bake_file_backend = {
    .name                       = "file",
    ._initialize                = bake_file_backend_initialize,
    ._finalize                  = bake_file_backend_finalize,
    ._create                    = bake_file_create,
    ._write_raw                 = bake_file_write_raw,
    ._write_bulk                = bake_file_write_bulk,
    ._read_raw                  = bake_file_read_raw,
    ._read_bulk                 = bake_file_read_bulk,
    ._persist                   = bake_file_persist,
    ._create_write_persist_raw  = NULL, /* use default implementation */
    ._create_write_persist_bulk = bake_file_create_write_persist_bulk,
    ._get_region_size           = bake_file_get_region_size,
    ._get_region_data           = bake_file_get_region_data,
    ._remove                    = bake_file_remove,
    ._migrate_region            = bake_file_migrate_region,
#ifdef USE_REMI
    ._create_fileset            = bake_file_create_fileset,
#endif
    ._set_conf                  = bake_file_set_conf
};

/* common utility function for relaying data in read_bulk/write_bulk */
static int transfer_data(
    bake_file_entry_t* entry,
    off_t log_entry_offset,
    size_t log_entry_size,
    uint64_t region_offset,
    hg_bulk_t remote_bulk,
    uint64_t remote_bulk_offset,
    uint64_t bulk_size,
    hg_addr_t src_addr,
    int op_flag)
{
    if(bulk_size+region_offset > log_entry_size)
    {
        /* caller is attempting to access more data in this region than
         * was allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }


    return(BAKE_ERR_OP_UNSUPPORTED);
}

/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* for O_DIRECT */
#define _GNU_SOURCE
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <json-c/json.h>
#include <abt-io.h>

#include "bake-config.h"
#include "bake.h"
#include "bake-rpc.h"
#include "bake-server.h"
#include "bake-provider.h"
#include "bake-backend.h"
#include "bake-macros.h"

/* bake-file-backend
 *
 * This is an implemenation of a back end for the Bake provider that stores
 * all data in normal POSIX files.  All data is stored in a single
 * block-aligned, log-structured, file and accessed using directio through
 * the abt-io library.
 */

#define BAKE_ALIGN_UP(x, _alignment) \
    ((((unsigned long)(x)) + (_alignment - 1)) & (~(_alignment - 1)))
#define BAKE_ALIGN_DOWN(x, _alignment) \
    ((unsigned long)(x) & (~(_alignment - 1)))

/* The superblock contains metadata at the front of the log.  This size is
 * not tunable; it is set when the target is created.  It must be a multiple
 * of 4k to ensure that it works with directio on most (all?) platforms.
 */
#define BAKE_SUPERBLOCK_SIZE 4096

#define TRANSFER_DATA_READ  1
#define TRANSFER_DATA_WRITE 2

/* definition of BAKE root data structure */
typedef struct {
    bake_target_id_t pool_id;
    uint32_t         nlogs;
    uint64_t         alignment;
    /* NOTE: trailing data in the superblock after the above struct is an array
     * of offsets for the current log position in each log.
     */
    off_t log_offsets[1];
} bake_root_t;

/* definition of internal BAKE region_id_t identifier for file back end */
typedef struct {
    off_t    log_entry_offset;
    size_t   log_entry_size;
    uint32_t log_index; /* which log if multiple are present */
} file_region_id_t;

typedef struct {
    char data[1];
} region_content_t;

typedef struct {
    bake_provider_t    provider;
    int*               log_fds;      /* file descriptor for log */
    int                next_log_idx; /* next log to allocate from */
    int                sync;   /* flag indicating whether to sync or not */
    abt_io_instance_id abtioi; /* abt-io instance used by this provider */
    bake_root_t*       file_root;
    ABT_mutex
        file_root_mutex; /* protects the above during concurrent meta updates */
    char* path;
} bake_file_entry_t;

typedef struct xfer_args {
    /* information about underlying target */
    bake_file_entry_t* entry;

    /* information about remote host */
    hg_addr_t remote_addr;   /* remote address */
    hg_bulk_t remote_bulk;   /* remote bulk handle for transfers */
    size_t    remote_offset; /* remote offset at which to take the data */

    /* state of region to be accessed in local log */
    int    fd;
    off_t  log_entry_offset; /* log extent to access */
    size_t log_entry_size;   /* log extent to access */
    size_t log_issued;       /* log accesses issued (bytes) */
    size_t log_retired;      /* log accesses retired (bytes) */

    /* state of network transmission */
    size_t transmit_size;          /* total amount of data to xmit */
    off_t  transmit_offset_in_log; /* what position in log to xmit first */
    size_t transmit_issued;        /* number of xmit bytes issued */
    size_t poolset_max_size;       /* max xmit size supported by poolset */

    /* state of transfer as a whole */
    int32_t      ret;         /* return code (0 on success) */
    int          ults_active; /* number of ULTs working on this xfer */
    ABT_mutex    mutex;       /* protect shared fields in this struct */
    ABT_eventual eventual;    /* signal when complete */
    int          op_flag;     /* read or write */
} xfer_args;

static int validate_and_complete_config(bake_provider_t    provider,
                                        bake_file_entry_t* new_entry);
static int transfer_data(bake_file_entry_t* entry,
                         int                fd,
                         off_t              log_entry_offset,
                         size_t             log_entry_size,
                         uint64_t           region_offset,
                         hg_bulk_t          remote_bulk,
                         uint64_t           remote_bulk_offset,
                         uint64_t           bulk_size,
                         hg_addr_t          src_addr,
                         int                op_flag);

static void xfer_ult(void* _args);

static int bake_file_makepool(const char* file_name, size_t file_size)
{
    int          fd = -1;
    bake_root_t* root;
    int          ret;
    int          oflags         = O_EXCL | O_WRONLY | O_CREAT;
    char         root_name[256] = {0};

    /* file targets are actually subdirectories (which may have multiple log
     * files within them
     */
    ret = mkdir(file_name, 0755);
    if (ret < 0) {
        perror("mkdir");
        return (BAKE_ERR_IO);
    }

    snprintf(root_name, 256, "%s/bake-file-root", file_name);

    /* NOTE: we do not use O_DIRECT here.  This fn is just creating the log and
     * is not performance sensitive.  Note that one side effect of this,
     * however, is that we won't be able to confirm if O_DIRECT is supported
     * on this storage device until the provider attaches the target.
     */
    fd = open(root_name, oflags, 0644);
    if (fd < 0) {
        perror("open");
        return (BAKE_ERR_IO);
    }

    /* we'll put a full block at the front of the file, the first bytes of
     * which will contain the bake_root_t
     */
    ret = posix_memalign((void**)(&root), BAKE_SUPERBLOCK_SIZE,
                         BAKE_SUPERBLOCK_SIZE);
    assert(ret == 0);
    memset(root, 0, BAKE_SUPERBLOCK_SIZE);

    /* store the target id for this bake pool at the root */
    uuid_generate(root->pool_id.id);
    /* the final alignment and nlogs values are not set until the target is
     * attached for the first time
     */
    root->nlogs     = 0;
    root->alignment = 0;

    ret = write(fd, root, BAKE_SUPERBLOCK_SIZE);
    if (ret != BAKE_SUPERBLOCK_SIZE) {
        perror("write");
        free(root);
        return (BAKE_ERR_IO);
    }
    free(root);

    close(fd);

    return BAKE_SUCCESS;
}

static int validate_and_complete_config(bake_provider_t    provider,
                                        bake_file_entry_t* new_entry)
{
    int64_t             check_alignment   = 0;
    int64_t             check_nlogs       = 0;
    struct json_object* file_backend_json = NULL;
    struct json_object* target_array      = NULL;
    struct json_object* val;

    if (!json_object_get_boolean(
            json_object_object_get(provider->json_cfg, "pipeline_enable"))) {
        BAKE_ERROR(provider->mid, "the bake file backend requires pipelining");
        BAKE_ERROR(provider->mid,
                   "please enable pipelining in the provider's json "
                   "configuration or with bake-server-daemon -p");
        return (BAKE_ERR_INVALID_ARG);
    }

    CONFIG_HAS_OR_CREATE_OBJECT(provider->json_cfg, "file_backend",
                                "file_backend", file_backend_json);
    CONFIG_HAS_OR_CREATE_ARRAY(file_backend_json, "targets",
                               "file_backend.targets", target_array);

    /* populate tuning parameters */

    /* number of log files (will be validated against the target root later) */
    CONFIG_HAS_OR_CREATE(file_backend_json, int64, "nlogs", 4,
                         "file_backend.nlogs", val);
    /* alignment */
    CONFIG_HAS_OR_CREATE(file_backend_json, int64, "alignment", 4096,
                         "file_backend.alignment", val);
    /* will the target be synchronized for durability (particularly when
     * persist() is called on a region)? */
    CONFIG_HAS_OR_CREATE(file_backend_json, boolean, "sync", 1,
                         "file_backend.sync", val);
    /* use directio? */
    CONFIG_HAS_OR_CREATE(file_backend_json, boolean, "directio", 1,
                         "file_backend.directio", val);

    /* check that values are sane now that json is populated */

    /* log alignment must be non-negative power of 2 */
    check_alignment = json_object_get_int(
        json_object_object_get(file_backend_json, "alignment"));
    if (check_alignment < 0) {
        BAKE_ERROR(provider->mid, "negative alignment %d", check_alignment);
        return (BAKE_ERR_INVALID_ARG);
    }
    if (!(((unsigned)check_alignment & ((unsigned)check_alignment - 1)) == 0)) {
        BAKE_ERROR(provider->mid, "alignment %d is not a power of 2 or zero",
                   check_alignment);
        return (BAKE_ERR_INVALID_ARG);
    }

    /* nlogs must be 1 or more */
    check_nlogs = json_object_get_int(
        json_object_object_get(file_backend_json, "nlogs"));
    if (check_nlogs < 1) {
        BAKE_ERROR(provider->mid, "nlogs %d must be at least 1", check_nlogs);
        return (BAKE_ERR_INVALID_ARG);
    }

    /* you can't pass in an existing abt-io instance _and_ request one with
     * a particular thread count.
     */
    if (provider->aid && CONFIG_HAS(file_backend_json, "abtio_nthreads", val)) {
        BAKE_ERROR(provider->mid,
                   "cannot pass in abt-io instance and also specify explicit "
                   "\"abtio_nthreads\" setting in json");
        return (BAKE_ERR_INVALID_ARG);
    } else if (provider->aid) {
        new_entry->abtioi = provider->aid;
    } else {
        CONFIG_HAS_OR_CREATE(file_backend_json, int64, "abtio_nthreads", 16,
                             "file_backend.abtio_nthreads", val);

        /* initialize an abt-io instance just for this target */
        new_entry->abtioi = abt_io_init(json_object_get_int(
            json_object_object_get(file_backend_json, "abtio_nthreads")));
        if (!new_entry->abtioi) { return (BAKE_ERR_IO); }
    }

    return (0);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_initialize(bake_provider_t    provider,
                                        const char*        path,
                                        bake_target_id_t*  target,
                                        backend_context_t* context)
{
    int                ret       = BAKE_SUCCESS;
    bake_file_entry_t* new_entry = calloc(1, sizeof(*new_entry));
    new_entry->provider          = provider;
    int                 root_fd  = -1;
    struct stat         statbuf;
    struct json_object* file_backend_json = NULL;
    struct json_object* target_array      = NULL;
    int                 oflags            = O_RDWR;
    char                log_name[256]     = {0};
    char                root_name[256]    = {0};
    int                 i;

    new_entry->file_root_mutex = ABT_MUTEX_NULL;

    ret = validate_and_complete_config(provider, new_entry);
    if (ret < 0) goto error_cleanup;

    file_backend_json
        = json_object_object_get(provider->json_cfg, "file_backend");
    target_array = json_object_object_get(file_backend_json, "targets");

    /* populate some runtime parameters so we don't have to consult json
     * within I/O path
     */
    new_entry->path = strdup(path);
    new_entry->sync = json_object_get_boolean(
        json_object_object_get(file_backend_json, "sync"));
    if (json_object_get_boolean(
            json_object_object_get(file_backend_json, "directio"))) {
        BAKE_DEBUG(provider->mid, "adding O_DIRECT to flags");
        oflags |= O_DIRECT;
    }
    ABT_mutex_create(&new_entry->file_root_mutex);

    snprintf(root_name, 256, "%s/bake-file-root", path);
    root_fd = abt_io_open(new_entry->abtioi, root_name, oflags, 0);
    if ((root_fd == -EINVAL) && (oflags & O_DIRECT)) {
        /* It looks like we may have failed to open the log because of
         * directio.  Try falling back without it */
        oflags &= ~O_DIRECT;
        root_fd = abt_io_open(new_entry->abtioi, root_name, oflags, 0);
        if (root_fd >= 0) {
            /* The user requested directio, but we are proceeding without
             * it.  Issue a warning and update runtime json.
             */
            json_object_set_boolean(
                json_object_object_get(file_backend_json, "directio"), 0);
            BAKE_WARNING(
                provider->mid,
                "O_DIRECT not supported on target %s; disabling directio",
                path);
        }
    }

    if (root_fd < 0) {
        BAKE_ERROR(provider->mid, "open(): %s on %s", strerror(-root_fd),
                   root_name);
        ret = BAKE_ERR_NOENT;
        goto error_cleanup;
    }

    /* read root superblock */
    ret = posix_memalign((void**)(&new_entry->file_root), BAKE_SUPERBLOCK_SIZE,
                         BAKE_SUPERBLOCK_SIZE);
    if (ret < 0) {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    ret = abt_io_pread(new_entry->abtioi, root_fd, new_entry->file_root,
                       BAKE_SUPERBLOCK_SIZE, 0);
    if (ret < 0) {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    *target = new_entry->file_root->pool_id;

    /* check that superblock is valid and that it matches runtime parameters */
    if (uuid_is_null(target->id)) {
        BAKE_ERROR(provider->mid, "pool %s is not properly formatted", path);
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }

    /* some parameters are not set until target is attached */
    if (new_entry->file_root->nlogs == 0)
        new_entry->file_root->nlogs = json_object_get_int(
            json_object_object_get(file_backend_json, "nlogs"));
    if (new_entry->file_root->alignment == 0)
        new_entry->file_root->alignment = json_object_get_int(
            json_object_object_get(file_backend_json, "alignment"));

    /* do json and root agree? */
    if (new_entry->file_root->nlogs
        != json_object_get_int(
            json_object_object_get(file_backend_json, "nlogs"))) {
        BAKE_ERROR(provider->mid,
                   "pool %s has nlogs set to %d but provider configuration has "
                   "nlogs set to %d",
                   path, new_entry->file_root->nlogs,
                   json_object_get_int(
                       json_object_object_get(file_backend_json, "nlogs")));
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }
    if (new_entry->file_root->alignment
        != json_object_get_int(
            json_object_object_get(file_backend_json, "alignment"))) {
        BAKE_ERROR(provider->mid,
                   "pool %s has alignment set to %d but provider configuration "
                   "has alignment set to %d",
                   path, new_entry->file_root->alignment,
                   json_object_get_int(
                       json_object_object_get(file_backend_json, "alignment")));
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }

    /* Track log offsets using memory in root superblock; will be flushed at
     * shutdown so that we have persistent record if possible.
     */
    new_entry->log_fds
        = calloc(new_entry->file_root->nlogs, sizeof(*new_entry->log_fds));
    if (!new_entry->log_fds) {
        ret = BAKE_ERR_NOMEM;
        goto error_cleanup;
    }
    for (i = 0; i < new_entry->file_root->nlogs; i++) {
        new_entry->log_fds[i] = -1;
    }

    /* open logs */
    for (i = 0; i < new_entry->file_root->nlogs; i++) {
        snprintf(log_name, 256, "%s/log.%d", path, i);
        new_entry->log_fds[i]
            = abt_io_open(new_entry->abtioi, log_name, oflags | O_CREAT, 0644);
        if (new_entry->log_fds < 0) {
            ret = BAKE_ERR_IO;
            goto error_cleanup;
        }

        /* TODO: this logic will be replaced; rely on what's in root superblock
         * first if possible, then fall back to fstat
         */
        /* check size of log to see where to pick up with new entries */
        ret = fstat(new_entry->log_fds[i], &statbuf);
        if (ret < 0) {
            perror("fstat");
            ret = BAKE_ERR_IO;
            goto error_cleanup;
        }
        new_entry->file_root->log_offsets[i] = statbuf.st_size;
    }

    /* select a random log to use as the next one to allocate from */
    srand(time(NULL));
    new_entry->next_log_idx = rand() % new_entry->file_root->nlogs;

    /* save superblock updates */
    ret = abt_io_pwrite(new_entry->abtioi, root_fd, new_entry->file_root,
                        BAKE_SUPERBLOCK_SIZE, 0);
    if (ret < 0) {
        ret = BAKE_ERR_IO;
        goto error_cleanup;
    }

    /* target successfully added; inject it into the json in array of
     * targets for this backend
     */
    json_object_array_add(target_array, json_object_new_string(path));

    BAKE_WARNING(provider->mid,
                 "bake file backend does not yet support the following:");
    BAKE_WARNING(provider->mid, "    * writes to non-zero region offsets");

    *context = new_entry;
    return 0;

error_cleanup:
    if (new_entry) {
        if (new_entry->file_root) free(new_entry->file_root);
        if (new_entry->abtioi && new_entry->abtioi != provider->aid)
            abt_io_finalize(new_entry->abtioi);
        if (new_entry->path) free(new_entry->path);
        if (new_entry->log_fds) {
            for (i = 0; i < new_entry->file_root->nlogs; i++) {
                if (new_entry->log_fds[i] > -1) close(new_entry->log_fds[i]);
            }
            free(new_entry->log_fds);
        }
        if (new_entry->file_root_mutex != ABT_MUTEX_NULL)
            ABT_mutex_free(&new_entry->file_root_mutex);
        free(new_entry);
    }
    return (ret);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_backend_finalize(backend_context_t context)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    int                i;

    BAKE_INFO(entry->provider->mid, "Bake file backend finalizing");

    free(entry->file_root);
    if (entry->abtioi && entry->abtioi != entry->provider->aid)
        abt_io_finalize(entry->abtioi);
    free(entry->path);
    for (i = 0; i < entry->file_root->nlogs; i++) { close(entry->log_fds[i]); }
    free(entry->log_fds);

    free(entry);

    return BAKE_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////////////////
static int
bake_file_create(backend_context_t context, size_t size, bake_region_id_t* rid)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    int                ret;
    void*              zero_block;
    file_region_id_t*  frid = (file_region_id_t*)rid->data;

    assert(sizeof(file_region_id_t) <= BAKE_REGION_ID_DATA_SIZE);

    /* round up size for directio alignment */
    size = BAKE_ALIGN_UP(size, entry->file_root->alignment);

    frid->log_entry_size = size;

    ABT_mutex_lock(entry->file_root_mutex);
    frid->log_index     = entry->next_log_idx;
    entry->next_log_idx = (entry->next_log_idx + 1) % entry->file_root->nlogs;
    frid->log_entry_offset = entry->file_root->log_offsets[frid->log_index];
    entry->file_root->log_offsets[frid->log_index] += size;
    ABT_mutex_unlock(entry->file_root_mutex);

    /* TODO: replace this; track sizes in root superblock at runtime */

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
    ret = posix_memalign(&zero_block, entry->file_root->alignment,
                         entry->file_root->alignment);
    if (ret != 0) return (BAKE_ERR_IO);

    ret = abt_io_pwrite(entry->abtioi, entry->log_fds[frid->log_index],
                        zero_block, entry->file_root->alignment,
                        frid->log_entry_offset + size
                            - entry->file_root->alignment);
    if (ret != entry->file_root->alignment) {
        free(zero_block);
        return (BAKE_ERR_IO);
    }

    if (entry->sync) {
        ret = abt_io_fdatasync(entry->abtioi, entry->log_fds[frid->log_index]);
        if (ret != 0) {
            free(zero_block);
            return (BAKE_ERR_IO);
        }
    }

    free(zero_block);
    return (BAKE_SUCCESS);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_raw(backend_context_t context,
                               bake_region_id_t  rid,
                               size_t            offset,
                               size_t            size,
                               const void*       data)
{
    /* NOTES:
     * - this routine is most likely called in the eager write path
     * - the data buffer is already present, and is probably small, but it
     *   is very unlikely that the offset and size are both page aligned
     * - we therefore create an intermediate aligned buffer to copy through
     *   and write to the log
     */

    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    file_region_id_t*  frid  = (file_region_id_t*)rid.data;
    void*              bounce_buffer;
    int                ret;

    /* TODO: implement this.  For now we only handle writes beginning at
     * offset zero of a region.  Writes that begin elsewhere will require a
     * r/m/w to handle correctly, since there is no requirement that bake
     * write offsets are aligned.
     */
    if (offset != 0) {
        BAKE_ERROR(entry->provider->mid,
                   "bake file backend does not yet support writes to non-zero "
                   "region offsets");
        return (BAKE_ERR_OP_UNSUPPORTED);
    }

    if (size + offset > frid->log_entry_size) {
        /* caller is attempting to write more data into this region than was
         * allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }

    ret = posix_memalign(&bounce_buffer, entry->file_root->alignment,
                         BAKE_ALIGN_UP(size, entry->file_root->alignment));
    if (ret != 0) return (BAKE_ERR_IO);

    memcpy(bounce_buffer, data, size);

    ret = abt_io_pwrite(entry->abtioi, entry->log_fds[frid->log_index],
                        bounce_buffer,
                        BAKE_ALIGN_UP(size, entry->file_root->alignment),
                        frid->log_entry_offset);
    if (ret != entry->file_root->alignment) {
        free(bounce_buffer);
        return (BAKE_ERR_IO);
    }

    free(bounce_buffer);

    return (BAKE_SUCCESS);
}

////////////////////////////////////////////////////////////////////////////////////////////
static int bake_file_write_bulk(backend_context_t context,
                                bake_region_id_t  rid,
                                size_t            region_offset,
                                size_t            size,
                                hg_bulk_t         bulk,
                                hg_addr_t         source,
                                size_t            bulk_offset)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    file_region_id_t*  frid  = (file_region_id_t*)rid.data;
    int                ret;

    /* TODO: implement this.  For now we only handle writes beginning at
     * offset zero of a region.  Writes that begin elsewhere will require a
     * r/m/w to handle correctly, since there is no requirement that bake
     * write offsets are aligned.
     */
    if (region_offset != 0) {
        BAKE_ERROR(entry->provider->mid,
                   "bake file backend does not yet support writes to non-zero "
                   "region offsets");
        return (BAKE_ERR_OP_UNSUPPORTED);
    }

    ret = transfer_data(entry, entry->log_fds[frid->log_index],
                        frid->log_entry_offset, frid->log_entry_size,
                        region_offset, bulk, bulk_offset, size, source,
                        TRANSFER_DATA_WRITE);

    return (ret);
}

/* utility function used to free bounce buffers created by
 * bake_file_read_raw().  It is like a normal free() except that it must
 * round down to block alignment to find the correct pointer to free.
 */
static void bake_file_read_raw_free(backend_context_t context, void* ptr)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    free((void*)(BAKE_ALIGN_DOWN(ptr, entry->file_root->alignment)));
    return;
}

static int bake_file_read_raw(backend_context_t context,
                              bake_region_id_t  rid,
                              size_t            offset,
                              size_t            size,
                              void**            data,
                              uint64_t*         data_size,
                              free_fn*          free_data)
{
    /* NOTES:
     * - this routine is most likely called in the eager read path
     * - the api provides both a buffer pointer and a free function pointer
     *   to the caller. We take advantage of that to account for alignment
     *   within the log and bounce buffer.
     * - doing this with a single intermediate buffer and one I/O operation
     *   is fine, as we expect this to be a small access.
     */

    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    file_region_id_t*  frid  = (file_region_id_t*)rid.data;
    void*              bounce_buffer;
    int                ret;
    off_t              natural_offset_start, natural_offset_end;
    off_t              log_offset_start, log_offset_end;

    if (size + offset > frid->log_entry_size) {
        /* caller is attempting to read more data from this region than was
         * allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }

    /* not counting alignment, what portion of the log do we want? */
    natural_offset_start = frid->log_entry_offset + offset;
    natural_offset_end   = natural_offset_start + size;
    /* align both to find log extent */
    log_offset_start
        = BAKE_ALIGN_DOWN(natural_offset_start, entry->file_root->alignment);
    log_offset_end
        = BAKE_ALIGN_UP(natural_offset_end, entry->file_root->alignment);

    /* create aligned bounce buffer large enough to hold log extent */
    ret = posix_memalign(&bounce_buffer, entry->file_root->alignment,
                         log_offset_end - log_offset_start);
    if (ret != 0) return (BAKE_ERR_IO);

    /* read extent from log */
    ret = abt_io_pread(entry->abtioi, entry->log_fds[frid->log_index],
                       bounce_buffer, log_offset_end - log_offset_start,
                       log_offset_start);
    if (ret != entry->file_root->alignment) {
        free(bounce_buffer);
        return (BAKE_ERR_IO);
    }

    /* give caller pointer to correct offset within log extent */
    *data      = bounce_buffer + (natural_offset_start - log_offset_start);
    *data_size = size;
    /* free function is special; the caller cannot free the pointer it is
     * given above since it isn't necessarily the start addr of the bounce
     * buffer
     */
    *free_data = bake_file_read_raw_free;

    return (BAKE_SUCCESS);
}

static int bake_file_read_bulk(backend_context_t context,
                               bake_region_id_t  rid,
                               size_t            region_offset,
                               size_t            size,
                               hg_bulk_t         bulk,
                               hg_addr_t         source,
                               size_t            bulk_offset,
                               size_t*           bytes_read)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    file_region_id_t*  frid  = (file_region_id_t*)rid.data;
    int                ret;

    ret = transfer_data(entry, entry->log_fds[frid->log_index],
                        frid->log_entry_offset, frid->log_entry_size,
                        region_offset, bulk, bulk_offset, size, source,
                        TRANSFER_DATA_READ);

    return (ret);
}

static int bake_file_persist(backend_context_t context,
                             bake_region_id_t  rid,
                             size_t            offset,
                             size_t            size)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    int                ret;
    file_region_id_t*  frid = (file_region_id_t*)rid.data;

    if (entry->sync) {
        /* NOTE: the size and offset doesn't matter.  There isn't any reasonably
         * portable function that can be used to sync portion of a log; we have
         * to sync the whole thing.
         */
        ret = abt_io_fdatasync(entry->abtioi, entry->log_fds[frid->log_index]);
        if (ret != 0) return (BAKE_ERR_IO);
    }

    return BAKE_SUCCESS;
}

static int bake_file_get_region_size(backend_context_t context,
                                     bake_region_id_t  rid,
                                     size_t*           size)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_get_region_data(backend_context_t context,
                                     bake_region_id_t  rid,
                                     void**            data)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

static int bake_file_remove(backend_context_t context, bake_region_id_t rid)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    file_region_id_t*  frid  = (file_region_id_t*)rid.data;
    int                ret;

    /* Rationale:
     *
     * All regions are stored in a single unified log, and indexed by their
     * offset into that log.  To remove an entry, we therefore punch a hole
     * in the log so that the underlying file system can deallocate the
     * associated blocks without perturbing the position of other log
     * elements.
     *
     * The block-level punch is likely to succeed (on file systems that
     * support this operation) because we are using directio and each region
     * is perfectly block aligned.
     *
     * The log could be defragmented, but that would be a higher level
     * opertion.
     */
    ret = abt_io_fallocate(entry->abtioi, entry->log_fds[frid->log_index],
                           FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                           frid->log_entry_offset, frid->log_entry_size);

    return (ret);
}

static int bake_file_migrate_region(backend_context_t context,
                                    bake_region_id_t  source_rid,
                                    size_t            region_size,
                                    int               remove_source,
                                    const char*       dest_addr_str,
                                    uint16_t          dest_provider_id,
                                    bake_target_id_t  dest_target_id,
                                    bake_region_id_t* dest_rid)
{
    return BAKE_ERR_OP_UNSUPPORTED;
}

#ifdef USE_REMI
static int bake_file_create_fileset(backend_context_t context,
                                    remi_fileset_t*   fileset)
{
    bake_file_entry_t* entry = (bake_file_entry_t*)context;
    int                ret;
    char               log_name[256] = {0};
    int                i;

    /* create a fileset */
    ret = remi_fileset_create("bake", entry->path, fileset);
    if (ret != REMI_SUCCESS) {
        ret = BAKE_ERR_REMI;
        goto error;
    }

    /* fill the fileset */
    /* superblock first */
    ret = remi_fileset_register_file(*fileset, "bake-file-root");
    if (ret != REMI_SUCCESS) {
        ret = BAKE_ERR_REMI;
        goto error;
    }
    /* all logs */
    for (i = 0; i < entry->file_root->nlogs; i++) {
        /* note that log name does not include directory path here */
        snprintf(log_name, 256, "log.%d", i);
        ret = remi_fileset_register_file(*fileset, log_name);
        if (ret != REMI_SUCCESS) {
            ret = BAKE_ERR_REMI;
            goto error;
        }
    }

finish:
    return ret;
error:
    remi_fileset_free(*fileset);
    *fileset = NULL;
    goto finish;
}
#endif

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
    ._create_write_persist_bulk = NULL, /* use default implementation */
    ._get_region_size           = bake_file_get_region_size,
    ._get_region_data           = bake_file_get_region_data,
    ._remove                    = bake_file_remove,
    ._migrate_region            = bake_file_migrate_region,
    ._create_raw_target         = bake_file_makepool,
#ifdef USE_REMI
    ._create_fileset = bake_file_create_fileset,
#endif
};

/* common utility function for relaying data in read_bulk/write_bulk */
static int transfer_data(bake_file_entry_t* entry,
                         int                fd,
                         off_t              log_entry_offset,
                         size_t             log_entry_size,
                         uint64_t           region_offset,
                         hg_bulk_t          remote_bulk,
                         uint64_t           remote_bulk_offset,
                         uint64_t           bulk_size,
                         hg_addr_t          src_addr,
                         int                op_flag)
{
    off_t            log_end_offset;
    struct xfer_args xargs = {0};
    int              i;

    if (bulk_size + region_offset > log_entry_size) {
        /* caller is attempting to access more data in this region than
         * was allocated for at creation time
         */
        return BAKE_ERR_OUT_OF_BOUNDS;
    }

    /* where in the log do we stop access? */
    log_end_offset
        = log_entry_offset + region_offset + bulk_size - remote_bulk_offset;
    log_end_offset = BAKE_ALIGN_UP(log_end_offset, entry->file_root->alignment);

    xargs.entry            = entry;
    xargs.remote_addr      = src_addr;
    xargs.remote_bulk      = remote_bulk;
    xargs.remote_offset    = remote_bulk_offset;
    xargs.log_entry_offset = BAKE_ALIGN_DOWN(log_entry_offset + region_offset,
                                             entry->file_root->alignment);
    xargs.fd               = fd;
    xargs.log_entry_size   = log_end_offset - xargs.log_entry_offset;
    xargs.log_issued       = 0;
    xargs.log_retired      = 0;
    xargs.transmit_size    = bulk_size - remote_bulk_offset;
    xargs.transmit_offset_in_log
        = log_entry_offset + region_offset - xargs.log_entry_offset;
    xargs.transmit_issued = 0;
    margo_bulk_poolset_get_max(entry->provider->poolset,
                               &xargs.poolset_max_size);
    xargs.ret         = 0;
    xargs.ults_active = 0;
    xargs.op_flag     = op_flag;
    ABT_mutex_create(&xargs.mutex);
    ABT_eventual_create(0, &xargs.eventual);

    /* divide amount to be accessed in log by max poolset size and create
     * one ULT per chunk
     */
    for (i = 0; i < xargs.log_entry_size; i += xargs.poolset_max_size)
        xargs.ults_active++;
    for (i = 0; i < xargs.log_entry_size; i += xargs.poolset_max_size) {
        /* NOTE: deliberately set output tid to NULL to ignore.  The last
         * thread out of this set to complete will signal eventual below,
         * rather than joining
         */
        ABT_thread_create(entry->provider->handler_pool, xfer_ult, &xargs,
                          ABT_THREAD_ATTR_NULL, NULL);
    }

    ABT_eventual_wait(xargs.eventual, NULL);
    ABT_eventual_free(&xargs.eventual);

    /* consolidated error code (0 if all successful, otherwise first
     * non-zero error code)
     */
    return (xargs.ret);
}

/* worker function for each ULT involved in a transfer */
static void xfer_ult(void* _args)
{
    struct xfer_args* args = _args;

    /* Variables with a this_ prefix are used to describe the
     * specific extent that this ULT is working on at a given time (as
     * opposed to the mutex-locked shared state tracked in the xfer_args).
     */
    size_t this_log_size;
    size_t this_transmit_size;
    off_t  this_log_offset;
    off_t  this_transmit_offset_in_log = 0;
    size_t this_remote_offset;

    /* references to local RDMA region */
    hg_bulk_t local_bulk = HG_BULK_NULL;
    void*     local_bulk_ptr;

    /* flag to indicate if this ULT is the one responsible for cleaning up
     * the transfer state on completion
     */
    int turn_out_the_lights = 0;

    /* misc */
    size_t      tmp_buf_size;
    hg_uint32_t tmp_count;
    int         ret;

    /* Each ULT runs a loop here to find work to do.  We don't care which
     * ULTs get there first.  The general strategy of the loop is to loop
     * over the entire log extent that needs to be accessed and then filter
     * out the parts that need to be trasmitted to client.  File alignment
     * is stricter (because we are using directio) and is a superset of the
     * data to be transmitted.
     */
    /* Hold lock going into loop so that this ULT can pull of it's unit of
     * work to operate on
     */
    ABT_mutex_lock(args->mutex);
    /* have we issued all of the necessary file operations or hit an error
     * already?
     */
    while (args->log_issued < args->log_entry_size && !args->ret) {
        /* calculate what extent to work on in this cycle, both in file and
         * in terms of remote transmission
         */
        if ((args->log_entry_size - args->log_issued) > args->poolset_max_size)
            this_log_size = args->poolset_max_size;
        else
            this_log_size = args->log_entry_size - args->log_issued;
        this_log_offset    = args->log_entry_offset + args->log_issued;
        this_remote_offset = args->remote_offset + args->transmit_issued;
        this_transmit_size = this_log_size;
        if (args->transmit_issued == 0) {
            /* first network transmission */
            /* skip unused part of first block, if present */
            this_transmit_size -= args->transmit_offset_in_log;
            this_transmit_offset_in_log = args->transmit_offset_in_log;
        } else {
            this_transmit_offset_in_log = 0;
        }
        /* truncate transmission at the end if needed */
        if ((this_transmit_size + args->transmit_issued) > args->transmit_size)
            this_transmit_size = args->transmit_size - args->transmit_issued;

        /* update shared state for the transfer */
        args->log_issued += this_log_size;
        args->transmit_issued += this_transmit_size;

        /* drop mutex while we work on our local piece */
        ABT_mutex_unlock(args->mutex);

        /* get buffer */
        /* this will block until a buffer is available if pool is exhausted */
        ret = margo_bulk_poolset_get(args->entry->provider->poolset,
                                     this_log_size, &local_bulk);
        if (ret != 0 && args->ret == 0) {
            args->ret = ret;
            goto finished;
        }
        /* find pointer of memory in buffer */
        ret = margo_bulk_access(local_bulk, 0, this_log_size, HG_BULK_READWRITE,
                                1, &local_bulk_ptr, &tmp_buf_size, &tmp_count);
        /* shouldn't ever fail in this scenario */
        assert(ret == 0);

        /* margo pool buffers are supposed to be page aligned already.  Just
         * safety checking here.
         */
        assert((long unsigned)local_bulk_ptr % 4096 == 0);

        if (args->op_flag == TRANSFER_DATA_WRITE) {
            /* rdma transfer */
            ret = margo_bulk_transfer(args->entry->provider->mid, HG_BULK_PULL,
                                      args->remote_addr, args->remote_bulk,
                                      this_remote_offset, local_bulk, 0,
                                      this_transmit_size);
            if (ret != 0 && args->ret == 0) {
                args->ret = ret;
                goto finished;
            }

            /* relay to log */
            ret = abt_io_pwrite(args->entry->abtioi, args->fd, local_bulk_ptr,
                                this_log_size, this_log_offset);
            if (ret != this_log_size && args->ret == 0) {
                args->ret = ret;
                goto finished;
            }
        } else if (args->op_flag == TRANSFER_DATA_READ) {
            /* read from log */
            ret = abt_io_pread(args->entry->abtioi, args->fd, local_bulk_ptr,
                               this_log_size, this_log_offset);
            if (ret != this_log_size && args->ret == 0) {
                args->ret = ret;
                goto finished;
            }

            /* rdma transfer */
            ret = margo_bulk_transfer(
                args->entry->provider->mid, HG_BULK_PUSH, args->remote_addr,
                args->remote_bulk, this_remote_offset, local_bulk,
                this_transmit_offset_in_log, this_transmit_size);
            if (ret != 0 && args->ret == 0) {
                args->ret = ret;
                goto finished;
            }
        } else
            assert(0);

        /* let go of bulk handle (we'll re-acquire one on next loop
         * iteration if we have more work to do) */
        margo_bulk_poolset_release(args->entry->provider->poolset, local_bulk);
        local_bulk = HG_BULK_NULL;

        ABT_mutex_lock(args->mutex);
        args->log_retired += this_log_size;
    }

    ABT_mutex_unlock(args->mutex);

finished:
    if (local_bulk != HG_BULK_NULL)
        margo_bulk_poolset_release(args->entry->provider->poolset, local_bulk);
    ABT_mutex_lock(args->mutex);
    args->ults_active--;
    /* The ULT that sets active to zero is the last one that can possibly
     * hold this mutex
     */
    if (!args->ults_active) turn_out_the_lights = 1;
    ABT_mutex_unlock(args->mutex);

    /* last ULT to exit cleans up remaining resources and signals caller */
    if (turn_out_the_lights) {
        ABT_mutex_free(&args->mutex);
        ABT_eventual_set(args->eventual, NULL, 0);
    }

    return;
}

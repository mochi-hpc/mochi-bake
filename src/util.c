/*
 * (C) 2019 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include "bake-config.h"
#include "bake.h"
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <libpmemobj.h>
#include "base64/b64.h"

static char* bake_err_str(int ret)
{
    switch (ret) {
    case BAKE_SUCCESS:
        return "Success";
        break;
    case BAKE_ERR_ALLOCATION:
        return "Error allocating something";
        break;
    case BAKE_ERR_INVALID_ARG:
        return "An argument is invalid";
        break;
    case BAKE_ERR_MERCURY:
        return "An error happened calling a Mercury function";
        break;
    case BAKE_ERR_ARGOBOTS:
        return "An error happened calling an Argobots function";
        break;
    case BAKE_ERR_PMEM:
        return "An error happened calling a PMDK function";
        break;
    case BAKE_ERR_UNKNOWN_TARGET:
        return "Target refered to by id is not know to provider";
        break;
    case BAKE_ERR_UNKNOWN_PROVIDER:
        return "Provider id could not be matched with a provider";
        break;
    case BAKE_ERR_UNKNOWN_REGION:
        return "Region id could not be found";
        break;
    case BAKE_ERR_OUT_OF_BOUNDS:
        return "Attempting an out of bound access";
        break;
    case BAKE_ERR_REMI:
        return "Error related to REMI";
        break;
    case BAKE_ERR_OP_UNSUPPORTED:
        return "Operation not supported";
        break;
    case BAKE_ERR_NOENT:
        return "Entry does not exist";
        break;
    case BAKE_ERR_EXIST:
        return "Entry already exists";
        break;
    default:
        return "Unknown error";
        break;
    }
}
void bake_perror(const char* s, int err)
{
    char  error_string[256];
    char* p;
    int   ret;
    ret = snprintf(error_string, 256, "%s", s);
    p   = error_string + ret;
    snprintf(p, 256 - ret, " (%d) %s", err, bake_err_str(err));
    error_string[255] = '\0';
    fprintf(stderr, "%s\n", error_string);
}

void bake_print_dbg_region_id_t(char* str, size_t size, bake_region_id_t rid)
{
    PMEMoid* oid;

    /* NOTE: this is fragile.  Would break if pmemobj format changes. */
    oid = (PMEMoid*)rid.data;

    snprintf(str, size, "%u:%" PRIu64 ":%" PRIu64, rid.type, oid->pool_uuid_lo,
             oid->off);

    return;
}

int bake_target_id_to_string(bake_target_id_t tid, char* str, size_t size)
{
    if (size < 37) return BAKE_ERR_INVALID_ARG;
    uuid_unparse(tid.id, str);
    return BAKE_SUCCESS;
}

int bake_target_id_from_string(const char* str, bake_target_id_t* tid)
{
    if (uuid_parse(str, tid->id) != 0) return BAKE_ERR_INVALID_ARG;
    return BAKE_SUCCESS;
}

int bake_region_id_to_string(bake_region_id_t rid, char* str, size_t size)
{
    char* s = bake_b64_encode((const unsigned char*)&rid, sizeof(rid));
    if (size < strlen(s) + 1) {
        free(s);
        return BAKE_ERR_INVALID_ARG;
    }
    strcpy(str, s);
    free(s);
    return BAKE_SUCCESS;
}

int bake_region_id_from_string(const char* str, bake_region_id_t* tid)
{
    unsigned char* data = bake_b64_decode(str, strlen(str));
    memcpy(tid, data, sizeof(*tid));
    free(data);
    return BAKE_SUCCESS;
}

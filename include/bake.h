/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_H
#define __BAKE_H

#include <uuid/uuid.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uuid_t id;
} bake_target_id_t;
/**
 * Persistent, opaque identifier for a region within a BAKE target.
 */
#define BAKE_REGION_ID_DATA_SIZE 16
typedef struct {
    uint32_t type;
    char data[BAKE_REGION_ID_DATA_SIZE];
} bake_region_id_t;

#define BAKE_SUCCESS                0    /* Success */
#define BAKE_ERR_ALLOCATION         (-1) /* Error allocating something */
#define BAKE_ERR_INVALID_ARG        (-2) /* An argument is invalid */
#define BAKE_ERR_MERCURY            (-3) /* An error happened calling a Mercury function */
#define BAKE_ERR_ARGOBOTS           (-4) /* An error happened calling an Argobots function */
#define BAKE_ERR_PMEM               (-5) /* An error happened calling a pmem function */
#define BAKE_ERR_UNKNOWN_TARGET     (-6) /* Target refered to by id is not known to provider */
#define BAKE_ERR_UNKNOWN_PROVIDER   (-7) /* Provider id could not be matched with a provider */
#define BAKE_ERR_UNKNOWN_REGION     (-8) /* Region id could not be found */
#define BAKE_ERR_OUT_OF_BOUNDS      (-9) /* Attempting an out of bound access */
#define BAKE_ERR_REMI              (-10) /* Error related to REMI */
#define BAKE_ERR_OP_UNSUPPORTED    (-11) /* Operation not supported */
#define BAKE_ERR_FORBIDDEN         (-12) /* Forbidden operation */
#define BAKE_ERR_BACKEND_TYPE      (-13) /* Unknown backend type */
#define BAKE_ERR_END               (-14) /* End of valid bake error codes */

/**
 * Print bake errors in human-friendly form
 *
 * @param a string to print out before the error
 * @param ret error code from a bake routine
 */
void bake_perror(char *s, int ret);

/**
 * @brief Converts a target id into an ASCII readable string.
 *
 * @param tid Target id to convert into a string.
 * @param str Resulting string (must be allocated to at least 37 bytes)
 * @param size size of the allocated string.
 *
 * @return error code.
 */
int bake_target_id_to_string(bake_target_id_t tid, char* str, size_t size);

/**
 * @brief Converts an ASCI readable representation of the target id into
 * and actual target id.
 *
 * @param str Null-terminated string to read from.
 * @param tid Resulting target id.
 *
 * @return error code.
 */
int bake_target_id_from_string(const char* str, bake_target_id_t* tid);

/**
 * @brief Converts the region id into an ASCII readable representation.
 *
 * @param rid Region id.
 * @param str Resulting string, should be preallocated wirg sufficient size.
 * @param size size of the preallocated string.
 *
 * @return error code.
 */
int bake_region_id_to_string(bake_region_id_t rid, char* str, size_t size);

/**
 * @brief Converts a string back into a region id.
 *
 * @param str String to convert.
 * @param rid Resulting region id.
 *
 * @return error code.
 */
int bake_region_id_from_string(const char* str, bake_region_id_t* rid);

/**
 * Convert region id into printable string for debugging purposes
 *
 * @param[in] str string to fill in
 * @param[in] size length of string, not including terminator.  If rid
 *                 string is longer than this it will be truncated.
 * @param[in] rid region_id
 */
void bake_print_dbg_region_id_t(char *str, size_t size, bake_region_id_t rid)
    __attribute__((deprecated("use bake_region_id_to_string instead")));

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_H */

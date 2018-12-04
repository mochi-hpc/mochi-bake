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

#ifdef __cplusplus
}
#endif

#endif /* __BAKE_H */

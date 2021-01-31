/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __BAKE_MACROS
#define __BAKE_MACROS

#include <json-c/json.h>

static const int json_type_int64 = json_type_int;

// Checks if a JSON object has a particular key and its value is of the
// specified type (not array or object or null). If the field does not exist,
// creates it with the provided value.. If the field exists but is not of type
// object, prints an error and return -1. After a call to this macro, __out is
// set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE(__config, __type, __key, __value, __fullname, \
                             __out)                                        \
    do {                                                                   \
        __out = json_object_object_get(__config, __key);                   \
        if (__out && !json_object_is_type(__out, json_type_##__type)) {    \
            fprintf(stderr,                                                \
                    "\"%s\" in configuration but has an incorrect type "   \
                    "(expected %s)",                                       \
                    __fullname, #__type);                                  \
            return -1;                                                     \
        }                                                                  \
        if (!__out) {                                                      \
            __out = json_object_new_##__type(__value);                     \
            json_object_object_add(__config, __key, __out);                \
        }                                                                  \
    } while (0)

// Checks if a JSON object has a particular key and its value is of type object.
// If the field does not exist, creates it with an empty object.
// If the field exists but is not of type object, prints an error and return -1.
// After a call to this macro, __out is set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE_OBJECT(__config, __key, __fullname, __out)        \
    do {                                                                       \
        __out = json_object_object_get(__config, __key);                       \
        if (__out && !json_object_is_type(__out, json_type_object)) {          \
            fprintf(stderr, "\"%s\" is in configuration but is not an object", \
                    __fullname);                                               \
            return -1;                                                         \
        }                                                                      \
        if (!__out) {                                                          \
            __out = json_object_new_object();                                  \
            json_object_object_add(__config, __key, __out);                    \
        }                                                                      \
    } while (0)

// Checks if a JSON object has a particular key and its value is of type array.
// If the field does not exist, creates it with an empty array.
// If the field exists but is not of type object, prints an error and return -1.
// After a call to this macro, __out is set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE_ARRAY(__config, __key, __fullname, __out)        \
    do {                                                                      \
        __out = json_object_object_get(__config, __key);                      \
        if (__out && !json_object_is_type(__out, json_type_array)) {          \
            fprintf(stderr, "\"%s\" is in configuration but is not an array", \
                    __fullname);                                              \
            return -1;                                                        \
        }                                                                     \
        if (!__out) {                                                         \
            __out = json_object_new_array();                                  \
            json_object_object_add(__config, __key, __out);                   \
        }                                                                     \
    } while (0)

// Can be used in configurations to check if a JSON object has a particular
// field. If it does, the __out parameter is set to that field.
#define CONFIG_HAS(__config, __key, __out) \
    ((__out = json_object_object_get(__config, __key)) != NULL)

#endif /* __BAKE_MACROS */

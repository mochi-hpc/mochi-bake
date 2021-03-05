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

#define json_array_foreach(__array, __index, __element)                \
    for (__index = 0;                                                  \
         __index < json_object_array_length(__array)                   \
         && (__element = json_object_array_get_idx(__array, __index)); \
         __index++)

// Overrides a field with a string. If the field already existed and was
// different from the new value, and __warning is true, prints a warning.
#define CONFIG_OVERRIDE_STRING(__config, __key, __value, __fullname,         \
                               __warning)                                    \
    do {                                                                     \
        struct json_object* _tmp = json_object_object_get(__config, __key);  \
        if (_tmp && __warning) {                                             \
            if (!json_object_is_type(_tmp, json_type_string))                \
                BAKE_WARNING(0, "Overriding field \"%s\" with value \"%s\"", \
                             __fullname, __value);                           \
            else if (strcmp(json_object_get_string(_tmp), __value) != 0)     \
                BAKE_WARNING(                                                \
                    0, "Overriding field \"%s\" (\"%s\") with value \"%s\"", \
                    __fullname, json_object_get_string(_tmp), __value);      \
        }                                                                    \
        _tmp = json_object_new_string(__value);                              \
        json_object_object_add(__config, __key, _tmp);                       \
    } while (0)

#endif /* __BAKE_MACROS */

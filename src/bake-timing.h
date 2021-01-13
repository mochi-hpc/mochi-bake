/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __BAKE_TIMING_H
#define __BAKE_TIMING_H

#define _GNU_SOURCE
#include <math.h>

#include "bake-config.h"

#ifdef ENABLE_TIMING

    #define TIMERS_INITIALIZE(...)                                            \
        static const char* __timer_names[] = {__VA_ARGS__, NULL};             \
        double*            __timer_values                                     \
            = alloca(sizeof(__timer_values[0]) * sizeof(__timer_names)        \
                     / sizeof(__timer_names[1]));                             \
        {                                                                     \
            unsigned i = 0;                                                   \
            for (i = 0; i < sizeof(__timer_names) / sizeof(__timer_names[1]); \
                 i++)                                                         \
                __timer_values[i] = NAN;                                      \
        }                                                                     \
        ABT_timer __timer;                                                    \
        do {                                                                  \
            ABT_timer_create(&__timer);                                       \
            ABT_timer_start(__timer);                                         \
        } while (0)

    #define TIMERS_FINALIZE()                                          \
        ABT_timer_stop(__timer);                                       \
        ABT_timer_free(&__timer);                                      \
        printf("TIMER %s: ", __FUNCTION__);                            \
        do {                                                           \
            unsigned i = 0;                                            \
            while (__timer_names[i] != NULL) {                         \
                printf("%s=%f ", __timer_names[i], __timer_values[i]); \
                i += 1;                                                \
            }                                                          \
            printf("\n");                                              \
            fflush(stdout);                                            \
        } while (0)

    #define TIMERS_END_STEP(num)                                    \
        do {                                                        \
            ABT_timer_stop_and_read(__timer, &__timer_values[num]); \
            ABT_timer_start(__timer);                               \
        } while (0);

#else

    #define TIMERS_INITIALIZE(...)
    #define TIMERS_FINALIZE()
    #define TIMERS_END_STEP(num)

#endif

#endif

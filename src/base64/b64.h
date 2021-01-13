/**
 * `b64.h' - b64
 *
 * copyright (c) 2014 joseph werle
 */

#ifndef B64_H
#define B64_H

char* bake_b64_encode(const unsigned char* data, size_t data_size);

unsigned char* bake_b64_decode(const char* str, size_t);

#endif

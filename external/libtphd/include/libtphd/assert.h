#pragma once

#include <stddef.h>

#define TPHD_ASSERT_OFFSET(type, field, offset)                                \
    _Pragma("GCC diagnostic push")                                             \
    _Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")                   \
    static_assert(offsetof(type, field) == (offset),                           \
                  #type "::" #field " is not at " #offset);                    \
    _Pragma("GCC diagnostic pop")

#define TPHD_ASSERT_SIZE(type, size)                                           \
    static_assert(sizeof(type) == (size), "sizeof(" #type ") is not " #size)

#pragma once

#include <stdint.h>

typedef struct
{
    unsigned bit_offset;
    unsigned bit_width;
    uint64_t offset;
} struct_bitfield_data_t;

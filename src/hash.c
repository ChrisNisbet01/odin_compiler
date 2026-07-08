#include "hash.h"

#include <string.h>

#define SIP_ROTL(x, b) (((x) << (b)) | ((x) >> (64 - (b))))

#define SIP_SIPROUND(v0, v1, v2, v3)        \
    do                                      \
    {                                       \
        v0 += v1;                            \
        v1 = SIP_ROTL(v1, 13);              \
        v1 ^= v0;                            \
        v0 = SIP_ROTL(v0, 32);              \
        v2 += v3;                            \
        v3 = SIP_ROTL(v3, 16);              \
        v3 ^= v2;                            \
        v0 += v3;                            \
        v3 = SIP_ROTL(v3, 21);              \
        v3 ^= v0;                            \
        v2 += v1;                            \
        v1 = SIP_ROTL(v1, 17);              \
        v1 ^= v2;                            \
        v2 = SIP_ROTL(v2, 32);              \
    } while (0)

static uint64_t
read_le64(unsigned char const * p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24)
        | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

uint64_t
hash_string(char const * data, size_t len)
{
    unsigned char const * bytes = (unsigned char const *)data;
    unsigned char const * end = bytes + len;

    uint64_t k0 = 0xa6592ea25e04ac3cULL;
    uint64_t k1 = 0xba3cba04ed28a9aeULL;

    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;

    // Process full 8-byte blocks
    unsigned char const * p = bytes;
    while (len >= 8)
    {
        uint64_t m = read_le64(p);
        v3 ^= m;
        SIP_SIPROUND(v0, v1, v2, v3);
        SIP_SIPROUND(v0, v1, v2, v3);
        v0 ^= m;
        p += 8;
        len -= 8;
    }

    // Last block: pad with 0x00, high byte = len << 56
    uint64_t last = (uint64_t)(end - bytes) << 56;
    switch (len)
    {
        case 7: last |= (uint64_t)p[6] << 48;
        case 6: last |= (uint64_t)p[5] << 40;
        case 5: last |= (uint64_t)p[4] << 32;
        case 4: last |= (uint64_t)p[3] << 24;
        case 3: last |= (uint64_t)p[2] << 16;
        case 2: last |= (uint64_t)p[1] << 8;
        case 1: last |= (uint64_t)p[0];
        case 0: break;
    }

    v3 ^= last;
    SIP_SIPROUND(v0, v1, v2, v3);
    SIP_SIPROUND(v0, v1, v2, v3);
    v0 ^= last;

    v2 ^= 0xff;
    SIP_SIPROUND(v0, v1, v2, v3);
    SIP_SIPROUND(v0, v1, v2, v3);
    SIP_SIPROUND(v0, v1, v2, v3);
    SIP_SIPROUND(v0, v1, v2, v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

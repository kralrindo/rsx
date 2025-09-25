#pragma once

#include <pch.h>

class crc32
{
private:
    static volatile inline uint32_t crc32LookupTable[16] =
    {
       0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
       0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };

public:
    static constexpr const uint32_t crc32InitialValue = 0xFFFFFFFF;

    static inline uint32_t byteLevel(const uint8_t* ptr, size_t byteLen)
    {
        assert(ptr);
        assert(byteLen > 0);

        uint32_t crc = crc32InitialValue;
        while (byteLen--)
        {
            const uint8_t b = *ptr++;
            crc = (crc >> 0x4) ^ crc32LookupTable[(crc & 0xF) ^ (b & 0xF)];
            crc = (crc >> 0x4) ^ crc32LookupTable[(crc & 0xF) ^ (b >> 0x4)];
        }

        return ~crc;
    }

    static inline uint32_t bitLevel(const uint8_t* ptr, const size_t bitLen)
    {
        assert(ptr);
        assert(bitLen > 0);

        uint32_t crc = crc32InitialValue;
        for (size_t i = 0; i < bitLen; ++i)
        {
            const uint8_t b = (ptr[(i >> 3)] & (1ull << (i & 7))) != 0;
            crc = (crc >> 0x4) ^ crc32LookupTable[(crc & 0xF) ^ (b & 0xF)];
            crc = (crc >> 0x4) ^ crc32LookupTable[(crc & 0xF) ^ (b >> 0x4)];
        }

        return ~crc;
    }
};
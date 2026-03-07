// This file is made available as part of the reSource Xtractor (RSX) asset extractor
// Licensed under AGPLv3. Details available at https://github.com/r-ex/rsx/blob/main/LICENSE

#include <pch.h>
#include <array>
#include <filesystem>
#include <string>

#include <game/rtech/utils/zstd_loader.h>

// zstd.h is included via vcpkg; static linking
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace RTechZstd
{
    bool Decompress(const void* src, size_t srcSize, void* dst, size_t dstCapacity, size_t& decodedBytes)
    {
        decodedBytes = 0;

        if (!src || !dst || srcSize == 0 || dstCapacity == 0)
            return false;

        // Create decompression context
        ZSTD_DCtx* ctx = ZSTD_createDCtx();
        if (!ctx)
            return false;

        // Decompress the data
        const size_t result = ZSTD_decompressDCtx(ctx, dst, dstCapacity, src, srcSize);

        // Free the context
        ZSTD_freeDCtx(ctx);

        // Check for errors
        if (ZSTD_isError(result))
            return false;

        decodedBytes = result;
        return true;
    }

    size_t FindFrameCompressedSize(const void* src, size_t srcSize)
    {
        if (!src || srcSize == 0)
            return 0;
        return ZSTD_findFrameCompressedSize(src, srcSize);
    }
}

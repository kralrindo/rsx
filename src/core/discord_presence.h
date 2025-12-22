#pragma once

#include <string>

// Define DISCORD_SDK_AVAILABLE when you have the Discord Game SDK headers/libs
// and want the real implementation. Otherwise this provides a no-op stub so
// the project builds without the SDK present.

class DiscordGamePresence
{
public:
    // Initialize the Discord Game SDK (no-op if SDK not available).
    static bool Initialize();

    // Shutdown and cleanup.
    static void Shutdown();

    // Update the presence text (details/state are arbitrary short strings).
    static void UpdatePresence(const std::string& details, const std::string& state);
    // Pump SDK callbacks; call regularly from the main loop.
    static void RunCallbacks();

    // Helper: truncate a string for Discord presence display.
    static std::string TruncateForPresence(const std::string& s, size_t maxLen = 64);

    // Convenience: update presence for an asset (shows asset name + asset type)
    static void UpdatePresenceForAsset(const std::string& assetName, const std::string& assetType);
};

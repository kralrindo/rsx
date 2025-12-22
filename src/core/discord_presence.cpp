#include <pch.h>
#include "discord_presence.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <deque>
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>

#if defined(DISCORD_SDK_AVAILABLE)
// If you enable DISCORD_SDK_AVAILABLE, provide the path to the Game SDK headers
// and link against discord_game_sdk.lib. The runtime requires discord_game_sdk.dll
#include <cpp/discord.h>
static std::atomic<bool> s_initialized{false};
static discord::Core* s_core = nullptr;

// Queue for pending presence updates submitted from any thread.
static std::mutex s_presenceQueueMutex;
static std::deque<std::pair<std::string, std::string>> s_presenceQueue;
// Retry init timing
static std::chrono::steady_clock::time_point s_lastInitAttempt = std::chrono::steady_clock::time_point{};
static constexpr std::chrono::seconds s_initRetryInterval = std::chrono::seconds(10);

bool DiscordGamePresence::Initialize()
{
    if (s_initialized.load())
        return true;

    // Application ID for your Discord application. Prefer setting via the
    // DISCORD_APP_ID preprocessor define (project settings) or via the
    // environment variable `DISCORD_APP_ID` to avoid hardcoding here.
    int64_t appId = 0;
#ifdef DISCORD_APP_ID
    appId = (int64_t)DISCORD_APP_ID;
#else
    const char* env = std::getenv("DISCORD_APP_ID");
    if (env)
    {
        try { appId = std::stoll(env); } catch (...) { appId = 0; }
    }
#endif

    // Don't attempt to create the Core if Discord is not running; creating it
    // can cause the client to be launched which we want to avoid.
    auto IsDiscordRunning = []() -> bool
    {
        const wchar_t* candidates[] = { L"discord.exe", L"Discord.exe", L"DiscordCanary.exe", L"DiscordPTB.exe" };
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (!Process32FirstW(snap, &pe))
        {
            CloseHandle(snap);
            return false;
        }

        bool found = false;
        do
        {
            for (auto name : candidates)
            {
                if (_wcsicmp(pe.szExeFile, name) == 0)
                {
                    found = true;
                    break;
                }
            }

            if (found)
                break;

        } while (Process32NextW(snap, &pe));

        CloseHandle(snap);
        return found;
    };

    if (!IsDiscordRunning())
    {
        fprintf(stderr, "Discord process not found; skipping Core::Create\n");
        return false;
    }

    auto result = discord::Core::Create(appId, DiscordCreateFlags_Default, &s_core);
    if (result != discord::Result::Ok || !s_core)
    {
        fprintf(stderr, "Discord Core create failed: %d\n", static_cast<int>(result));
        return false;
    }

    s_initialized.store(true);
    return true;
}

void DiscordGamePresence::Shutdown()
{
    if (!s_initialized.load())
        return;

    delete s_core;
    s_core = nullptr;
    s_initialized.store(false);
}

void DiscordGamePresence::UpdatePresence(const std::string& details, const std::string& state)
{
    // If SDK isn't initialized, don't attempt to initialize here — initialization
    // can cause Discord to be launched. Simply skip updates until initialized
    // (initialization is attempted at startup and can be retried manually).
    if (!s_initialized.load() || !s_core)
    {
        fprintf(stderr, "Discord SDK not initialized; skipping presence update\n");
        return;
    }

    // Enqueue the update so it is processed on the main thread where the Discord Core
    // object was created. The Game SDK is not guaranteed to be called from arbitrary
    // threads safely, so we queue updates and apply them in RunCallbacks().
    std::scoped_lock lk(s_presenceQueueMutex);
    s_presenceQueue.emplace_back(details, state);
}

void DiscordGamePresence::RunCallbacks()
{
    // Periodically attempt to initialize if not already initialized. This allows
    // the app to automatically connect when Discord is started later, while
    // avoiding frequent retries.
    if (!s_initialized.load() || !s_core)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - s_lastInitAttempt >= s_initRetryInterval)
        {
            s_lastInitAttempt = now;
            try
            {
                Initialize(); // Initialize() is already defensive and checks for Discord process
            }
            catch (...) { /* ignore */ }
        }
        // If still not initialized, we continue but skip running callbacks until ready.
        if (!s_initialized.load() || !s_core)
            return;
    }

    // Process any queued presence updates on the main thread.
    while (true)
    {
        std::pair<std::string, std::string> item;
        {
            std::scoped_lock lk(s_presenceQueueMutex);
            if (s_presenceQueue.empty())
                break;
            item = std::move(s_presenceQueue.front());
            s_presenceQueue.pop_front();
        }

        try
        {
            discord::Activity activity{};
            // Truncate for safety/display
            const std::string details = TruncateForPresence(item.first, 64);
            const std::string state = TruncateForPresence(item.second, 32);
            activity.SetDetails(details.c_str());
            activity.SetState(state.c_str());

            s_core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {
                if (result != discord::Result::Ok)
                {
                    fprintf(stderr, "Discord UpdateActivity failed: %d\n", static_cast<int>(result));
                }
            });
        }
        catch (...) // guard against SDK throwing or unexpected errors
        {
            fprintf(stderr, "Exception while updating Discord activity; clearing pending updates\n");
            std::scoped_lock lk(s_presenceQueueMutex);
            s_presenceQueue.clear();
            break;
        }
    }

    try
    {
        s_core->RunCallbacks();
    }
    catch (...) // guard against SDK throwing
    {
        fprintf(stderr, "Exception in Discord RunCallbacks(); ignoring\n");
    }
}

// Helper: truncate a string for Discord presence display.
std::string DiscordGamePresence::TruncateForPresence(const std::string& s, size_t maxLen)
{
    if (s.size() <= maxLen)
        return s;

    if (maxLen <= 3)
        return s.substr(0, maxLen);

    return s.substr(0, maxLen - 3) + "...";
}

// Convenience wrapper: show asset name (truncated) and its type as state.
void DiscordGamePresence::UpdatePresenceForAsset(const std::string& assetName, const std::string& assetType)
{
    const std::string details = TruncateForPresence(assetName, 64);
    const std::string state = TruncateForPresence(assetType, 32);
    UpdatePresence(details, state);
}

#else

// Stub/no-op implementation when SDK isn't available.
static std::atomic<bool> s_initialized{false};

bool DiscordGamePresence::Initialize()
{
    s_initialized.store(true);
    return true;
}

void DiscordGamePresence::Shutdown()
{
    s_initialized.store(false);
}

void DiscordGamePresence::UpdatePresence(const std::string& /*details*/, const std::string& /*state*/)
{
    // no-op
}

#endif

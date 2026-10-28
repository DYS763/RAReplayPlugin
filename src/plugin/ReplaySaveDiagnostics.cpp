#include "ReplaySaveDiagnostics.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objidl.h>

// 此版 YRpp 的部分头文件仍使用 Win32 的小写 byte 基类型。
using byte = unsigned char;

#include <ScenarioClass.h>
#include <SessionClass.h>
#include <Unsorted.h>

#include "Diagnostics.h"
#include "../target/TargetImage.h"
#include "../../third_party/minhook/include/MinHook.h"

#include <atomic>
#include <cstdio>
#include <string>

namespace rareplay::plugin
{
namespace
{
using SaveGameFunction = bool(__fastcall*)(const char*, const wchar_t*, bool);
using StgCreateDocfileFunction = HRESULT(WINAPI*)(const WCHAR*, DWORD, DWORD, IStorage**);
using CreateStreamFunction = HRESULT(STDMETHODCALLTYPE*)(IStorage*, const WCHAR*, DWORD,
    DWORD, DWORD, IStream**);
using CommitFunction = HRESULT(STDMETHODCALLTYPE*)(IStorage*, DWORD);

SaveGameFunction g_SaveGameOriginal = nullptr;
StgCreateDocfileFunction g_StgCreateDocfileOriginal = nullptr;
CreateStreamFunction g_CreateStreamOriginal = nullptr;
CommitFunction g_CommitOriginal = nullptr;
void* g_StgCreateDocfileTarget = nullptr;
std::atomic<bool> g_Installed{false};
std::atomic<bool> g_StorageHooksInstalled{false};
thread_local bool g_InSaveGameHook = false;

std::string WideToUtf8(const wchar_t* value)
{
    if (!value || !*value)
        return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(),
            required, nullptr, nullptr) <= 0)
        return {};
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

void LogStorageResult(const char* operation, HRESULT result, const char* detail) noexcept
{
    char message[768]{};
    std::snprintf(message, sizeof(message),
        "Replay save diagnostic: %s hr=0x%08lX succeeded=%d %s",
        operation ? operation : "unknown", static_cast<unsigned long>(result),
        SUCCEEDED(result) ? 1 : 0, detail ? detail : "");
    Log(message);
}

HRESULT STDMETHODCALLTYPE CreateStreamHook(IStorage* storage, const WCHAR* name, DWORD mode,
    DWORD reserved1, DWORD reserved2, IStream** stream);
HRESULT STDMETHODCALLTYPE CommitHook(IStorage* storage, DWORD flags);

bool InstallStorageMethodHook(void* target, void* detour, void** original,
    const char* name) noexcept
{
    if (!target || !detour || !original)
        return false;
    const MH_STATUS created = MH_CreateHook(target, detour, original);
    if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED)
    {
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "Replay save diagnostic: unable to create %s hook status=%d",
            name ? name : "storage method", static_cast<int>(created));
        Log(message);
        return false;
    }
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
    {
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "Replay save diagnostic: unable to enable %s hook status=%d",
            name ? name : "storage method", static_cast<int>(enabled));
        Log(message);
        return false;
    }
    return true;
}

void InstallStorageMethodHooks(IStorage* storage) noexcept
{
    if (!storage || g_StorageHooksInstalled.load())
        return;
    void** vtable = *reinterpret_cast<void***>(storage);
    if (!vtable)
        return;
    const bool createStream = InstallStorageMethodHook(vtable[3],
        reinterpret_cast<void*>(&CreateStreamHook), reinterpret_cast<void**>(&g_CreateStreamOriginal),
        "IStorage::CreateStream");
    const bool commit = InstallStorageMethodHook(vtable[9],
        reinterpret_cast<void*>(&CommitHook), reinterpret_cast<void**>(&g_CommitOriginal),
        "IStorage::Commit");
    if (createStream && commit)
        g_StorageHooksInstalled.store(true);
}

HRESULT STDMETHODCALLTYPE CreateStreamHook(IStorage* storage, const WCHAR* name, DWORD mode,
    DWORD reserved1, DWORD reserved2, IStream** stream)
{
    const HRESULT result = g_CreateStreamOriginal
        ? g_CreateStreamOriginal(storage, name, mode, reserved1, reserved2, stream)
        : E_POINTER;
    const std::string streamName = WideToUtf8(name);
    char detail[384]{};
    std::snprintf(detail, sizeof(detail), "name=%s mode=0x%08lX stream=%p",
        streamName.empty() ? "<empty>" : streamName.c_str(),
        static_cast<unsigned long>(mode), stream ? static_cast<void*>(*stream) : nullptr);
    LogStorageResult("IStorage::CreateStream", result, detail);
    return result;
}

HRESULT STDMETHODCALLTYPE CommitHook(IStorage* storage, DWORD flags)
{
    const HRESULT result = g_CommitOriginal ? g_CommitOriginal(storage, flags) : E_POINTER;
    char detail[128]{};
    std::snprintf(detail, sizeof(detail), "flags=0x%08lX", static_cast<unsigned long>(flags));
    LogStorageResult("IStorage::Commit", result, detail);
    return result;
}

HRESULT WINAPI StgCreateDocfileHook(const WCHAR* path, DWORD mode, DWORD reserved,
    IStorage** storage)
{
    const HRESULT result = g_StgCreateDocfileOriginal
        ? g_StgCreateDocfileOriginal(path, mode, reserved, storage)
        : E_POINTER;
    const std::string utf8Path = WideToUtf8(path);
    char detail[448]{};
    std::snprintf(detail, sizeof(detail), "path=%s mode=0x%08lX storage=%p",
        utf8Path.empty() ? "<empty>" : utf8Path.c_str(), static_cast<unsigned long>(mode),
        storage ? static_cast<void*>(*storage) : nullptr);
    LogStorageResult("StgCreateDocfile", result, detail);
    if (SUCCEEDED(result) && storage && *storage)
        InstallStorageMethodHooks(*storage);
    return result;
}

bool __fastcall SaveGameHook(const char* fileName, const wchar_t* description, bool barGraph)
{
    if (g_InSaveGameHook || !g_SaveGameOriginal)
        return g_SaveGameOriginal ? g_SaveGameOriginal(fileName, description, barGraph) : false;
    g_InSaveGameHook = true;

    char currentDirectory[MAX_PATH]{};
    const DWORD directoryLength = GetCurrentDirectoryA(static_cast<DWORD>(sizeof(currentDirectory)),
        currentDirectory);
    const auto* scenario = ScenarioClass::Instance;
    const auto& session = SessionClass::Instance;
    const std::string descriptionText = WideToUtf8(description);
    char entered[1024]{};
    std::snprintf(entered, sizeof(entered),
        "Replay save diagnostic: SaveGame enter file=%s description=%s cwd=%s frame=%u "
        "mode=%d paused=%d world=%d sessionPlay=%d sessionRecord=%d inGame=%d "
        "sessionLoad=%d sessionSave=%d barGraph=%d",
        fileName ? fileName : "<null>",
        descriptionText.empty() ? "<empty>" : descriptionText.c_str(),
        directoryLength > 0 && directoryLength < sizeof(currentDirectory) ? currentDirectory : "<unknown>",
        static_cast<unsigned>(Unsorted::CurrentFrame),
        static_cast<int>(session.GameMode), scenario ? (scenario->IsGamePaused ? 1 : 0) : -1,
        Game::IsActive ? 1 : 0, session.Play ? 1 : 0, session.Record ? 1 : 0,
        session.CurrentlyInGame ? 1 : 0, session.LoadGame, session.SaveGame, barGraph ? 1 : 0);
    Log(entered);

    const bool result = g_SaveGameOriginal(fileName, description, barGraph);
    const DWORD lastError = GetLastError();
    char returned[384]{};
    std::snprintf(returned, sizeof(returned),
        "Replay save diagnostic: SaveGame return=%d lastError=%lu file=%s",
        result ? 1 : 0, static_cast<unsigned long>(lastError), fileName ? fileName : "<null>");
    Log(returned);
    g_InSaveGameHook = false;
    return result;
}

bool InstallHook(void* target, void* detour, void** original, const char* name) noexcept
{
    if (!target)
    {
        Log("Replay save diagnostic: hook target is null");
        return false;
    }
    const MH_STATUS created = MH_CreateHook(target, detour, original);
    if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED)
    {
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "Replay save diagnostic: unable to create %s hook status=%d",
            name ? name : "function", static_cast<int>(created));
        Log(message);
        return false;
    }
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK && enabled != MH_ERROR_ENABLED)
    {
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "Replay save diagnostic: unable to enable %s hook status=%d",
            name ? name : "function", static_cast<int>(enabled));
        Log(message);
        return false;
    }
    return true;
}
}

bool InstallReplaySaveDiagnostics() noexcept
{
    if (g_Installed.load())
        return true;
    try
    {
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
        {
            Log("Replay save diagnostic: MinHook initialization failed");
            return false;
        }
        const auto saveGame = reinterpret_cast<void*>(target::TARGET_IMAGE_BASE + 0x67CEF0u);
        const bool saveHook = InstallHook(saveGame, reinterpret_cast<void*>(&SaveGameHook),
            reinterpret_cast<void**>(&g_SaveGameOriginal), "ScenarioClass::SaveGame");
        const bool stgCreated = MH_CreateHookApiEx(L"ole32.dll", "StgCreateDocfile",
            reinterpret_cast<void*>(&StgCreateDocfileHook),
            reinterpret_cast<void**>(&g_StgCreateDocfileOriginal), &g_StgCreateDocfileTarget) == MH_OK;
        const bool stgHook = stgCreated && MH_EnableHook(g_StgCreateDocfileTarget) == MH_OK;
        if (!stgHook)
            Log("Replay save diagnostic: unable to install StgCreateDocfile hook");
        g_Installed.store(saveHook && stgHook);
        Log(g_Installed.load() ? "Replay save diagnostics installed"
            : "Replay save diagnostics incomplete");
        return g_Installed.load();
    }
    catch (...)
    {
        Log("Replay save diagnostic: installation threw an exception");
        return false;
    }
}
}

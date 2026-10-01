#include "NativeGame.h"

#include "../../target/TargetAbi.h"

using byte = unsigned char;

#include <objidl.h>
#include <Unsorted.h>

#include <cstdint>

namespace rareplay::game_api::native
{
namespace
{
using HandleAnyFunction = int(__fastcall*)(int, int, void*, int*, int*);
using PostFrameEventsFunction = void(__cdecl*)();
using FrameParserFunction = void(__cdecl*)();
using PreworldWaitFunction = bool(__cdecl*)();
}

bool IsWorldReady()
{
    return *reinterpret_cast<const std::uint8_t*>(target::WORLD_READY) == 1;
}

bool ExecuteDueEvents()
{
    // ProcessNetworkFrame 在目标版本使用 Session+0x310 作 ECX、EDX=0。
    const int context = *reinterpret_cast<const int*>(target::EXECUTOR_CONTEXT);
    return reinterpret_cast<HandleAnyFunction>(target::EXECUTE_EVENTS)(
        context, 0, nullptr, nullptr, nullptr) != 0;
}

void ProcessPostFrameEvents()
{
    reinterpret_cast<PostFrameEventsFunction>(target::POST_FRAME_EVENTS)();
}

void InvokeFrameParser()
{
    reinterpret_cast<FrameParserFunction>(target::NATIVE_FRAME_PARSER)();
}

bool WaitForPeers()
{
    return reinterpret_cast<PreworldWaitFunction>(target::NATIVE_PREWORLD_WAIT)();
}

void RequestReplayExit()
{
    *reinterpret_cast<volatile std::uint8_t*>(target::EXIT_REQUEST) = 1;
    Game::IsActive = false;
}

void RequestViewerOptionsMenu()
{
    *reinterpret_cast<volatile int*>(target::OPTIONS_REQUEST) = 1;
}
}

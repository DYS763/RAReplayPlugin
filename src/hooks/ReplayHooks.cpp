#include "../game_api/GameApi.h"
#include "../plugin/ReplayRuntime.h"
#include "../target/TargetAbi.h"
#include "ViewerInputScope.h"

#include <Syringe.h>

// Syringe 从 .syhks00 读取声明；CALL rel32 站点由回调显式调用原目标并跳到续点。
DEFINE_HOOK(0x52D9A0, ReplayStartupHook, 6)
{
    rareplay::plugin::OnStartup();
    return 0;
}

// 两个分支均已完成选种；在原指令读取种子并初始化 RNG 前恢复录像值。
DEFINE_HOOK(0x52FDF9, ReplayRandomSeedHook, 6)
{
    std::uint32_t seed = 0;
    if (rareplay::plugin::TryGetPlaybackSeed(seed))
        rareplay::game_api::GameApi::RestoreRandomSeedForInitialization(seed);
    return 0;
}

DEFINE_HOOK_AGAIN(0x52FE5C, ReplayRandomSeedHook, 5)

DEFINE_HOOK(0x55D360, ReplayFrameBeginHook, 5)
{
    rareplay::plugin::OnFrameBegin();
    return 0;
}

// Native Input 之后、Logic.Update 之前重演本帧输入效果。
DEFINE_HOOK(0x55DC99, ReplayBeforeSimulationHook, 5)
{
    rareplay::plugin::OnBeforeSimulation();
    return 0;
}

// 已安装 Syringe 至少改写 5 字节；覆盖 SUB ESP,40h + PUSH EBX + 完整 MOV EBX,[A80238]。
DEFINE_HOOK(0x64C380, ReplayBeforeEventsHook, 10)
{
    rareplay::plugin::OnBeforeNativeEvents();
    return 0;
}

DEFINE_HOOK(0x55DE40, ReplayParserHook, 5)
{
    if (rareplay::plugin::OnPlaybackParser())
        return static_cast<DWORD>(rareplay::target::FRAME_PARSER_CONTINUE);
    rareplay::game_api::GameApi::InvokeNativeFrameParser();
    return static_cast<DWORD>(rareplay::target::FRAME_PARSER_CONTINUE);
}

DEFINE_HOOK(0x55DE7E, ReplayFrameCompleteHook, 9)
{
    rareplay::plugin::OnFrameComplete();
    return 0;
}

DEFINE_HOOK(0x684B3E, ReplayPreworldWaitHook, 5)
{
    const bool ready = rareplay::plugin::ShouldBypassPeerProgressWait() ||
        rareplay::game_api::GameApi::InvokeNativePreworldWait();
    R->EAX(static_cast<DWORD>(ready));
    return static_cast<DWORD>(rareplay::target::PREWORLD_WAIT_CONTINUE);
}

DEFINE_HOOK(0x48CEAF, ReplaySessionEndHook, 5)
{
    rareplay::hooks::GetViewerInputScope().Reset();
    rareplay::plugin::OnSessionEnd();
    return 0;
}

#include "../plugin/ReplayRuntime.h"
#include "../target/TargetAbi.h"

#include <Syringe.h>

// 菜单显示读取观众覆盖值；未设置时保留模拟速度的原生读取。
DEFINE_HOOK(0x4E209E, ReplayViewerSpeedGetterHook, 6)
{
    int setting = 0;
    if (!rareplay::plugin::TryGetPlaybackViewingSpeed(setting))
        return 0;
    R->EDX(static_cast<DWORD>(setting));
    return static_cast<DWORD>(rareplay::target::VIEWER_SPEED_GETTER_CONTINUE);
}

// 本地滑杆只改观众节奏；GameApi 会把网络录像覆盖写入本地 RequestedFPS，仍跳过网络事件入队。
DEFINE_HOOK(0x4E1E1B, ReplayViewerSpeedSetterHook, 5)
{
    if (!rareplay::plugin::ShouldHandlePlaybackViewingSpeed())
        return 0;
    const int requested = R->ECX<int>();
    rareplay::plugin::TrySetPlaybackViewingSpeed(requested);
    return static_cast<DWORD>(rareplay::target::VIEWER_SPEED_SETTER_CONTINUE);
}

// 单人 viewer 只替换 pacing 寄存器；网络录像 pacing 使用 GameApi 的本地 RequestedFPS 覆盖。
DEFINE_HOOK(0x55D79E, ReplayViewerFramePacingHook, 6)
{
    int setting = 0;
    if (!rareplay::plugin::TryGetPlaybackViewingSpeed(setting))
        return 0;
    R->ESI(static_cast<DWORD>(setting));
    return static_cast<DWORD>(rareplay::target::VIEWER_FRAME_PACING_CONTINUE);
}

// 前进跳转时跳过完整的 pacing 等待调用；回放帧仍由原生主循环逐帧执行。
DEFINE_HOOK(0x55D854, ReplayViewerFramePacingCallHook, 5)
{
    if (rareplay::plugin::IsReplayFastForwarding())
        return static_cast<DWORD>(rareplay::target::VIEWER_FRAME_PACING_CALL_CONTINUE);
    return 0;
}

// LAN/Internet 录像走另一条主循环路径；该调用才是多人录像本地回放的实际等待点。
DEFINE_HOOK(0x55DE9A, ReplayNetworkViewerFramePacingCallHook, 5)
{
    if (rareplay::plugin::IsReplayFastForwarding())
        return static_cast<DWORD>(rareplay::target::VIEWER_NETWORK_FRAME_PACING_CALL_CONTINUE);
    return 0;
}

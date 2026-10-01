#include "../plugin/ReplayRuntime.h"
#include "../target/TargetAbi.h"

#include <Syringe.h>

// 直接跳到独立 RETN 0Ch；原函数栈帧未建立，不能经过它的清理尾声。
DEFINE_HOOK(0x6AAD00, ReplaySidebarCommandHook, 6)
{
    if (!rareplay::plugin::ShouldBlockPlayerCommands())
        return 0;
    R->EAX(1);
    return static_cast<DWORD>(rareplay::target::SIDEBAR_COMMAND_BLOCK_RETURN);
}

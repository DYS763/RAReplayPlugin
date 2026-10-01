#pragma once

#include <cstdint>

namespace rareplay::target
{
/** 仅匹配目标版本中用于玩家控制和动作光标的返回地址。 */
bool IsViewerControlCaller(std::uintptr_t caller) noexcept;
}

#pragma once

#include "OverlayInputQueue.h"

#include <Windows.h>

namespace rareplay::render
{
/** 当前线程须持有正确的 ImGui context；只消费队列，不修改 Win32 窗口捕获。 */
void ProcessOverlayInput(OverlayInputQueue& inputQueue, HWND window,
                         bool playbackActive, bool& visible) noexcept;
}

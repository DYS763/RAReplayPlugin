#pragma once

#include <Windows.h>

namespace rareplay::render { class OverlayInputQueue; }

namespace rareplay::hooks
{
/** 只替换平台输入接收者，不改变游戏窗口或渲染配置。队列须存活到进程结束。 */
bool AttachOverlayWindow(HWND window, render::OverlayInputQueue& input) noexcept;
void ClearOverlayWindowInput() noexcept;
}

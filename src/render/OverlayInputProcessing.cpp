#include "OverlayInputProcessing.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <windowsx.h>

#include <cstdint>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                               WPARAM wParam, LPARAM lParam);

namespace rareplay::render
{
namespace
{
int GetMouseButton(UINT message, WPARAM wParam) noexcept
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP: return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP: return 1;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP: return 2;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP: return HIWORD(wParam) == XBUTTON1 ? 3 : 4;
    default: return -1;
    }
}

bool IsButtonDown(UINT message) noexcept
{
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
}
}

void ProcessOverlayInput(OverlayInputQueue& inputQueue, HWND window,
                         bool playbackActive, bool& visible) noexcept
{
    const std::vector<MSG> messages = inputQueue.Drain();
    ImGuiIO& io = ImGui::GetIO();
    if (inputQueue.ConsumeFocusReset())
    {
        io.AddFocusEvent(false);
        for (int button = 0; button < 5; ++button)
        {
            io.AddMouseButtonEvent(button, false);
        }
    }

    for (const MSG& message : messages)
    {
        if (playbackActive &&
            (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
            message.wParam == VK_F10)
        {
            if ((static_cast<std::uint32_t>(message.lParam) & (1u << 30)) == 0)
            {
                visible = !visible;
                if (!visible)
                {
                    io.AddFocusEvent(false);
                    inputQueue.RequestFocusReset();
                }
                else
                {
                    const HWND foreground = GetForegroundWindow();
                    io.AddFocusEvent(foreground == window ||
                                     (foreground != nullptr && IsChild(window, foreground)) ||
                                     (foreground != nullptr && GetAncestor(window, GA_ROOT) == foreground));
                }
            }
            continue;
        }
        if (!visible || !playbackActive)
        {
            continue;
        }

        const int button = GetMouseButton(message.message, message.wParam);
        if (button >= 0)
        {
            // Win32 后端的按钮分支会 SetCapture；窗口捕获仅由 WndProc 队列线程维护。
            io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(message.lParam)),
                                static_cast<float>(GET_Y_LPARAM(message.lParam)));
            io.AddMouseButtonEvent(button, IsButtonDown(message.message));
            continue;
        }
        ImGui_ImplWin32_WndProcHandler(message.hwnd, message.message,
                                       message.wParam, message.lParam);
    }
}
}

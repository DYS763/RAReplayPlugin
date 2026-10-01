#include "OverlayWindowHook.h"

#include "../../render/OverlayInputQueue.h"
#include "../../viewer/ReplayViewExchange.h"

#include <atomic>

namespace rareplay::hooks
{
namespace
{
std::atomic<HWND> g_Window{nullptr};
std::atomic<WNDPROC> g_Original{nullptr};
std::atomic<render::OverlayInputQueue*> g_Input{nullptr};
std::atomic<render::OverlayInputQueue*> g_RetiredInput{nullptr};

LRESULT CALLBACK OverlayWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    const auto original = g_Original.load(std::memory_order_acquire);
    try
    {
        if (auto* retired = g_RetiredInput.exchange(nullptr))
        {
            retired->Clear();
            retired->RouteMessage(window, message, wParam, lParam);
        }
        if (auto* input = g_Input.load(std::memory_order_acquire))
        {
            const auto snapshot = viewer::GetReplayViewExchange().Read();
            if (message == WM_NCDESTROY || !snapshot || !snapshot->mActive)
                input->Clear();
            if (input->RouteMessage(window, message, wParam, lParam))
                return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK ? TRUE : 0;
        }
        if (message == WM_NCDESTROY && g_Window.load() == window)
        {
            if (original && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == OverlayWndProc)
                SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
            g_Window.store(nullptr);
            g_Input.store(nullptr);
        }
    }
    catch (...) { ClearOverlayWindowInput(); }
    return original ? CallWindowProcW(original, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}
}

bool AttachOverlayWindow(HWND window, render::OverlayInputQueue& input) noexcept
{
    const HWND existing = g_Window.load(std::memory_order_acquire);
    if (!window || (existing && existing != window))
        return false;
    if (!existing)
    {
        const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (!original || original == OverlayWndProc)
            return false;
        g_Original.store(original, std::memory_order_release);
        g_Window.store(window, std::memory_order_release);
        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverlayWndProc));
        if (!previous && GetLastError())
        {
            g_Window.store(nullptr);
            g_Original.store(nullptr);
            return false;
        }
        g_Original.store(reinterpret_cast<WNDPROC>(previous), std::memory_order_release);
    }
    auto* previousInput = g_Input.exchange(&input);
    if (previousInput && previousInput != &input)
    {
        previousInput->Clear();
        g_RetiredInput.store(previousInput);
    }
    return true;
}

void ClearOverlayWindowInput() noexcept
{
    if (auto* input = g_Input.load(std::memory_order_acquire))
        input->Clear();
}
}

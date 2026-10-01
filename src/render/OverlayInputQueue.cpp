#include "OverlayInputQueue.h"

#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace rareplay::render
{
void OverlayInputQueue::PublishPolicy(bool playbackActive, bool overlayVisible, bool wantsKeyboard,
                                     const std::vector<ui::UiRect>& inputRegions) noexcept
{
    try
    {
        auto policy = std::make_shared<CapturePolicy>();
        policy->mPlaybackActive = playbackActive;
        policy->mOverlayVisible = overlayVisible;
        policy->mWantsKeyboard = wantsKeyboard;
        policy->mInputRegions = inputRegions;
        mPolicy.store(std::move(policy), std::memory_order_release);
        if (!playbackActive || !overlayVisible)
        {
            mClearCapturedButtonsRequested.store(true, std::memory_order_release);
            RequestFocusReset();
        }
    }
    catch (...)
    {
        mPolicy.store(nullptr, std::memory_order_release);
        mClearCapturedButtonsRequested.store(true, std::memory_order_release);
        RequestFocusReset();
    }
}

bool OverlayInputQueue::RouteMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    if (mClearCapturedButtonsRequested.exchange(false, std::memory_order_acq_rel))
    {
        mCapturedButtons = 0;
        mGameButtons = 0;
        if (mOwnsWindowCapture)
        {
            if (GetCapture() == window)
            {
                ReleaseCapture();
            }
            mOwnsWindowCapture = false;
        }
    }

    const auto policy = mPolicy.load(std::memory_order_acquire);
    if (!policy || !policy->mPlaybackActive)
    {
        mCapturedButtons = 0;
        mGameButtons = 0;
        return false;
    }

    if (message == WM_KILLFOCUS || message == WM_SETFOCUS)
    {
        Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
        if (message == WM_KILLFOCUS)
        {
            mCapturedButtons = 0;
            mGameButtons = 0;
            if (mOwnsWindowCapture)
            {
                if (GetCapture() == window)
                {
                    ReleaseCapture();
                }
                mOwnsWindowCapture = false;
            }
        }
        return false;
    }

    if (message == WM_CAPTURECHANGED)
    {
        mCapturedButtons = 0;
        mOwnsWindowCapture = false;
        return false;
    }

    if (IsF10Message(message, wParam))
    {
        Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
        return false;
    }

    if (!policy->mOverlayVisible)
    {
        return false;
    }

    POINT point{};
    if (IsMouseMessage(message))
    {
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        {
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
            ScreenToClient(window, &point);
        }
        else
        {
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
        }
    }

    if (IsButtonUp(message))
    {
        const std::uint8_t buttonMask = ButtonMask(message, wParam);
        const bool wasCaptured = buttonMask != 0 && (mCapturedButtons & buttonMask) != 0;
        mCapturedButtons = static_cast<std::uint8_t>(mCapturedButtons & ~buttonMask);
        mGameButtons = static_cast<std::uint8_t>(mGameButtons & ~buttonMask);
        if (wasCaptured)
        {
            Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
            if (mCapturedButtons == 0 && mOwnsWindowCapture)
            {
                if (GetCapture() == window)
                {
                    ReleaseCapture();
                }
                mOwnsWindowCapture = false;
            }
        }
        return wasCaptured;
    }

    if (IsButtonDown(message))
    {
        if (mGameButtons == 0 && IsInsideRegion(*policy, point))
        {
            const std::uint8_t buttonMask = ButtonMask(message, wParam);
            if (mCapturedButtons == 0 && !mOwnsWindowCapture)
            {
                const HWND capturedWindow = GetCapture();
                if (capturedWindow != nullptr && capturedWindow != window)
                {
                    mGameButtons = static_cast<std::uint8_t>(mGameButtons | buttonMask);
                    return false;
                }
                // 游戏可能已经捕获自己的窗口；复用它，但不能在 UI 抬起时释放它。
                if (capturedWindow == nullptr && window != nullptr)
                {
                    SetCapture(window);
                    mOwnsWindowCapture = GetCapture() == window;
                }
            }
            mCapturedButtons = static_cast<std::uint8_t>(mCapturedButtons | buttonMask);
            Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
            return true;
        }
        mGameButtons = static_cast<std::uint8_t>(mGameButtons | ButtonMask(message, wParam));
        return false;
    }

    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
    {
        if (IsInsideRegion(*policy, point))
        {
            Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
            return true;
        }
        return false;
    }

    if (message == WM_MOUSEMOVE)
    {
        if (mGameButtons == 0 && (mCapturedButtons != 0 || IsInsideRegion(*policy, point)))
        {
            Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
        }
        return false;
    }

    const bool isSystemKey = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR;
    if (IsKeyboardMessage(message) && !isSystemKey && policy->mWantsKeyboard)
    {
        const HWND focusedWindow = GetFocus();
        if (window != nullptr &&
            (focusedWindow == window || (focusedWindow != nullptr && IsChild(window, focusedWindow))))
        {
            Enqueue(MSG{window, message, wParam, lParam, static_cast<DWORD>(GetMessageTime()), {0, 0}});
            return true;
        }
    }

    return false;
}

void OverlayInputQueue::RequestFocusReset() noexcept
{
    std::lock_guard lock(mQueueMutex);
    mFocusResetRequested = true;
}

std::vector<MSG> OverlayInputQueue::Drain() noexcept
{
    std::lock_guard lock(mQueueMutex);
    std::vector<MSG> messages;
    try
    {
        messages.reserve(mMessages.size());
        while (!mMessages.empty())
        {
            messages.push_back(mMessages.front());
            mMessages.pop_front();
        }
    }
    catch (...)
    {
        mMessages.clear();
        mFocusResetRequested = true;
    }
    return messages;
}

bool OverlayInputQueue::ConsumeFocusReset() noexcept
{
    std::lock_guard lock(mQueueMutex);
    const bool reset = mFocusResetRequested;
    mFocusResetRequested = false;
    return reset;
}

void OverlayInputQueue::Clear() noexcept
{
    mPolicy.store(nullptr, std::memory_order_release);
    mClearCapturedButtonsRequested.store(true, std::memory_order_release);
    std::lock_guard lock(mQueueMutex);
    mMessages.clear();
    mFocusResetRequested = true;
}

bool OverlayInputQueue::IsMouseMessage(UINT message) noexcept
{
    return message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
           message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MBUTTONDBLCLK ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK;
}

bool OverlayInputQueue::IsKeyboardMessage(UINT message) noexcept
{
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR ||
           message == WM_SYSKEYDOWN || message == WM_SYSKEYUP || message == WM_SYSCHAR;
}

bool OverlayInputQueue::IsF10Message(UINT message, WPARAM wParam) noexcept
{
    return (message == WM_KEYDOWN || message == WM_KEYUP ||
            message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && wParam == VK_F10;
}

bool OverlayInputQueue::IsInsideRegion(const CapturePolicy& policy, const POINT& point) noexcept
{
    for (const ui::UiRect& region : policy.mInputRegions)
    {
        if (point.x >= region.mLeft && point.y >= region.mTop &&
            point.x < region.mRight && point.y < region.mBottom)
        {
            return true;
        }
    }
    return false;
}

bool OverlayInputQueue::IsButtonDown(UINT message) noexcept
{
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
}

bool OverlayInputQueue::IsButtonUp(UINT message) noexcept
{
    return message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
           message == WM_MBUTTONUP || message == WM_XBUTTONUP;
}

std::uint8_t OverlayInputQueue::ButtonMask(UINT message, WPARAM wParam) noexcept
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return 0x01;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return 0x02;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return 0x04;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return HIWORD(wParam) == XBUTTON1 ? 0x08 : 0x10;
    default:
        return 0;
    }
}

void OverlayInputQueue::Enqueue(const MSG& message) noexcept
{
    std::lock_guard lock(mQueueMutex);
    if (mMessages.size() >= CAPACITY)
    {
        const auto mouseMove = std::find_if(mMessages.begin(), mMessages.end(), [](const MSG& queued) {
            return queued.message == WM_MOUSEMOVE;
        });
        if (mouseMove != mMessages.end())
        {
            mMessages.erase(mouseMove);
        }
        else if (message.message == WM_MOUSEMOVE)
        {
            return;
        }
        else
        {
            mMessages.pop_front();
            mFocusResetRequested = true;
        }
    }

    try
    {
        mMessages.push_back(message);
    }
    catch (...)
    {
        mFocusResetRequested = true;
    }
}
}

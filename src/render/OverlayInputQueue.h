#pragma once

#include "../ui/ReplayHud.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace rareplay::render
{
/**
 * WndProc 线程只复制 Win32 输入消息；ImGui 后端在 OpenGL 渲染线程消费消息。
 * 输入策略是渲染线程发布的不可变上一帧快照。
 */
class OverlayInputQueue final
{
public:
    static constexpr std::size_t CAPACITY = 256;

    void PublishPolicy(bool playbackActive, bool overlayVisible, bool wantsKeyboard,
                       const std::vector<ui::UiRect>& inputRegions) noexcept;

    /** 入队并根据上一帧 UI 区域决定是否让游戏收到该消息。 */
    bool RouteMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    void RequestFocusReset() noexcept;

    std::vector<MSG> Drain() noexcept;
    bool ConsumeFocusReset() noexcept;
    void Clear() noexcept;

private:
    struct CapturePolicy
    {
        bool mPlaybackActive = false;
        bool mOverlayVisible = false;
        bool mWantsKeyboard = false;
        std::vector<ui::UiRect> mInputRegions;
    };

    static bool IsMouseMessage(UINT message) noexcept;
    static bool IsKeyboardMessage(UINT message) noexcept;
    static bool IsF10Message(UINT message, WPARAM wParam) noexcept;
    static bool IsInsideRegion(const CapturePolicy& policy, const POINT& point) noexcept;
    static bool IsButtonDown(UINT message) noexcept;
    static bool IsButtonUp(UINT message) noexcept;
    static std::uint8_t ButtonMask(UINT message, WPARAM wParam) noexcept;

    void Enqueue(const MSG& message) noexcept;

    std::atomic<std::shared_ptr<const CapturePolicy>> mPolicy;
    std::mutex mQueueMutex;
    std::deque<MSG> mMessages;
    bool mFocusResetRequested = false;
    std::atomic<bool> mClearCapturedButtonsRequested{false};
    std::uint8_t mCapturedButtons = 0;
    std::uint8_t mGameButtons = 0;
    bool mOwnsWindowCapture = false;
};
}

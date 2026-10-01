#pragma once

#include <Windows.h>
#include <gl/GL.h>

namespace rareplay::render
{
/** 在原始交换调用上下文的绘制目标上创建专用共享 WGL 上下文。 */
class WglOverlayContext final
{
public:
    WglOverlayContext() = default;
    WglOverlayContext(const WglOverlayContext&) = delete;
    WglOverlayContext& operator=(const WglOverlayContext&) = delete;
    ~WglOverlayContext();

    bool Ensure(HWND window, HDC drawDc, HGLRC sourceContext) noexcept;
    void Reset() noexcept;

    HGLRC Get() const noexcept { return mContext; }
    HWND GetWindow() const noexcept { return mWindow; }

private:
    HWND mWindow = nullptr;
    HGLRC mSourceContext = nullptr;
    HGLRC mContext = nullptr;
    int mPixelFormat = 0;
};

/** 绘制范围内绑定 HUD 上下文，离开时恢复调用者的 HDC/RC。 */
class ScopedWglContext final
{
public:
    ScopedWglContext(HDC drawDc, HGLRC overlayContext) noexcept;
    ScopedWglContext(const ScopedWglContext&) = delete;
    ScopedWglContext& operator=(const ScopedWglContext&) = delete;
    ~ScopedWglContext();

    bool IsBound() const noexcept { return mBound; }

private:
    HDC mPreviousDc = nullptr;
    HGLRC mPreviousContext = nullptr;
    bool mBound = false;
};
}

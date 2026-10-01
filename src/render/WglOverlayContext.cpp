#include "WglOverlayContext.h"

namespace rareplay::render
{
WglOverlayContext::~WglOverlayContext()
{
    Reset();
}

bool WglOverlayContext::Ensure(HWND window, HDC drawDc, HGLRC sourceContext) noexcept
{
    if (window == nullptr || drawDc == nullptr || sourceContext == nullptr ||
        wglGetCurrentContext() != sourceContext || wglGetCurrentDC() != drawDc)
    {
        return false;
    }

    const int pixelFormat = GetPixelFormat(drawDc);
    if (pixelFormat == 0)
    {
        return false;
    }

    if (mContext != nullptr && mWindow == window && mSourceContext == sourceContext &&
        mPixelFormat == pixelFormat)
    {
        return true;
    }

    Reset();
    HGLRC overlayContext = wglCreateContext(drawDc);
    if (overlayContext == nullptr)
    {
        return false;
    }

    mWindow = window;
    mSourceContext = sourceContext;
    mContext = overlayContext;
    mPixelFormat = pixelFormat;
    return true;
}

void WglOverlayContext::Reset() noexcept
{
    if (mContext != nullptr)
    {
        if (wglGetCurrentContext() == mContext)
        {
            wglMakeCurrent(nullptr, nullptr);
        }
        wglDeleteContext(mContext);
    }
    mWindow = nullptr;
    mSourceContext = nullptr;
    mContext = nullptr;
    mPixelFormat = 0;
}

ScopedWglContext::ScopedWglContext(HDC drawDc, HGLRC overlayContext) noexcept
    : mPreviousDc(wglGetCurrentDC()),
      mPreviousContext(wglGetCurrentContext())
{
    mBound = drawDc != nullptr && overlayContext != nullptr &&
             wglMakeCurrent(drawDc, overlayContext) != FALSE;
}

ScopedWglContext::~ScopedWglContext()
{
    if (!mBound)
    {
        return;
    }

    if (mPreviousContext != nullptr && mPreviousDc != nullptr)
    {
        wglMakeCurrent(mPreviousDc, mPreviousContext);
    }
    else
    {
        wglMakeCurrent(nullptr, nullptr);
    }
}
}

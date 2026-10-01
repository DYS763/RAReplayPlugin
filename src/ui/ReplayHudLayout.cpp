#include "ReplayHudLayout.h"

#include <algorithm>
#include <cmath>

namespace rareplay::ui
{
std::optional<float> GetSidebarPanelTop(const std::optional<viewer::NativeUiLayout>& layout,
    float displayWidth, float displayHeight, UiRect gameContentRect) noexcept
{
    if (!layout || layout->mCanvasWidth <= 0 || layout->mCanvasHeight <= 0 ||
        layout->mSidebarContentTop <= 0 || layout->mSidebarContentTop >= layout->mCanvasHeight ||
        !std::isfinite(displayWidth) || !std::isfinite(displayHeight) ||
        displayWidth <= 0 || displayHeight <= 0)
        return std::nullopt;

    const float nativeWidth = static_cast<float>(layout->mCanvasWidth);
    const float nativeHeight = static_cast<float>(layout->mCanvasHeight);
    const float nativeTop = static_cast<float>(layout->mSidebarContentTop);
    const float yFraction = nativeTop / nativeHeight;
    float bottom = yFraction * displayHeight;

    // 某些呈现器在全客户区 viewport 内绘制带上下黑边的图像。
    const float fitScale = std::min(displayWidth / nativeWidth, displayHeight / nativeHeight);
    const float fittedTop = (displayHeight - nativeHeight * fitScale) * 0.5f + nativeTop * fitScale;
    bottom = std::max(bottom, fittedTop);

    const float contentHeight = gameContentRect.mBottom - gameContentRect.mTop;
    if (std::isfinite(contentHeight) && contentHeight > 0 && gameContentRect.mTop >= 0 &&
        gameContentRect.mBottom <= displayHeight + 1.0f)
        bottom = std::max(bottom, gameContentRect.mTop + yFraction * contentHeight);
    return bottom;
}
}

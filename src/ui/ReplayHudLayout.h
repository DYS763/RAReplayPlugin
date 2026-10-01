#pragma once

#include "ReplayHud.h"

#include <optional>

namespace rareplay::ui
{
/** 将原生侧栏内容起点投影到客户区；保守覆盖拉伸与居中等比缩放两种方式。 */
std::optional<float> GetSidebarPanelTop(const std::optional<viewer::NativeUiLayout>& layout,
    float displayWidth, float displayHeight, UiRect gameContentRect) noexcept;
}

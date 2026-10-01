#pragma once

#include "../ui/ReplayHud.h"

#include <Windows.h>

#include <vector>

namespace rareplay::render
{
/** 纯命中策略；坐标与矩形均为窗口客户区逻辑像素。 */
bool ShouldDrawOverlayCursor(bool playbackActive, bool overlayVisible, bool foregroundOwner,
                             bool hasMousePosition, float mouseX, float mouseY,
                             const std::vector<ui::UiRect>& inputRegions) noexcept;

/** Win32 NewFrame 之后、ImGui NewFrame 之前调用，补足未通过 WndProc 转发的客户区鼠标位置。 */
void RefreshOverlayMousePosition(HWND window) noexcept;

/** HUD Draw 之后、ImGui Render 之前调用；只在前台 HUD 区域绘制软件箭头。 */
void UpdateOverlayCursor(HWND window, bool playbackActive, bool overlayVisible,
                         const std::vector<ui::UiRect>& inputRegions) noexcept;

/** 隐藏、失焦或回放停用时清除本 ImGui context 的软件游标请求。 */
void ClearOverlayCursor() noexcept;
}

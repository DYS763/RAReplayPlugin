#pragma once

struct ImGuiIO;

namespace rareplay::render
{
/** 从 Windows 系统字体目录装载简体中文字体；失败时安装 ImGui 默认字体。 */
bool LoadOverlayFont(ImGuiIO& io) noexcept;
}

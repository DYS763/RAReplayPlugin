#pragma once

#include "../viewer/ReplayViewModel.h"
#include "IconTextureBudget.h"
#include "imgui.h"

#include <Windows.h>
#include <gl/GL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace rareplay::render
{
/**
 * 缓存由 HUD 专用 WGL 上下文创建的 OpenGL 纹理。
 * Resolve/Clear 只能在该上下文已绑定且渲染互斥由调用方持有时调用。
 */
class IconTextureCache final
{
public:
    ImTextureID Resolve(const std::shared_ptr<const viewer::IconImage>& image) noexcept;
    void Clear() noexcept;

private:
    static constexpr int MAX_DIMENSION = 512;

    IconTextureBudget mBudget;
    std::unordered_map<std::string, GLuint> mTextures;
};
}

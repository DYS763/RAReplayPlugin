#pragma once

#include "../viewer/ReplayViewModel.h"
#include "IconTextureBudget.h"
#include "imgui.h"

#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace rareplay::render
{
/** 只在关联 device 的呈现线程调用；纹理处于 D3DPOOL_DEFAULT，Reset 前必须 Clear。 */
class D3D9IconTextureCache final
{
public:
    ImTextureID Resolve(IDirect3DDevice9* device,
                        const std::shared_ptr<const viewer::IconImage>& image) noexcept;
    void Clear() noexcept;

private:
    static constexpr int MAX_DIMENSION = 512;

    IDirect3DDevice9* mOwner = nullptr;
    IconTextureBudget mBudget;
    std::unordered_map<std::string, IDirect3DTexture9*> mTextures;
};
}

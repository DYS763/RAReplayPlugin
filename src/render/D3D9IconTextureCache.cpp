#include "D3D9IconTextureCache.h"

#include <cstdint>
#include <string>

namespace rareplay::render
{
ImTextureID D3D9IconTextureCache::Resolve(
    IDirect3DDevice9* device, const std::shared_ptr<const viewer::IconImage>& image) noexcept
{
    if (device == nullptr || !image || image->mWidth <= 0 || image->mHeight <= 0 ||
        image->mWidth > MAX_DIMENSION || image->mHeight > MAX_DIMENSION ||
        (mOwner != nullptr && mOwner != device))
    {
        return nullptr;
    }
    const std::size_t expectedSize = static_cast<std::size_t>(image->mWidth) *
                                     static_cast<std::size_t>(image->mHeight) * 4;
    if (image->mRgba.size() != expectedSize)
    {
        return nullptr;
    }

    IDirect3DTexture9* texture = nullptr;
    try
    {
        const std::string key = image->mKey.empty() ?
            "@" + std::to_string(reinterpret_cast<std::uintptr_t>(image.get())) : image->mKey;
        if (const auto found = mTextures.find(key); found != mTextures.end())
        {
            return static_cast<ImTextureID>(found->second);
        }
        // ImDrawData 可能仍引用本帧较早绘制的图标；到上限后显示占位直至清理。
        if (!mBudget.CanAdd(expectedSize))
        {
            return nullptr;
        }

        if (FAILED(device->CreateTexture(static_cast<UINT>(image->mWidth),
                                         static_cast<UINT>(image->mHeight), 1,
                                         D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                         D3DPOOL_DEFAULT, &texture, nullptr)) || texture == nullptr)
        {
            return nullptr;
        }
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD)))
        {
            texture->Release();
            return nullptr;
        }
        if (locked.pBits == nullptr || locked.Pitch < image->mWidth * 4)
        {
            texture->UnlockRect(0);
            texture->Release();
            return nullptr;
        }
        for (int y = 0; y < image->mHeight; ++y)
        {
            const std::uint8_t* source = image->mRgba.data() +
                static_cast<std::size_t>(y) * image->mWidth * 4;
            auto* destination = static_cast<std::uint8_t*>(locked.pBits) +
                static_cast<std::size_t>(y) * locked.Pitch;
            for (int x = 0; x < image->mWidth; ++x)
            {
                destination[x * 4 + 0] = source[x * 4 + 2];
                destination[x * 4 + 1] = source[x * 4 + 1];
                destination[x * 4 + 2] = source[x * 4 + 0];
                destination[x * 4 + 3] = source[x * 4 + 3];
            }
        }
        if (FAILED(texture->UnlockRect(0)))
        {
            texture->Release();
            return nullptr;
        }
        mTextures.emplace(key, texture);
        mBudget.Commit(expectedSize);
        mOwner = device;
        return static_cast<ImTextureID>(texture);
    }
    catch (...)
    {
        if (texture != nullptr)
        {
            texture->Release();
        }
        return nullptr;
    }
}

void D3D9IconTextureCache::Clear() noexcept
{
    for (const auto& entry : mTextures)
    {
        entry.second->Release();
    }
    mTextures.clear();
    mBudget.Reset();
    mOwner = nullptr;
}
}

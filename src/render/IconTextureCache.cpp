#include "IconTextureCache.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace rareplay::render
{
ImTextureID IconTextureCache::Resolve(const std::shared_ptr<const viewer::IconImage>& image) noexcept
{
    if (!image || image->mWidth <= 0 || image->mHeight <= 0 ||
        image->mWidth > MAX_DIMENSION || image->mHeight > MAX_DIMENSION ||
        wglGetCurrentContext() == nullptr)
    {
        return nullptr;
    }
    const std::size_t expectedSize = static_cast<std::size_t>(image->mWidth) *
                                     static_cast<std::size_t>(image->mHeight) * 4;
    if (image->mRgba.size() != expectedSize)
    {
        return nullptr;
    }

    GLuint texture = 0;
    try
    {
        const std::string key = image->mKey.empty() ?
            "@" + std::to_string(reinterpret_cast<std::uintptr_t>(image.get())) : image->mKey;
        if (const auto found = mTextures.find(key); found != mTextures.end())
        {
            return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(found->second));
        }

        // 当前 ImGui 帧可能仍引用已解析纹理；到上限时等待会话切换统一清理。
        if (!mBudget.CanAdd(expectedSize))
        {
            return nullptr;
        }

        GLint oldTexture = 0;
        GLint oldAlignment = 4;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldAlignment);

        glGenTextures(1, &texture);
        if (texture == 0)
        {
            return nullptr;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->mWidth, image->mHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, image->mRgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, oldAlignment);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(oldTexture));

        mTextures.emplace(key, texture);
        mBudget.Commit(expectedSize);
        return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(texture));
    }
    catch (...)
    {
        if (texture != 0)
        {
            glDeleteTextures(1, &texture);
        }
        return nullptr;
    }
}

void IconTextureCache::Clear() noexcept
{
    if (wglGetCurrentContext() == nullptr)
    {
        return;
    }
    for (const auto& entry : mTextures)
    {
        glDeleteTextures(1, &entry.second);
    }
    mTextures.clear();
    mBudget.Reset();
}
}

#pragma once

#include <cstddef>

namespace rareplay::render
{
/** 单会话 GPU 图标预算按 RGBA 像素副本估算；无同帧淘汰。 */
class IconTextureBudget final
{
public:
    static constexpr std::size_t MAX_TEXTURES = 1024;
    static constexpr std::size_t MAX_BYTES = 32U * 1024U * 1024U;

    bool CanAdd(std::size_t bytes) const noexcept
    {
        return mCount < MAX_TEXTURES && mUsedBytes <= MAX_BYTES &&
               bytes <= MAX_BYTES - mUsedBytes;
    }

    void Commit(std::size_t bytes) noexcept
    {
        if (CanAdd(bytes))
        {
            ++mCount;
            mUsedBytes += bytes;
        }
    }

    void Reset() noexcept
    {
        mCount = 0;
        mUsedBytes = 0;
    }

    std::size_t GetCount() const noexcept { return mCount; }
    std::size_t GetUsedBytes() const noexcept { return mUsedBytes; }

private:
    std::size_t mCount = 0;
    std::size_t mUsedBytes = 0;
};
}

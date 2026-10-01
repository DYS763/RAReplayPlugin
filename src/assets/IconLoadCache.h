#pragma once

#include "../viewer/ReplayViewModel.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace rareplay::assets
{
/** 一个会话内的图标缓存；空结果也保存，采样预算仅约束新类型读取。 */
class IconLoadCache final
{
public:
    using Loader = std::function<std::shared_ptr<const viewer::IconImage>()>;

    IconLoadCache(std::size_t maxEntries, int maxLoadsPerSample, std::size_t maxBytes);
    void BeginSample() noexcept;
    void Clear() noexcept;
    std::shared_ptr<const viewer::IconImage> Resolve(const std::string& key,
        const Loader& loader, bool& outAtCapacity);

private:
    std::size_t mMaxEntries;
    std::size_t mMaxBytes;
    std::size_t mUsedBytes = 0;
    int mMaxLoadsPerSample;
    int mLoadsThisSample = 0;
    std::unordered_map<std::string, std::shared_ptr<const viewer::IconImage>> mImages;
};
}

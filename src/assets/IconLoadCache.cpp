#include "IconLoadCache.h"

namespace rareplay::assets
{
IconLoadCache::IconLoadCache(std::size_t maxEntries, int maxLoadsPerSample, std::size_t maxBytes)
    : mMaxEntries(maxEntries), mMaxBytes(maxBytes), mMaxLoadsPerSample(maxLoadsPerSample) {}

void IconLoadCache::BeginSample() noexcept
{
    mLoadsThisSample = 0;
}

void IconLoadCache::Clear() noexcept
{
    mImages.clear();
    mLoadsThisSample = 0;
    mUsedBytes = 0;
}

std::shared_ptr<const viewer::IconImage> IconLoadCache::Resolve(const std::string& key,
    const Loader& loader, bool& outAtCapacity)
{
    outAtCapacity = false;
    if (const auto found = mImages.find(key); found != mImages.end())
        return found->second;
    if (mImages.size() >= mMaxEntries)
    {
        outAtCapacity = true;
        return {};
    }
    if (mUsedBytes >= mMaxBytes)
    {
        // 容量已满的类型也做负缓存，避免每个采样周期再次访问原生资源。
        mImages.emplace(key, nullptr);
        outAtCapacity = true;
        return {};
    }
    if (mLoadsThisSample >= mMaxLoadsPerSample)
        return {};
    ++mLoadsThisSample;
    auto image = loader();
    if (image && image->mRgba.size() > mMaxBytes - mUsedBytes)
    {
        image.reset();
        outAtCapacity = true;
    }
    else if (image)
        mUsedBytes += image->mRgba.size();
    mImages.emplace(key, image);
    return image;
}
}

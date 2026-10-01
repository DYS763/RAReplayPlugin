#pragma once

#include "../viewer/ReplayViewModel.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rareplay::game_api
{
struct IconStats
{
    std::uint32_t mPcx = 0;
    std::uint32_t mShp = 0;
    std::uint32_t mMissing = 0;
    std::uint32_t mInvalidSurface = 0;
    std::uint32_t mCacheLimit = 0;
    std::string mRecentDetail;
};

/** 由一个播放会话持有；只在游戏线程采样，向视图复制值而不传递 SDK 指针。 */
class ReplayWorldReader final
{
public:
    ReplayWorldReader();
    ~ReplayWorldReader();
    ReplayWorldReader(const ReplayWorldReader&) = delete;
    ReplayWorldReader& operator=(const ReplayWorldReader&) = delete;
    void Read(viewer::ReplayViewSnapshot& snapshot);
    void Reset() noexcept;
    const IconStats& GetIconStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
}

#pragma once

#include "ReplayArchive.h"
#include "../replay/ReplayPorts.h"
#include "../replay/ReplaySeek.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rareplay::storage
{
/** 完整校验容器后按顺序读取帧；文件句柄由本对象独占。 */
class FrameReader final : public replay::IReplaySource
{
public:
    bool Open(const std::wstring& path, const std::array<std::uint8_t, 32>& expectedTargetHash,
        std::vector<std::uint8_t>& outMetadata, std::string& error);
    bool GetSessionInfo(replay::ReplaySessionInfo& outInfo, std::string& error) override;
    replay::ReplayReadResult ReadFrame(replay::ReplayFrame& outFrame, std::string& error) override;
    bool SeekFrame(std::uint32_t frameNumber, std::string& error) override;
    std::optional<std::uint32_t> GetTotalFrames() const noexcept override;
    void Close();

private:
    bool BuildIndex(std::string& error);

    ArchiveReader mArchive;
    replay::ReplaySessionInfo mInfo;
    replay::ReplayFrameIndex mIndex;
    std::uint64_t mNextFrame = 0;
    bool mIsOpen = false;
    bool mHasFailed = false;
};
}

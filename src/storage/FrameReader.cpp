#include "FrameReader.h"

#include "../replay/FrameCodec.h"

#include <cstdio>
#include <utility>

namespace rareplay::storage
{
bool FrameReader::Open(const std::wstring& path,
    const std::array<std::uint8_t, 32>& expectedTargetHash,
    std::vector<std::uint8_t>& outMetadata, std::string& error)
{
    Close();
    ArchiveInfo info;
    if (!mArchive.Open(path, info, error))
    {
        return false;
    }
    if (info.mTargetHash != expectedTargetHash)
    {
        error = "Replay target executable hash mismatch";
        Close();
        return false;
    }
    if (!replay::FrameCodec::DecodeHeader(mArchive.GetStream(), mInfo, error))
    {
        Close();
        return false;
    }
    if (!BuildIndex(error))
    {
        Close();
        return false;
    }
    mNextFrame = mInfo.mFirstFrame;
    mIsOpen = true;
    outMetadata = std::move(info.mMetadata);
    error.clear();
    return true;
}

bool FrameReader::GetSessionInfo(replay::ReplaySessionInfo& outInfo, std::string& error)
{
    if (!mIsOpen || mHasFailed)
    {
        error = "Replay reader is not open";
        return false;
    }
    outInfo = mInfo;
    error.clear();
    return true;
}

replay::ReplayReadResult FrameReader::ReadFrame(replay::ReplayFrame& outFrame,
    std::string& error)
{
    if (!mIsOpen || mHasFailed)
    {
        error = "Replay reader is not open";
        return replay::ReplayReadResult::Error;
    }
    replay::ReplayFrame frame;
    const replay::ReplayReadResult result =
        replay::FrameCodec::ReadFrame(mArchive.GetStream(), frame, error, mInfo.mStreamVersion);
    if (result == replay::ReplayReadResult::Error)
    {
        mHasFailed = true;
        return result;
    }
    if (result == replay::ReplayReadResult::End)
    {
        return result;
    }
    if (mNextFrame > UINT32_MAX || frame.mFrameNumber != mNextFrame)
    {
        error = "Replay frame sequence is not contiguous";
        mHasFailed = true;
        return replay::ReplayReadResult::Error;
    }
    ++mNextFrame;
    outFrame = std::move(frame);
    error.clear();
    return replay::ReplayReadResult::Frame;
}

bool FrameReader::SeekFrame(std::uint32_t frameNumber, std::string& error)
{
    if (!mIsOpen || mHasFailed)
    {
        error = "Replay reader is not open";
        return false;
    }
    const auto checkpoint = mIndex.FindCheckpoint(frameNumber);
    if (!checkpoint || !mArchive.Seek(static_cast<int>(checkpoint->mStreamOffset), SEEK_SET))
    {
        error = "Replay target frame is outside the indexed stream";
        return false;
    }
    mNextFrame = checkpoint->mFrameNumber;
    replay::ReplayFrame skipped;
    while (mNextFrame < frameNumber)
    {
        const auto result = ReadFrame(skipped, error);
        if (result != replay::ReplayReadResult::Frame)
        {
            if (error.empty())
                error = "Unable to seek to replay target frame";
            return false;
        }
    }
    error.clear();
    return true;
}

std::optional<std::uint32_t> FrameReader::GetTotalFrames() const noexcept
{
    if (!mIsOpen || !mIndex.IsComplete() || !mIndex.GetLastFrame())
        return std::nullopt;
    return mIndex.GetFrameCount();
}

bool FrameReader::BuildIndex(std::string& error)
{
    mIndex.Reset(mInfo.mFirstFrame);
    while (true)
    {
        const auto offset = mArchive.GetPosition();
        replay::ReplayFrame frame;
        const auto result = replay::FrameCodec::ReadFrame(
            mArchive.GetStream(), frame, error, mInfo.mStreamVersion);
        if (result == replay::ReplayReadResult::End)
            break;
        if (result == replay::ReplayReadResult::Error ||
            !mIndex.AddFrame(frame.mFrameNumber, static_cast<std::uint64_t>(offset), error))
        {
            return false;
        }
    }
    if (!mIndex.Finish(error) ||
        !mArchive.Seek(static_cast<int>(replay::FrameCodec::HEADER_BYTES), SEEK_SET))
    {
        if (error.empty())
            error = "Unable to rewind replay stream after indexing";
        return false;
    }
    error.clear();
    return true;
}

void FrameReader::Close()
{
    mArchive.Close();
    mInfo = {};
    mIndex.Reset();
    mNextFrame = 0;
    mIsOpen = false;
    mHasFailed = false;
}
}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rareplay::replay
{
struct ReplayFrameIndexEntry
{
    std::uint32_t mFrameNumber = 0;
    std::uint64_t mStreamOffset = 0;
};

/**
 * 稀疏帧索引的领域表示。调用方为每一帧提供偏移，本类只保留检查点，
 * 以便在长录像中限制索引内存并从检查点顺序解码到目标帧。
 */
class ReplayFrameIndex final
{
public:
    explicit ReplayFrameIndex(std::uint32_t checkpointStride = 300) noexcept
        : mCheckpointStride(checkpointStride == 0 ? 1 : checkpointStride) {}

    void Reset(std::uint32_t firstFrame = 0) noexcept
    {
        mFirstFrame = firstFrame;
        mNextFrame = firstFrame;
        mLastFrame.reset();
        mCheckpoints.clear();
        mIsComplete = false;
    }

    bool AddFrame(std::uint32_t frameNumber, std::uint64_t streamOffset, std::string& error)
    {
        if (mIsComplete || frameNumber != mNextFrame)
        {
            error = "Replay frame index sequence is not contiguous";
            return false;
        }
        if (mNextFrame == mFirstFrame ||
            (mNextFrame - mFirstFrame) % mCheckpointStride == 0)
        {
            mCheckpoints.push_back({frameNumber, streamOffset});
        }
        ++mNextFrame;
        mLastFrame = frameNumber;
        error.clear();
        return true;
    }

    bool Finish(std::string& error) noexcept
    {
        if (mIsComplete)
        {
            error = "Replay frame index is already complete";
            return false;
        }
        mIsComplete = true;
        error.clear();
        return true;
    }

    bool IsComplete() const noexcept { return mIsComplete; }
    std::uint32_t GetFirstFrame() const noexcept { return mFirstFrame; }
    std::optional<std::uint32_t> GetLastFrame() const noexcept { return mLastFrame; }
    std::uint32_t GetFrameCount() const noexcept
    {
        return mLastFrame ? *mLastFrame - mFirstFrame + 1 : 0;
    }
    const std::vector<ReplayFrameIndexEntry>& GetCheckpoints() const noexcept
    {
        return mCheckpoints;
    }

    std::optional<ReplayFrameIndexEntry> FindCheckpoint(std::uint32_t targetFrame) const noexcept
    {
        if (!mIsComplete || !mLastFrame || targetFrame < mFirstFrame ||
            targetFrame > *mLastFrame || mCheckpoints.empty())
            return std::nullopt;

        ReplayFrameIndexEntry result = mCheckpoints.front();
        for (const auto& checkpoint : mCheckpoints)
        {
            if (checkpoint.mFrameNumber > targetFrame)
                break;
            result = checkpoint;
        }
        return result;
    }

private:
    std::uint32_t mCheckpointStride = 300;
    std::uint32_t mFirstFrame = 0;
    std::uint32_t mNextFrame = 0;
    std::optional<std::uint32_t> mLastFrame;
    std::vector<ReplayFrameIndexEntry> mCheckpoints;
    bool mIsComplete = false;
};

struct WorldSnapshotRecord
{
    std::uint32_t mFrameNumber = 0;
    std::string mPath;
    bool mReady = false;
};
}

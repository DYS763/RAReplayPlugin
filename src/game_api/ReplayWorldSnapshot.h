#pragma once

#include "../replay/ReplaySeek.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rareplay::game_api
{
enum class SnapshotRestorePoll
{
    Idle,
    Pending,
    Completed,
    Failed
};

/** 通过目标游戏的存档接口保存和恢复回放世界，不复制原生对象内存。 */
class ReplayWorldSnapshotStore final
{
public:
    static constexpr std::uint32_t DEFAULT_INTERVAL_SECONDS = 20;
    static constexpr std::size_t MAX_SNAPSHOTS = 256;

    ReplayWorldSnapshotStore() = default;
    ~ReplayWorldSnapshotStore();
    ReplayWorldSnapshotStore(const ReplayWorldSnapshotStore&) = delete;
    ReplayWorldSnapshotStore& operator=(const ReplayWorldSnapshotStore&) = delete;

    bool Begin(std::uint32_t desiredFrameRate, const std::filesystem::path& replayPath,
        std::string& error);
    bool CaptureIfDue(std::uint32_t frameNumber, std::string& error);
    bool RequestRestore(std::uint32_t targetFrame, std::string& error);
    SnapshotRestorePoll PollRestore(bool worldReady, std::uint32_t currentFrame,
        std::string& error);
    void End() noexcept;

    bool IsEnabled() const noexcept { return mEnabled; }
    std::uint32_t GetIntervalFrames() const noexcept { return mIntervalFrames; }
    const std::filesystem::path& GetDirectory() const noexcept { return mDirectory; }
    bool IsRestorePending() const noexcept { return mPendingRestore.has_value(); }
    std::optional<std::uint32_t> GetPendingRestoreFrame() const noexcept;
    const std::vector<replay::WorldSnapshotRecord>& GetRecords() const noexcept
    {
        return mRecords;
    }

    static std::uint32_t ComputeIntervalFrames(std::uint32_t desiredFrameRate,
        std::uint32_t intervalSeconds = DEFAULT_INTERVAL_SECONDS) noexcept;

private:
    bool SaveSnapshot(std::uint32_t frameNumber, std::string& error);
    std::optional<replay::WorldSnapshotRecord> FindNearest(std::uint32_t targetFrame) const;
    bool PrepareDirectory(const std::filesystem::path& replayPath, std::string& error);
    bool LoadRecords(std::string& error);

    std::filesystem::path mDirectory;
    std::string mFilePrefix;
    std::vector<replay::WorldSnapshotRecord> mRecords;
    std::optional<replay::WorldSnapshotRecord> mPendingRestore;
    std::uint32_t mIntervalFrames = 1;
    std::uint32_t mNextCaptureFrame = 0;
    bool mEnabled = false;
};
}

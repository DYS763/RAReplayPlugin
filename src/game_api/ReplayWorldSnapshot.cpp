#include "ReplayWorldSnapshot.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using byte = unsigned char;

#include <ScenarioClass.h>

#include <algorithm>
#include <cstdio>
#include <limits>

namespace rareplay::game_api
{
namespace
{
constexpr wchar_t SNAPSHOT_DESCRIPTION[] = L"RAReplay checkpoint";

void SetError(std::string& error, const char* message)
{
    error = message;
}
}

ReplayWorldSnapshotStore::~ReplayWorldSnapshotStore()
{
    End();
}

std::uint32_t ReplayWorldSnapshotStore::ComputeIntervalFrames(
    std::uint32_t desiredFrameRate, std::uint32_t intervalSeconds) noexcept
{
    if (desiredFrameRate == 0 || intervalSeconds == 0)
        return 1;
    const auto seconds = static_cast<std::uint64_t>(desiredFrameRate) * intervalSeconds;
    return seconds > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(seconds);
}

bool ReplayWorldSnapshotStore::Begin(std::uint32_t desiredFrameRate, std::string& error)
{
    End();
    mIntervalFrames = ComputeIntervalFrames(desiredFrameRate);
    if (!CreateDirectory(error))
        return false;
    mEnabled = true;
    mNextCaptureFrame = 0;
    error.clear();
    return true;
}

bool ReplayWorldSnapshotStore::CaptureIfDue(std::uint32_t frameNumber, std::string& error)
{
    if (!mEnabled)
    {
        error.clear();
        return true;
    }
    if (frameNumber < mNextCaptureFrame ||
        (!mRecords.empty() && mRecords.back().mFrameNumber == frameNumber))
    {
        error.clear();
        return true;
    }
    if (!SaveSnapshot(frameNumber, error))
    {
        mEnabled = false;
        return false;
    }
    const auto next = static_cast<std::uint64_t>(frameNumber) + mIntervalFrames;
    mNextCaptureFrame = next > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(next);
    error.clear();
    return true;
}

bool ReplayWorldSnapshotStore::RequestRestore(std::uint32_t targetFrame, std::string& error)
{
    if (!mEnabled)
    {
        SetError(error, "Replay world snapshots are disabled");
        return false;
    }
    if (mPendingRestore)
    {
        SetError(error, "Replay world snapshot restore is already pending");
        return false;
    }
    const auto record = FindNearest(targetFrame);
    if (!record)
    {
        SetError(error, "No replay world snapshot is available for the target frame");
        return false;
    }
    if (!ScenarioClass::LoadGame(record->mPath.c_str()))
    {
        SetError(error, "Target game rejected replay world snapshot load");
        return false;
    }
    mPendingRestore = record;
    error.clear();
    return true;
}

SnapshotRestorePoll ReplayWorldSnapshotStore::PollRestore(bool worldReady,
    std::uint32_t currentFrame, std::string& error)
{
    if (!mPendingRestore)
    {
        error.clear();
        return SnapshotRestorePoll::Idle;
    }
    if (!worldReady)
    {
        error.clear();
        return SnapshotRestorePoll::Pending;
    }
    if (currentFrame != mPendingRestore->mFrameNumber)
    {
        error = "Loaded replay snapshot restored an unexpected game frame";
        mPendingRestore.reset();
        return SnapshotRestorePoll::Failed;
    }
    mPendingRestore.reset();
    error.clear();
    return SnapshotRestorePoll::Completed;
}

void ReplayWorldSnapshotStore::End() noexcept
{
    mPendingRestore.reset();
    mEnabled = false;
    mNextCaptureFrame = 0;
    RemoveFiles();
    mRecords.clear();
}

std::optional<std::uint32_t> ReplayWorldSnapshotStore::GetPendingRestoreFrame() const noexcept
{
    if (!mPendingRestore)
        return std::nullopt;
    return mPendingRestore->mFrameNumber;
}

bool ReplayWorldSnapshotStore::SaveSnapshot(std::uint32_t frameNumber, std::string& error)
{
    if (ScenarioClass::Instance == nullptr)
    {
        SetError(error, "Scenario is not ready for replay world snapshot");
        return false;
    }
    char path[512]{};
    const int written = std::snprintf(path, sizeof(path), "%s\\frame_%010u.sav",
        mDirectory.c_str(), frameNumber);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(path))
    {
        SetError(error, "Replay world snapshot path is too long");
        return false;
    }
    if (!ScenarioClass::SaveGame(path, SNAPSHOT_DESCRIPTION, false))
    {
        SetError(error, "Target game rejected replay world snapshot save");
        return false;
    }
    mRecords.push_back({frameNumber, path, true});
    while (mRecords.size() > MAX_SNAPSHOTS)
    {
        if (mRecords.size() <= 1)
            break;
        DeleteFileA(mRecords[1].mPath.c_str());
        mRecords.erase(mRecords.begin() + 1);
    }
    error.clear();
    return true;
}

std::optional<replay::WorldSnapshotRecord> ReplayWorldSnapshotStore::FindNearest(
    std::uint32_t targetFrame) const
{
    std::optional<replay::WorldSnapshotRecord> result;
    for (const auto& record : mRecords)
    {
        if (!record.mReady || record.mFrameNumber > targetFrame)
            break;
        result = record;
    }
    return result;
}

bool ReplayWorldSnapshotStore::CreateDirectory(std::string& error)
{
    char temporaryPath[MAX_PATH]{};
    const DWORD pathLength = GetTempPathA(static_cast<DWORD>(sizeof(temporaryPath)), temporaryPath);
    if (pathLength == 0 || pathLength >= sizeof(temporaryPath))
    {
        SetError(error, "Unable to find a temporary directory for replay snapshots");
        return false;
    }
    char uniquePath[MAX_PATH]{};
    if (GetTempFileNameA(temporaryPath, "RRP", 0, uniquePath) == 0 ||
        !DeleteFileA(uniquePath) || !CreateDirectoryA(uniquePath, nullptr))
    {
        SetError(error, "Unable to create replay snapshot directory");
        return false;
    }
    mDirectory = uniquePath;
    return true;
}

void ReplayWorldSnapshotStore::RemoveFiles() noexcept
{
    for (const auto& record : mRecords)
        DeleteFileA(record.mPath.c_str());
    if (!mDirectory.empty())
        RemoveDirectoryA(mDirectory.c_str());
    mDirectory.clear();
}
}

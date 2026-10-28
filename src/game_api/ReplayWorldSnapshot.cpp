#include "ReplayWorldSnapshot.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using byte = unsigned char;

#include <ScenarioClass.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <string_view>

namespace rareplay::game_api
{
namespace
{
constexpr wchar_t SNAPSHOT_DESCRIPTION[] = L"RAReplay checkpoint";
constexpr std::size_t MAX_SNAPSHOT_PATH_BYTES = 512;
constexpr std::size_t GAME_FILENAME_BUFFER_CHARS = 64;

void SetError(std::string& error, const char* message)
{
    error = message;
}

std::string MakeReplayToken(const std::string& replayName)
{
    // SaveGame's internal ANSI-to-wide conversion accepts only a short name.
    // A stable token keeps snapshots from different replays separate while
    // leaving enough room for the frame number and extension.
    std::uint32_t hash = 2166136261u;
    for (const unsigned char character : replayName)
    {
        hash ^= character;
        hash *= 16777619u;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(8) << hash;
    return stream.str();
}

template <typename Invoke>
bool InvokeWithSnapshotDirectory(const std::filesystem::path& snapshotPath,
    Invoke&& invoke, std::string& error)
{
    const auto fileName = snapshotPath.filename().string();
    if (fileName.empty() || fileName.size() >= GAME_FILENAME_BUFFER_CHARS)
    {
        SetError(error, "Replay world snapshot filename is too long for target game");
        return false;
    }

    std::array<wchar_t, MAX_PATH> previousDirectory{};
    const DWORD previousLength = GetCurrentDirectoryW(
        static_cast<DWORD>(previousDirectory.size()), previousDirectory.data());
    if (previousLength == 0 || previousLength >= previousDirectory.size())
    {
        SetError(error, "Unable to read target game working directory");
        return false;
    }
    if (!SetCurrentDirectoryW(snapshotPath.parent_path().c_str()))
    {
        SetError(error, "Unable to enter replay directory for world snapshot");
        return false;
    }

    const bool wasPaused = ScenarioClass::Instance != nullptr &&
        ScenarioClass::Instance->IsGamePaused;
    if (!wasPaused)
        ScenarioClass::PauseGame();
    const bool result = invoke(fileName.c_str());
    if (!wasPaused)
        ScenarioClass::ResumeGame();
    const DWORD restoreError = SetCurrentDirectoryW(previousDirectory.data()) ? ERROR_SUCCESS : GetLastError();
    if (!result)
    {
        error = "Target game rejected replay world snapshot file: " + snapshotPath.string();
        if (restoreError != ERROR_SUCCESS)
            error += " (also failed to restore working directory, error " + std::to_string(restoreError) + ")";
        return false;
    }
    if (restoreError != ERROR_SUCCESS)
    {
        error = "Unable to restore target game working directory (error " +
            std::to_string(restoreError) + ")";
        return false;
    }
    error.clear();
    return true;
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

bool ReplayWorldSnapshotStore::Begin(std::uint32_t desiredFrameRate,
    const std::filesystem::path& replayPath, std::string& error)
{
    mPendingRestore.reset();
    mEnabled = false;
    mNextCaptureFrame = 0;
    mDirectory.clear();
    mFilePrefix.clear();
    mRecords.clear();
    mIntervalFrames = ComputeIntervalFrames(desiredFrameRate);
    if (!PrepareDirectory(replayPath, error) || !LoadRecords(error))
    {
        mDirectory.clear();
        mFilePrefix.clear();
        mRecords.clear();
        return false;
    }
    mEnabled = true;
    const auto baseline = std::find_if(mRecords.begin(), mRecords.end(), [](const auto& record) {
        return record.mReady && record.mFrameNumber == 0;
    });
    if (baseline == mRecords.end())
    {
        // The first world frame is still being assembled when the replay
        // bootstrap finishes. Capture the baseline after frame 1 completes.
        mNextCaptureFrame = 1;
    }
    else if (mRecords.size() >= MAX_SNAPSHOTS)
    {
        mNextCaptureFrame = std::numeric_limits<std::uint32_t>::max();
    }
    else
    {
        const auto next = static_cast<std::uint64_t>(mRecords.back().mFrameNumber) + mIntervalFrames;
        mNextCaptureFrame = next > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(next);
    }
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
    if (mRecords.size() >= MAX_SNAPSHOTS)
    {
        // 快照达到上限后只停止新增，已有快照仍可用于后退跳转。
        error.clear();
        return true;
    }
    if (frameNumber < mNextCaptureFrame ||
        std::any_of(mRecords.begin(), mRecords.end(), [frameNumber](const auto& record) {
            return record.mReady && record.mFrameNumber == frameNumber;
        }))
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
    std::string invokeError;
    if (!InvokeWithSnapshotDirectory(record->mPath,
            [](const char* fileName) { return ScenarioClass::LoadGame(fileName); }, invokeError))
    {
        error = invokeError.empty() ? "Target game rejected replay world snapshot load" : invokeError;
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
    mRecords.clear();
    mDirectory.clear();
    mFilePrefix.clear();
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
    char frameSuffix[16]{};
    const int written = std::snprintf(frameSuffix, sizeof(frameSuffix), "%010u.sav", frameNumber);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(frameSuffix))
    {
        SetError(error, "Replay world snapshot path is too long");
        return false;
    }
    const auto snapshotPath = mDirectory / (mFilePrefix + frameSuffix);
    const std::string path = snapshotPath.string();
    if (path.empty() || path.size() >= MAX_SNAPSHOT_PATH_BYTES)
    {
        SetError(error, "Replay world snapshot path is too long");
        return false;
    }
    std::string invokeError;
    if (!InvokeWithSnapshotDirectory(snapshotPath,
            [](const char* fileName) {
                return ScenarioClass::SaveGame(fileName, SNAPSHOT_DESCRIPTION, false);
            }, invokeError))
    {
        error = invokeError.empty() ? "Target game rejected replay world snapshot save" : invokeError;
        return false;
    }
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(snapshotPath, fileError) || fileError)
    {
        error = "Target game accepted replay world snapshot save but no file was created: " +
            path;
        return false;
    }
    mRecords.push_back({frameNumber, path, true});
    std::sort(mRecords.begin(), mRecords.end(), [](const auto& left, const auto& right) {
        return left.mFrameNumber < right.mFrameNumber;
    });
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

bool ReplayWorldSnapshotStore::PrepareDirectory(const std::filesystem::path& replayPath,
    std::string& error)
{
    if (replayPath.empty() || replayPath.filename().empty())
    {
        SetError(error, "Replay path is unavailable for world snapshots");
        return false;
    }
    std::error_code filesystemError;
    const auto directory = replayPath.parent_path();
    if (directory.empty() || !std::filesystem::is_directory(directory, filesystemError) ||
        filesystemError)
    {
        error = "Replay file directory is unavailable for world snapshots: " +
            (filesystemError ? filesystemError.message() : std::string("path is not a directory"));
        return false;
    }
    const auto replayName = replayPath.filename().string();
    const auto prefix = "RRP_" + MakeReplayToken(replayName) + "_";
    if (prefix.empty() || prefix.size() >= MAX_SNAPSHOT_PATH_BYTES)
    {
        SetError(error, "Replay world snapshot filename prefix is too long");
        return false;
    }
    mDirectory = directory.lexically_normal();
    mFilePrefix = prefix;
    return true;
}

bool ReplayWorldSnapshotStore::LoadRecords(std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::directory_iterator iterator(mDirectory, filesystemError);
    if (filesystemError)
    {
        error = "Unable to scan replay snapshot directory: " + filesystemError.message();
        return false;
    }
    const std::string prefix = mFilePrefix;
    constexpr std::string_view SUFFIX = ".sav";
    const std::filesystem::directory_iterator end;
    while (iterator != end)
    {
        const auto entry = *iterator;
        if (!entry.is_regular_file(filesystemError) || filesystemError)
        {
            filesystemError.clear();
        }
        else
        {
            const auto name = entry.path().filename().string();
            if (name.size() == prefix.size() + 10 + SUFFIX.size() &&
                name.compare(0, prefix.size(), prefix) == 0 &&
                name.compare(name.size() - SUFFIX.size(), SUFFIX.size(), SUFFIX) == 0)
            {
                std::uint32_t frameNumber = 0;
                const auto parseBegin = name.data() + prefix.size();
                const auto parseEnd = parseBegin + 10;
                const auto parsed = std::from_chars(parseBegin, parseEnd, frameNumber);
                if (parsed.ec == std::errc{} && parsed.ptr == parseEnd)
                    mRecords.push_back({frameNumber, entry.path().string(), true});
            }
        }
        iterator.increment(filesystemError);
        if (filesystemError)
            break;
    }
    if (filesystemError)
    {
        error = "Unable to scan replay snapshot directory: " + filesystemError.message();
        return false;
    }
    std::sort(mRecords.begin(), mRecords.end(), [](const auto& left, const auto& right) {
        if (left.mFrameNumber != right.mFrameNumber)
            return left.mFrameNumber < right.mFrameNumber;
        return left.mPath < right.mPath;
    });
    mRecords.erase(std::unique(mRecords.begin(), mRecords.end(), [](const auto& left,
        const auto& right) { return left.mFrameNumber == right.mFrameNumber; }), mRecords.end());
    error.clear();
    return true;
}
}

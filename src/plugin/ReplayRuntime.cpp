#include "ReplayRuntime.h"

#include "Diagnostics.h"
#include "ReplayConfig.h"
#include "ReplayViewPublisher.h"
#include "OverlayDiagnosticsReporter.h"
#include "../hooks/graphics/ReplayOverlayHook.h"
#include "../viewer/ReplayViewExchange.h"
#include "../game_api/GameApi.h"
#include "../game_api/ReplayWorldSnapshot.h"
#include "../replay/ReplaySession.h"
#include "../storage/FrameReader.h"
#include "../storage/FrameWriter.h"
#include "../target/TargetImage.h"

#include <filesystem>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace rareplay::plugin
{
namespace
{
/** 游戏线程上唯一的启动请求、领域会话及文件句柄所有者。 */
class ReplayRuntime
{
public:
    explicit ReplayRuntime(ReplayRequest request) : mRequest(std::move(request)), mSession(mGame) {}

    bool IsPlayback() const { return mRequest.mMode == ReplayMode::Play; }
    bool IsStarted() const { return mIsStarted; }
    bool IsRecordingActive() const
    {
        return mIsStarted && mRequest.mMode == ReplayMode::Record &&
            mSession.GetState() == replay::ReplayState::Recording;
    }
    bool ShouldBlockPlayerCommands() const
    {
        return IsPlayback() && mIsPlaybackSourceOpen && !mHasStartFailed &&
            mSession.GetState() == replay::ReplayState::Playing;
    }
    bool TrySetPlaybackViewingSpeed(int setting)
    {
        return ShouldBlockPlayerCommands() && mGame.TrySetViewerPlaybackSpeed(setting);
    }
    bool TryGetPlaybackViewingSpeed(int& outSetting) const
    {
        return ShouldBlockPlayerCommands() && mGame.TryGetViewerPlaybackSpeed(outSetting);
    }
    bool ShouldHandlePlaybackViewingSpeed() const
    {
        return ShouldBlockPlayerCommands() && mGame.CanControlViewerPlaybackSpeed();
    }

    bool OpenPlaybackSource()
    {
        if (!IsPlayback() || mHasStartFailed)
            return false;
        if (mIsPlaybackSourceOpen)
            return true;
        std::vector<std::uint8_t> metadata;
        std::string error;
        if (!mReader.Open(mRequest.mReplayPath.wstring(), target::TARGET_SHA256_BYTES,
                metadata, error))
            return Fail(error);
        if (!MatchesArchiveManifest(mRequest, metadata))
            return Fail("Replay request manifest differs from archive");
        if (!mReader.GetSessionInfo(mRecordedInfo, error))
            return Fail(error);
        mIsPlaybackSourceOpen = true;
        return true;
    }

    bool TryGetPlaybackSeed(std::uint32_t& outSeed)
    {
        if (!OpenPlaybackSource())
            return false;
        outSeed = mRecordedInfo.mRandomSeed;
        return true;
    }

    void BeginFrame()
    {
        mGame.BeginFrame();
        if (!mGame.IsWorldReady())
            return;
        if (!mIsStarted && !Start())
            return;
        const bool restoreWasPending = mSnapshotStore.IsRestorePending();
        ConsumeViewerCommands();
        if (mSnapshotStore.IsRestorePending())
        {
            // 新请求刚刚调用了 LoadGame；不要在同一轮访问可能正在重建的世界。
            if (!restoreWasPending)
                return;
            const auto pendingFrame = mSnapshotStore.GetPendingRestoreFrame();
            std::uint32_t currentFrame = 0;
            std::uint32_t ignoredChecksum = 0;
            bool ignoredHasChecksum = false;
            std::string error;
            if (!pendingFrame || !mGame.ReadFrameContext(currentFrame, ignoredChecksum,
                    ignoredHasChecksum, error))
            {
                Fail(error.empty() ? "Unable to inspect the restored replay frame" : error);
                return;
            }
            const auto poll = mSnapshotStore.PollRestore(true, currentFrame, error);
            if (poll == game_api::SnapshotRestorePoll::Pending)
                return;
            if (poll == game_api::SnapshotRestorePoll::Failed ||
                !mSession.SeekPlayback(*pendingFrame, error))
            {
                Fail(error.empty() ? "Replay world snapshot restore failed" : error);
                return;
            }
            // LoadGame 可能在本轮调用内立即替换世界；下一轮 BeginFrame 才能让
            // GameApi 重新绑定恢复后的当前帧和输入效果缓冲。
            return;
        }
        if (IsPlayback())
        {
            std::uint32_t currentFrame = 0;
            std::uint32_t ignoredChecksum = 0;
            bool ignoredHasChecksum = false;
            std::string error;
            if (mGame.ReadFrameContext(currentFrame, ignoredChecksum, ignoredHasChecksum, error))
            {
                const auto previousSnapshotCount = mSnapshotStore.GetRecords().size();
                if (!mSnapshotStore.CaptureIfDue(currentFrame, error))
                    Log(error.empty() ? "Replay world snapshot capture failed" : error.c_str());
                else if (mSnapshotStore.GetRecords().size() > previousSnapshotCount)
                {
                    const auto& record = mSnapshotStore.GetRecords().back();
                    char message[512]{};
                    std::snprintf(message, sizeof(message),
                        "Replay world snapshot saved: frame=%u path=%s count=%zu",
                        record.mFrameNumber, record.mPath.c_str(),
                        mSnapshotStore.GetRecords().size());
                    Log(message);
                }
            }
        }
        const auto previousState = mSession.GetState();
        ReportFailure(mSession.OnFrameBegin());
        if (previousState == replay::ReplayState::Playing &&
            mSession.GetState() == replay::ReplayState::Finished)
            Log("Replay playback reached EOF");
    }

    void BeforeNativeEvents()
    {
        if (!mIsStarted || mRequest.mMode != ReplayMode::Record)
            return;
        mGame.BeforeNativeEventExecution();
        ReportFailure(mSession.OnBeforeEvents());
    }

    void BeforeSimulation()
    {
        if (mIsStarted && IsPlayback() &&
            mSession.GetState() == replay::ReplayState::Playing)
            ReportFailure(mSession.OnBeforeSimulation());
    }

    void CapturePlayerInputEffect(const replay::RecordedInputEffect& effect)
    {
        if (!IsRecordingActive())
            return;
        std::string error;
        if (!mGame.RecordPlayerInputEffect(effect, error))
            FailPlayerInputCapture(error);
    }

    void FailPlayerInputCapture(const std::string& error)
    {
        if (!IsRecordingActive())
            return;
        mSession.Stop();
        Fail(error);
    }

    void CompleteFrame()
    {
        if (mIsStarted && mRequest.mMode == ReplayMode::Record)
            ReportFailure(mSession.OnFrameEnd());
        if (mIsStarted && IsPlayback() && mSession.GetState() == replay::ReplayState::Playing)
            UpdateViewer();
    }

    bool ProcessPlaybackFrame()
    {
        if (!IsPlayback())
            return false;
        if (mSnapshotStore.IsRestorePending())
            return true;
        if (!mIsStarted)
        {
            if (!mGame.IsWorldReady() || !Start())
            {
                mGame.EndReplay();
                return true;
            }
        }
        mGame.DiscardLocalCommands();
        mSession.ProcessPlaybackFrame();
        ReportFailure(mSession.GetState() != replay::ReplayState::Failed);
        if (mSeekTargetFrame && mSession.GetCurrentPlaybackFrame() &&
            *mSession.GetCurrentPlaybackFrame() >= *mSeekTargetFrame)
        {
            RestoreSeekSpeed();
            char message[160]{};
            std::snprintf(message, sizeof(message),
                "Replay seek completed: frame=%u", *mSeekTargetFrame);
            Log(message);
            mSeekTargetFrame.reset();
        }
        if (mSession.GetState() != replay::ReplayState::Playing)
            ResetSeekState();
        return true;
    }

    void Finish()
    {
        ResetSeekState();
        if (IsPlayback())
            mOverlayDiagnostics.Report(true);
        mViewPublisher.End();
        mSnapshotStore.End();
        if (mIsStarted && mSession.GetState() == replay::ReplayState::Recording)
        {
            const bool success = mSession.Finish();
            ReportFailure(success);
            if (success)
                Log("Replay recording finalized");
        }
        else
            mSession.Stop();
        mReader.Close();
    }

    void FailUnexpected()
    {
        ResetSeekState();
        mViewPublisher.End();
        mSnapshotStore.End();
        mSession.Stop();
        if (IsPlayback())
            mGame.EndReplay();
        Log("Unexpected exception in replay hook");
    }

private:
    void ConsumeViewerCommands() noexcept
    {
        if (!IsPlayback())
            return;
        try
        {
            for (const auto& command : viewer::GetReplayViewExchange().Drain())
            {
                if (command.mSessionId != mViewPublisher.GetSessionId())
                {
                    Log("Replay HUD command ignored: session mismatch");
                    continue;
                }
                char message[192]{};
                std::snprintf(message, sizeof(message),
                    "Replay HUD command: kind=%s frame=%u value=%d session=%llu",
                    command.mKind == viewer::ViewerCommandKind::SeekFrame ? "seek" : "speed",
                    command.mFrameNumber, command.mValue,
                    static_cast<unsigned long long>(command.mSessionId));
                Log(message);
                if (command.mKind == viewer::ViewerCommandKind::SetPlaybackSpeed)
                    TrySetPlaybackViewingSpeed(command.mValue);
                else if (command.mKind == viewer::ViewerCommandKind::SeekFrame)
                    RequestSeek(command.mFrameNumber);
            }
        }
        catch (...) { Log("Replay HUD command was ignored after an exception"); }
    }

    void RequestSeek(std::uint32_t targetFrame) noexcept
    {
        if (!IsPlayback() || mSnapshotStore.IsRestorePending() ||
            mSession.GetState() != replay::ReplayState::Playing)
            return;
        const auto totalFrames = mReader.GetTotalFrames();
        if (!totalFrames || *totalFrames == 0 || targetFrame >= *totalFrames)
        {
            Log("Replay seek target is outside the indexed frame range");
            return;
        }
        std::uint32_t currentFrame = 0;
        std::uint32_t ignoredChecksum = 0;
        bool ignoredHasChecksum = false;
        std::string error;
        if (!mGame.ReadFrameContext(currentFrame, ignoredChecksum, ignoredHasChecksum, error))
        {
            Log(error.empty() ? "Unable to read current frame for replay seek" : error.c_str());
            return;
        }
        char requestMessage[192]{};
        std::snprintf(requestMessage, sizeof(requestMessage),
            "Replay seek requested: current=%u target=%u total=%u snapshots=%zu",
            currentFrame, targetFrame, *totalFrames, mSnapshotStore.GetRecords().size());
        Log(requestMessage);
        if (targetFrame == currentFrame)
        {
            ResetSeekState();
            return;
        }

        // 新目标取代旧目标；恢复原速度后再决定是否进入新的追帧阶段。
        ResetSeekState();
        mSeekTargetFrame = targetFrame;
        if (targetFrame < currentFrame)
        {
            if (!mSnapshotStore.RequestRestore(targetFrame, error))
            {
                ResetSeekState();
                Log(error.empty() ? "Replay backward seek was rejected" : error.c_str());
                return;
            }
            Log("Replay backward seek snapshot load requested");
            return;
        }

        int currentSpeed = 0;
        if (mGame.TryGetViewerPlaybackSpeed(currentSpeed) && currentSpeed != 0 &&
            mGame.TrySetViewerPlaybackSpeed(0))
        {
            mSeekRestoreSpeed = currentSpeed;
            Log("Replay forward seek entered fast-forward mode");
        }
        else
        {
            Log("Replay forward seek running at current playback speed");
    }

    void RestoreSeekSpeed() noexcept
    {
        if (!mSeekRestoreSpeed)
            return;
        const int speed = *mSeekRestoreSpeed;
        mSeekRestoreSpeed.reset();
        if (!mGame.TrySetViewerPlaybackSpeed(speed))
            Log("Replay seek could not restore playback speed");
    }

    void ResetSeekState() noexcept
    {
        RestoreSeekSpeed();
        mSeekTargetFrame.reset();
    }

    void UpdateViewer() noexcept
    {
        // 界面失败只能停止界面，不能把已验证的回放会话转换为 Failed。
        if (mHasViewFailed)
            return;
        try
        {
            hooks::EnsureReplayOverlayStarted();
            mOverlayDiagnostics.Report();
            int speed = 0;
            const std::optional<int> override = mGame.TryGetViewerPlaybackSpeed(speed)
                ? std::optional<int>(speed) : std::nullopt;
            mViewPublisher.Publish(override, mGame.CanControlViewerPlaybackSpeed());
        }
        catch (...)
        {
            mHasViewFailed = true;
            mViewPublisher.End();
            Log("Replay HUD disabled after a snapshot exception; playback continues");
        }
    }

    bool Start()
    {
        if (mHasStartFailed)
            return false;
        replay::ReplaySessionInfo info;
        std::string error;
        if (!mGame.ReadSessionInfo(info, error))
            return Fail(error);
        if (IsPlayback())
        {
            if (!OpenPlaybackSource())
                return false;
            if (!mSession.StartPlayback(mReader))
            {
                return Fail(mSession.GetLastError());
            }
            std::string snapshotError;
            const auto desiredFrameRate = mGame.GetDesiredFrameRate();
            if (!mSnapshotStore.Begin(desiredFrameRate == 0 ? 60 : desiredFrameRate,
                    mRequest.mReplayPath, snapshotError))
            {
                Log(snapshotError.empty() ? "Replay world snapshots disabled" : snapshotError.c_str());
            }
            else
            {
                char message[512]{};
                std::snprintf(message, sizeof(message),
                    "Replay world snapshots enabled: directory=%s intervalFrames=%u existing=%zu",
                    mSnapshotStore.GetDirectory().string().c_str(),
                    mSnapshotStore.GetIntervalFrames(), mSnapshotStore.GetRecords().size());
                Log(message);
            }
        }
        else if (!mWriter.Open(mRequest.mReplayPath.wstring(), target::TARGET_SHA256_BYTES,
                mRequest.mManifest, info, error) || !mSession.StartRecording(mWriter))
        {
            return Fail(error.empty() ? mSession.GetLastError() : error);
        }
        mIsStarted = true;
        Log(IsPlayback() ? "Replay playback started" : "Replay recording started");
        if (IsPlayback())
        {
            Log("Replay viewer command filtering active; native map input remains available");
            try
            {
                const auto name = mRequest.mReplayPath.filename().u8string();
                mViewPublisher.Begin(std::string(name.begin(), name.end()),
                    static_cast<int>(mRecordedInfo.mLocalHouseIndex), mReader.GetTotalFrames());
            }
            catch (...) { mHasViewFailed = true; Log("Replay HUD initialization failed; playback continues"); }
        }
        return true;
    }

    bool Fail(const std::string& error)
    {
        mViewPublisher.End();
        mSnapshotStore.End();
        ResetSeekState();
        if (!mIsStarted)
            mHasStartFailed = true;
        if (!mHasLoggedFailure)
        {
            Log(error.c_str());
            mHasLoggedFailure = true;
        }
        if (IsPlayback())
            mGame.EndReplay();
        else
            mWriter.Abort();
        return false;
    }

    void ReportFailure(bool success)
    {
        if (!success)
            Fail(mSession.GetLastError());
    }

    ReplayRequest mRequest;
    game_api::GameApi mGame;
    storage::FrameReader mReader;
    storage::FrameWriter mWriter;
    replay::ReplaySession mSession;
    replay::ReplaySessionInfo mRecordedInfo;
    ReplayViewPublisher mViewPublisher;
    game_api::ReplayWorldSnapshotStore mSnapshotStore;
    std::optional<std::uint32_t> mSeekTargetFrame;
    std::optional<int> mSeekRestoreSpeed;
    bool mHasViewFailed = false;
    OverlayDiagnosticsReporter mOverlayDiagnostics;
    bool mIsStarted = false;
    bool mIsPlaybackSourceOpen = false;
    bool mHasStartFailed = false;
    bool mHasLoggedFailure = false;
};

std::unique_ptr<ReplayRuntime> g_Runtime;
bool g_StartupAttempted = false;
}

void OnStartup() noexcept
{
    if (g_StartupAttempted)
        return;
    g_StartupAttempted = true;
    try
    {
        target::TargetImage image;
        std::string error;
        const bool isTargetValid = image.LoadCurrentProcess(error);
        const auto gameDirectory = image.GetPath().empty()
            ? std::filesystem::current_path() : image.GetPath().parent_path();
        std::string diagnosticError;
        if (!OpenBootstrapDiagnostics(gameDirectory, diagnosticError))
            Log(diagnosticError.c_str());
        Log("Replay bootstrap entered");
        if (!isTargetValid)
        {
            Log("Target validation failed");
            Log(error.c_str());
            return;
        }
        const auto markerPath = gameDirectory / L"rareplay-runtime.bin";
        ReplayRequest request;
        std::vector<std::uint8_t> rawBytes;
        if (!ReadReplayRequest(markerPath, request, rawBytes, error))
        {
            Log("Replay request read failed");
            Log(error.c_str());
            return;
        }
        if (!ConsumeReplayRequest(markerPath, rawBytes, error))
        {
            Log("Replay request consumption failed");
            Log(error.c_str());
            return;
        }
        g_Runtime = std::make_unique<ReplayRuntime>(std::move(request));
        Log("Replay request consumed");
        if (g_Runtime->IsPlayback())
            g_Runtime->OpenPlaybackSource();
    }
    catch (...)
    {
        Log("Replay startup failed");
    }
}

bool TryGetPlaybackSeed(std::uint32_t& outSeed) noexcept
{
    OnStartup();
    try { return g_Runtime && g_Runtime->TryGetPlaybackSeed(outSeed); }
    catch (...) { OnHookFailure(); return false; }
}

void OnFrameBegin() noexcept
{
    OnStartup();
    try { if (g_Runtime) g_Runtime->BeginFrame(); }
    catch (...) { OnHookFailure(); }
}

void OnBeforeSimulation() noexcept
{
    try { if (g_Runtime) g_Runtime->BeforeSimulation(); }
    catch (...) { OnHookFailure(); }
}

bool ShouldCapturePlayerInputEffect() noexcept
{
    return g_Runtime && g_Runtime->IsRecordingActive();
}

void OnPlayerInputEffectCaptured(const replay::RecordedInputEffect& effect) noexcept
{
    try { if (g_Runtime) g_Runtime->CapturePlayerInputEffect(effect); }
    catch (...) { OnHookFailure(); }
}

void OnPlayerInputEffectCaptureFailed() noexcept
{
    try
    {
        if (g_Runtime)
            g_Runtime->FailPlayerInputCapture("Unsupported movement feedback capture parameters");
    }
    catch (...) { OnHookFailure(); }
}

bool ShouldBlockPlayerCommands() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->ShouldBlockPlayerCommands();
}

bool TrySetPlaybackViewingSpeed(int setting) noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->TrySetPlaybackViewingSpeed(setting);
}

bool TryGetPlaybackViewingSpeed(int& outSetting) noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->TryGetPlaybackViewingSpeed(outSetting);
}

bool ShouldHandlePlaybackViewingSpeed() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->ShouldHandlePlaybackViewingSpeed();
}

void OnBeforeNativeEvents() noexcept
{
    try { if (g_Runtime) g_Runtime->BeforeNativeEvents(); }
    catch (...) { OnHookFailure(); }
}

void OnFrameComplete() noexcept
{
    try { if (g_Runtime) g_Runtime->CompleteFrame(); }
    catch (...) { OnHookFailure(); }
}

bool OnPlaybackParser() noexcept
{
    try { return g_Runtime && g_Runtime->ProcessPlaybackFrame(); }
    catch (...) { OnHookFailure(); return g_Runtime && g_Runtime->IsPlayback(); }
}

bool ShouldBypassPeerProgressWait() noexcept
{
    OnStartup();
    return g_Runtime && g_Runtime->IsPlayback();
}

void OnSessionEnd() noexcept
{
    try
    {
        if (g_Runtime)
        {
            g_Runtime->Finish();
            g_Runtime.reset();
        }
        CloseDiagnostics();
        g_StartupAttempted = false;
    }
    catch (...) { OnHookFailure(); }
}

void OnHookFailure() noexcept
{
    if (g_Runtime)
        g_Runtime->FailUnexpected();
}
}

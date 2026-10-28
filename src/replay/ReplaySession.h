#pragma once

#include "ReplayPorts.h"

#include <cstdint>
#include <optional>
#include <string>

namespace rareplay::replay
{
enum class ReplayState
{
    Inactive,
    Recording,
    Playing,
    Finished,
    Failed
};

/**
 * 游戏线程上的唯一回放状态所有者。端口对象由调用方持有，必须活得比本会话长。
 * 录制帧仅在 OnFrameEnd 提交；最后一个未完成帧会被丢弃。
 */
class ReplaySession
{
public:
    explicit ReplaySession(IReplayGame& game);

    bool StartRecording(IReplaySink& sink);
    bool StartPlayback(IReplaySource& source);
    bool OnFrameBegin();
    /** 原生 Input 之后、Logic.Update 之前应用预取帧的玩家输入效果。 */
    bool OnBeforeSimulation();
    bool OnBeforeEvents();
    bool OnFrameEnd();
    /** 播放开始后始终返回 true，要求调用方绕过真实网络路径。 */
    bool ProcessPlaybackFrame();
    /** 世界已恢复到目标帧后，重置源读取和当前帧状态。 */
    bool SeekPlayback(std::uint32_t frameNumber, std::string& error);
    std::optional<std::uint32_t> GetCurrentPlaybackFrame() const noexcept;
    bool Finish();
    void Stop();

    ReplayState GetState() const noexcept;
    const std::string& GetLastError() const noexcept;

private:
    bool FailRecording(const std::string& error);
    bool FailPlayback(const std::string& error);
    void EndPlayback();

    IReplayGame& mGame;
    IReplaySource* mSource = nullptr;
    IReplaySink* mSink = nullptr;
    ReplayFrame mPendingFrame;
    ReplayFrame mReadyFrame;
    std::uint64_t mNextFrame = 0;
    std::uint64_t mLastProcessedFrame = UINT64_MAX;
    ReplayState mState = ReplayState::Inactive;
    bool mHasPendingFrame = false;
    bool mHasReadyFrame = false;
    bool mHasAppliedInputEffects = false;
    bool mHasCopiedEvents = false;
    bool mWasPlayback = false;
    bool mHasEndedGame = false;
    std::string mLastError;
};
}

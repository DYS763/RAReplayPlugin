#pragma once

#include <cstdint>

#include "../replay/ReplayTypes.h"

namespace rareplay::plugin
{
void OnStartup() noexcept;
bool TryGetPlaybackSeed(std::uint32_t& outSeed) noexcept;
void OnFrameBegin() noexcept;
void OnBeforeSimulation() noexcept;
bool ShouldCapturePlayerInputEffect() noexcept;
void OnPlayerInputEffectCaptured(const replay::RecordedInputEffect& effect) noexcept;
void OnPlayerInputEffectCaptureFailed() noexcept;
bool ShouldBlockPlayerCommands() noexcept;
bool TrySetPlaybackViewingSpeed(int setting) noexcept;
bool TryGetPlaybackViewingSpeed(int& outSetting) noexcept;
bool ShouldHandlePlaybackViewingSpeed() noexcept;
bool IsReplayFastForwarding() noexcept;
void OnBeforeNativeEvents() noexcept;
void OnFrameComplete() noexcept;
bool OnPlaybackParser() noexcept;
bool ShouldBypassPeerProgressWait() noexcept;
void OnSessionEnd() noexcept;
void OnHookFailure() noexcept;
}

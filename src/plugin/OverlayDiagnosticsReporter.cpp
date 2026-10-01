#include "OverlayDiagnosticsReporter.h"
#include "Diagnostics.h"

#include <cstdio>

namespace rareplay::plugin
{
void OverlayDiagnosticsReporter::Report(bool force) noexcept
{
    const auto info = hooks::GetReplayOverlayDiagnostics();
    const auto now = std::chrono::steady_clock::now();
    const bool changed = info.mObservedBackend != mLastBackend || info.mStatus != mLastStatus;
    if (!force && mHasReported && !changed && now < mNextReport)
        return;
    mNextReport = now + std::chrono::seconds(info.mRenderedFrames > 0 ? 30 : 3);
    mHasReported = true;
    mLastBackend = info.mObservedBackend;
    mLastStatus = info.mStatus;
    const char* backend = "unobserved";
    if (info.mObservedBackend == hooks::ReplayGraphicsBackend::OpenGL)
        backend = "OpenGL";
    else if (info.mObservedBackend == hooks::ReplayGraphicsBackend::Direct3D9)
        backend = "Direct3D9";
    char message[512]{};
    std::snprintf(message, sizeof(message),
        "Replay HUD renderer: actual=%s glSwap=%llu glContext=%llu dx9EndScene=%llu dx9Present=%llu drawn=%llu failed=%llu hooks(gl=%d dx9=%d) status=%d reason=%s",
        backend, info.mOpenGlSwapCalls, info.mOpenGlContextFrames, info.mD3D9EndSceneCalls,
        info.mD3D9PresentCalls, info.mRenderedFrames, info.mRenderFailures,
        info.mOpenGlHooked ? 1 : 0, info.mD3D9Hooked ? 1 : 0,
        static_cast<int>(info.mStatus), info.mFailureReason);
    Log(message);
}
}

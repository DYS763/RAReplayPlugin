#pragma once

#include "../hooks/graphics/ReplayOverlayHook.h"

#include <chrono>

namespace rareplay::plugin
{
/** 在游戏线程输出实际呈现计数；安装成功与真正画出 UI 分开记录。 */
class OverlayDiagnosticsReporter final
{
public:
    void Report(bool force = false) noexcept;
private:
    std::chrono::steady_clock::time_point mNextReport;
    hooks::ReplayGraphicsBackend mLastBackend = hooks::ReplayGraphicsBackend::Unknown;
    hooks::ReplayOverlayStatus mLastStatus = hooks::ReplayOverlayStatus::NotStarted;
    bool mHasReported = false;
};
}

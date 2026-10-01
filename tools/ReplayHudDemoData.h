#pragma once

#include "../src/viewer/ReplayViewModel.h"

#include <memory>

namespace rareplay::tools
{
enum class ReplayHudDemoPanel
{
    Production,
    Units,
    Groups
};

struct ReplayHudDemoOptions
{
    ReplayHudDemoPanel mPanel = ReplayHudDemoPanel::Production;
    bool mMissingData = false;
};

/** 生成只用于 UI 预览的值数据，不读取游戏资源或运行时对象。 */
std::shared_ptr<viewer::ReplayViewSnapshot> CreateReplayHudDemoSnapshot(const ReplayHudDemoOptions& options);
}

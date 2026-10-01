#include "ViewerPlaybackSpeed.h"

namespace rareplay::game_api
{
bool ViewerPlaybackSpeed::TrySet(int setting) noexcept
{
    if (setting < 0 || setting > 6)
        return false;
    mOverride = setting;
    return true;
}

bool ViewerPlaybackSpeed::TryGet(int& outSetting) const noexcept
{
    if (!mOverride)
        return false;
    outSetting = *mOverride;
    return true;
}
}

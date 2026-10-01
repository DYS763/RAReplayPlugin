#pragma once

#include <optional>

namespace rareplay::game_api
{
/** 观众本地播放节奏；无覆盖值时让原生代码使用模拟速度。 */
class ViewerPlaybackSpeed final
{
public:
    bool TrySet(int setting) noexcept;
    bool TryGet(int& outSetting) const noexcept;

private:
    std::optional<int> mOverride;
};
}

#pragma once

#include <cstdint>

namespace rareplay::hooks
{
/** 游戏线程内的原生输入调用深度；不修改引擎状态。 */
class ViewerInputScope final
{
public:
    void Enter() noexcept;
    void Leave() noexcept;
    void Reset() noexcept;
    bool IsActive() const noexcept;

private:
    unsigned int mDepth = 0;
};

ViewerInputScope& GetViewerInputScope() noexcept;
bool ShouldSuppressViewerControl(const ViewerInputScope& scope,
    bool isPlaybackActive, std::uintptr_t caller) noexcept;
}

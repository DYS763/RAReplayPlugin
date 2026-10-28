#pragma once

#include "../viewer/ReplayViewModel.h"
#include "imgui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace rareplay::viewer
{
class ReplayViewExchange;
}

namespace rareplay::ui
{
struct UiRect
{
    float mLeft = 0.0f;
    float mTop = 0.0f;
    float mRight = 0.0f;
    float mBottom = 0.0f;
};

enum class ReplayPanel { Production, Units, Groups };

/**
 * 只读回放快照的 ImGui 表示层。宿主拥有实例、纹理和 ImGui 帧生命周期；仅在渲染线程调用。
 * 选择玩家仅改变本界面的信息，不能授予该玩家的游戏控制权或地图视野。
 */
class ReplayHud final
{
public:
    using IconResolver = std::function<ImTextureID(const std::shared_ptr<const viewer::IconImage>&)>;

    /** Draw 要在 ImGui::NewFrame 之后调用；速度请求通过 exchange 送回游戏线程。 */
    void Draw(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange);
    void Reset();

    /** 回调仅解析已上传纹理；空回调或空纹理会显示中性占位。 */
    void SetIconResolver(IconResolver resolver);

    /** 选择下一帧显示的详情标签，仅修改 UI 本地状态。 */
    void SelectPanel(ReplayPanel panel);

    /** 原生游戏图像在主视口中的客户区矩形；无值时按整个视口换算。 */
    void SetGameContentRect(UiRect rect) noexcept { mGameContentRect = rect; }

    /** 返回本次 Draw 中可交互窗口的主视口绘制坐标，供宿主做输入路由。 */
    const std::vector<UiRect>& GetInputRegions() const noexcept;

    /** 返回本次 Draw 中可能遮住原生光标的可见窗口，包含只读 FPS 卡。 */
    const std::vector<UiRect>& GetVisibleRegions() const noexcept;

private:
    std::uint64_t mSessionId = 0;
    int mSelectedHouseIndex = -1;
    IconResolver mIconResolver;
    std::vector<UiRect> mInputRegions;
    std::vector<UiRect> mVisibleRegions;
    ReplayPanel mSelectedPanel = ReplayPanel::Production;
    bool mSelectPanelPending = false;
    UiRect mGameContentRect;
    std::optional<float> mSeekPreviewProgress;
};
}

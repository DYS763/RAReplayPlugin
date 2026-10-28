#include "ReplayHudParts.h"

#include "../viewer/ReplayViewExchange.h"

#include <algorithm>
#include <cmath>

namespace rareplay::ui::detail
{
void DrawFrameControls(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange,
                       std::vector<UiRect>& inputRegions)
{
    constexpr const char* SPEED_LABELS[] = {
        "最快", "很快", "快", "正常", "慢", "很慢", "最慢"};
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool compact = availableWidth < 730.0f;

    if (snapshot.mTotalFrames && *snapshot.mTotalFrames > 0)
    {
        const std::uint32_t lastFrame = *snapshot.mTotalFrames - 1;
        const float denominator = static_cast<float>(std::max<std::uint32_t>(1, lastFrame));
        const float progress = std::clamp(static_cast<float>(snapshot.mFrameNumber) /
                                          denominator, 0.0f, 1.0f);
        ImGui::Text("帧 %u / %u", snapshot.mFrameNumber, *snapshot.mTotalFrames);
        if (compact)
        {
            ImGui::SetNextItemWidth(-1.0f);
        }
        else
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(100.0f, availableWidth - 420.0f));
        }
        float requestedProgress = progress;
        if (ImGui::SliderFloat("##回放时间轴", &requestedProgress, 0.0f, 1.0f, ""))
        {
            requestedProgress = std::clamp(requestedProgress, 0.0f, 1.0f);
        }
        if (snapshot.mActive && ImGui::IsItemDeactivatedAfterEdit())
        {
            const auto requestedFrame = static_cast<std::uint32_t>(std::lround(
                requestedProgress * static_cast<float>(lastFrame)));
            exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SeekFrame,
                              0, std::min(requestedFrame, lastFrame)});
        }
    }
    else
    {
        ImGui::Text("当前帧 %u · 总帧未知", snapshot.mFrameNumber);
    }

    if (!compact)
        ImGui::SameLine();
    const int speed = snapshot.mPlaybackSpeedSetting;
    const char* currentLabel = speed >= 0 && speed <= 6 ? SPEED_LABELS[speed] : "未知";
    if (snapshot.mCanChangePlaybackSpeed && snapshot.mActive)
    {
        ImGui::SetNextItemWidth(compact ? 150.0f : 160.0f);
        if (ImGui::BeginCombo("##播放速度", currentLabel))
        {
            for (int setting = 0; setting <= 6; ++setting)
            {
                if (ImGui::Selectable(SPEED_LABELS[setting], speed == setting))
                {
                    exchange.Submit({snapshot.mSessionId, viewer::ViewerCommandKind::SetPlaybackSpeed, setting});
                }
            }
            const ImVec2 popupPosition = ImGui::GetWindowPos();
            const ImVec2 popupSize = ImGui::GetWindowSize();
            inputRegions.push_back({popupPosition.x, popupPosition.y,
                                    popupPosition.x + popupSize.x, popupPosition.y + popupSize.y});
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("速度");
    }
    else
    {
        ImGui::TextDisabled("速度 %s · 不可调", currentLabel);
    }
    if (ImGui::GetContentRegionAvail().x > 85.0f)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("F10 隐藏");
    }
}
}

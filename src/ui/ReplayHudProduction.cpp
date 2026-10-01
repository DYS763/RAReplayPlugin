#include "ReplayHudParts.h"

#include <algorithm>

namespace rareplay::ui::detail
{
void DrawProductionLines(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver)
{
    if (player == nullptr)
    {
        ImGui::TextDisabled("未选择玩家");
        return;
    }
    if (!player->mHasProductionData)
    {
        ImGui::TextDisabled("该玩家暂无建造数据");
        return;
    }
    if (player->mProductionLines.empty())
    {
        ImGui::TextDisabled("当前没有建造项目");
        return;
    }

    for (std::size_t lineIndex = 0; lineIndex < player->mProductionLines.size(); ++lineIndex)
    {
        const auto& line = player->mProductionLines[lineIndex];
        ImGui::PushID(static_cast<int>(line.mId));
        ImGui::Separator();
        const char* label = line.mLabel.empty() ? "未命名产线" : line.mLabel.c_str();
        ImGui::Text("%s · 第 %zu 条", label, lineIndex + 1);
        DrawIcon(line.mIcon, iconResolver, ImVec2(38.0f, 38.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", line.mProductName.empty() ? "未知建造项目" : line.mProductName.c_str());
        const char* status = line.mReady ? "已就绪" : line.mOnHold ? "已暂停" : "建造中";
        ImGui::TextDisabled("%s · 队列 %u", status, line.mQueuedCount);
        ImGui::EndGroup();
        const float progress = std::clamp(line.mProgress, 0.0f, 1.0f);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 5.0f), "");
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        ImGui::PopID();
    }
}
}

#include "ReplayHudParts.h"

#include <cstdio>

namespace rareplay::ui::detail
{
namespace
{
ImVec4 GetPlayerColor(std::uint32_t rgb)
{
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
}

void DrawPlayerRow(const viewer::PlayerSummary& player, bool isSelected, int& selectedHouseIndex)
{
    ImGui::PushID(player.mHouseIndex);
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float rowHeight = 83.0f;
    if (ImGui::InvisibleButton("选择玩家", ImVec2(rowWidth, rowHeight)))
    {
        selectedHouseIndex = player.mHouseIndex;
    }
    const ImVec2 rowEnd(rowStart.x + rowWidth, rowStart.y + rowHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = isSelected ? IM_COL32(89, 75, 43, 240) : IM_COL32(36, 43, 37, 215);
    drawList->AddRectFilled(rowStart, rowEnd, fill, 4.0f);
    if (isSelected)
    {
        drawList->AddRect(rowStart, rowEnd, IM_COL32(223, 177, 74, 255), 4.0f);
    }
    drawList->AddRectFilled(ImVec2(rowStart.x + 8.0f, rowStart.y + 9.0f),
                            ImVec2(rowStart.x + 20.0f, rowStart.y + 21.0f),
                            ImGui::ColorConvertFloat4ToU32(GetPlayerColor(player.mColorRgb)), 2.0f);

    const char* playerName = player.mName.empty() ? "未命名玩家" : player.mName.c_str();
    char title[256];
    std::snprintf(title, sizeof(title), "%s  %s%s", playerName,
                  player.mIsHuman ? "玩家" : "AI", player.mDefeated ? " · 已战败" : "");
    const ImVec4 titleClip(rowStart.x + 26.0f, rowStart.y + 4.0f,
                            rowEnd.x - 5.0f, rowStart.y + 24.0f);
    const ImVec4 bodyClip(rowStart.x + 9.0f, rowStart.y + 24.0f,
                           rowEnd.x - 5.0f, rowEnd.y - 2.0f);
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 26.0f, rowStart.y + 6.0f),
                      IM_COL32(239, 235, 217, 255), title, nullptr, 0.0f, &titleClip);
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 25.0f),
                      IM_COL32(171, 189, 166, 255),
                      player.mFaction.empty() ? "阵营 —" : player.mFaction.c_str(),
                      nullptr, 0.0f, &bodyClip);

    char economy[128];
    if (player.mCredits)
    {
        std::snprintf(economy, sizeof(economy), "资金 %lld", static_cast<long long>(*player.mCredits));
    }
    else
    {
        std::snprintf(economy, sizeof(economy), "资金 —");
    }
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 44.0f),
                      IM_COL32(218, 218, 203, 255), economy, nullptr, 0.0f, &bodyClip);

    char power[128];
    if (player.mPowerOutput && player.mPowerDrain)
    {
        std::snprintf(power, sizeof(power), "电力 %d / %d%s", *player.mPowerOutput,
                      *player.mPowerDrain, *player.mPowerOutput < *player.mPowerDrain ? " · 停电" : "");
    }
    else
    {
        char output[24];
        char drain[24];
        if (player.mPowerOutput)
        {
            std::snprintf(output, sizeof(output), "%d", *player.mPowerOutput);
        }
        else
        {
            std::snprintf(output, sizeof(output), "—");
        }
        if (player.mPowerDrain)
        {
            std::snprintf(drain, sizeof(drain), "%d", *player.mPowerDrain);
        }
        else
        {
            std::snprintf(drain, sizeof(drain), "—");
        }
        std::snprintf(power, sizeof(power), "电力 %s / %s", output, drain);
    }
    const bool hasOutage = player.mPowerOutput && player.mPowerDrain && *player.mPowerOutput < *player.mPowerDrain;
    drawList->AddText(nullptr, 0.0f, ImVec2(rowStart.x + 9.0f, rowStart.y + 63.0f),
                      hasOutage ? IM_COL32(241, 129, 106, 255) : IM_COL32(176, 198, 176, 255),
                      power, nullptr, 0.0f, &bodyClip);

    ImGui::SetItemTooltip("%s · 阵营：%s", playerName,
                          player.mFaction.empty() ? "未知" : player.mFaction.c_str());
    ImGui::PopID();
}
}

void DrawPlayerList(const viewer::ReplayViewSnapshot& snapshot, int& selectedHouseIndex, float height)
{
    if (ImGui::BeginChild("玩家列表", ImVec2(0.0f, height), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        if (snapshot.mPlayers.empty())
        {
            ImGui::TextDisabled("暂无玩家数据");
        }
        for (const auto& player : snapshot.mPlayers)
        {
            DrawPlayerRow(player, selectedHouseIndex == player.mHouseIndex, selectedHouseIndex);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }
    ImGui::EndChild();
}
}

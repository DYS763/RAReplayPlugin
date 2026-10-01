#include "ReplayHudParts.h"

#include <algorithm>
#include <cstdio>

namespace rareplay::ui::detail
{
namespace
{
const char* GetCategoryLabel(viewer::UnitCategory category)
{
    switch (category)
    {
    case viewer::UnitCategory::Infantry: return "步兵";
    case viewer::UnitCategory::Vehicle: return "载具";
    case viewer::UnitCategory::Aircraft: return "航空";
    case viewer::UnitCategory::Building: return "建筑";
    case viewer::UnitCategory::Other: return "其他";
    }
    return "其他";
}

void DrawUnitTile(const viewer::UnitSummary& unit, const ReplayHud::IconResolver& iconResolver,
                  float tileWidth)
{
    ImGui::PushID(unit.mTypeId.c_str());
    ImGui::BeginGroup();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 tileSize(tileWidth, 94.0f);
    ImGui::InvisibleButton("单位卡", tileSize);
    const char* name = unit.mName.empty() ? unit.mTypeId.c_str() : unit.mName.c_str();
    ImGui::SetItemTooltip("%s · %u", name, unit.mCount);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(start, ImVec2(start.x + tileWidth, start.y + tileSize.y),
                            IM_COL32(39, 47, 41, 235), 4.0f);
    const ImVec4 labelClip(start.x + 5.0f, start.y + 5.0f,
                           start.x + tileWidth - 5.0f, start.y + 29.0f);
    drawList->AddText(nullptr, 0.0f, ImVec2(start.x + 5.0f, start.y + 6.0f),
                      IM_COL32(236, 235, 219, 255), name, nullptr, 0.0f,
                      &labelClip);
    const ImVec2 iconPosition(start.x + (tileWidth - 47.0f) * 0.5f, start.y + 35.0f);
    ImGui::SetCursorScreenPos(iconPosition);
    const UiRect iconRect = DrawIcon(unit.mIcon, iconResolver, ImVec2(47.0f, 47.0f));
    char count[32];
    std::snprintf(count, sizeof(count), "%u", unit.mCount);
    const ImVec2 textSize = ImGui::CalcTextSize(count);
    const ImVec2 badgeMin(iconRect.mRight - textSize.x - 8.0f, iconRect.mBottom - 18.0f);
    drawList->AddRectFilled(badgeMin, ImVec2(iconRect.mRight, iconRect.mBottom),
                            IM_COL32(14, 21, 18, 245), 3.0f);
    drawList->AddText(ImVec2(badgeMin.x + 4.0f, badgeMin.y + 1.0f), IM_COL32(246, 222, 164, 255), count);
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + tileSize.y));
    ImGui::EndGroup();
    ImGui::PopID();
}
}

UiRect DrawIcon(const std::shared_ptr<const viewer::IconImage>& icon,
              const ReplayHud::IconResolver& iconResolver, const ImVec2& size)
{
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImTextureID texture = nullptr;
    if (icon && iconResolver)
    {
        texture = iconResolver(icon);
    }
    if (texture != nullptr)
    {
        const float scale = std::min(size.x / icon->mWidth, size.y / icon->mHeight);
        const ImVec2 imageSize(icon->mWidth * scale, icon->mHeight * scale);
        const ImVec2 imageMin(start.x + (size.x - imageSize.x) * 0.5f,
                              start.y + (size.y - imageSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddImage(texture, imageMin,
            ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y));
        return {imageMin.x, imageMin.y, imageMin.x + imageSize.x, imageMin.y + imageSize.y};
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y),
                            IM_COL32(75, 81, 72, 255), 3.0f);
    drawList->AddRect(start, ImVec2(start.x + size.x, start.y + size.y),
                      IM_COL32(117, 123, 108, 255), 3.0f);
    const char* placeholder = "?";
    const ImVec2 textSize = ImGui::CalcTextSize(placeholder);
    drawList->AddText(ImVec2(start.x + (size.x - textSize.x) * 0.5f,
                             start.y + (size.y - textSize.y) * 0.5f),
                      IM_COL32(195, 199, 187, 255), placeholder);
    ImGui::SetItemTooltip("图标缺失或尚未载入");
    return {start.x, start.y, start.x + size.x, start.y + size.y};
}

void DrawUnitInventory(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver)
{
    if (player == nullptr)
    {
        ImGui::TextDisabled("未选择玩家");
        return;
    }
    if (!player->mHasUnitData)
    {
        ImGui::TextDisabled("该玩家暂无单位数据");
        return;
    }

    static constexpr viewer::UnitCategory CATEGORIES[] = {
        viewer::UnitCategory::Infantry, viewer::UnitCategory::Vehicle,
        viewer::UnitCategory::Aircraft, viewer::UnitCategory::Building,
        viewer::UnitCategory::Other};
    for (const viewer::UnitCategory category : CATEGORIES)
    {
        bool hasUnits = false;
        for (const auto& unit : player->mUnits)
        {
            if (unit.mCategory == category)
            {
                hasUnits = true;
                break;
            }
        }
        if (!hasUnits)
        {
            continue;
        }
        ImGui::TextColored(ImVec4(0.82f, 0.75f, 0.52f, 1.0f), "%s", GetCategoryLabel(category));
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>((availableWidth + spacing) / (90.0f + spacing)));
        const float tileWidth = (availableWidth - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        int column = 0;
        for (const auto& unit : player->mUnits)
        {
            if (unit.mCategory != category)
            {
                continue;
            }
            if (column != 0)
            {
                ImGui::SameLine();
            }
            DrawUnitTile(unit, iconResolver, tileWidth);
            column = (column + 1) % columns;
        }
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
    }
    if (player->mUnits.empty())
    {
        ImGui::TextDisabled("当前没有单位");
    }
}
}

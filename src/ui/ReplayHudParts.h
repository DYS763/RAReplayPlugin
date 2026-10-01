#pragma once

#include "ReplayHud.h"

namespace rareplay::ui::detail
{
void DrawFpsCard(const viewer::ReplayViewSnapshot& snapshot);
void DrawPlayerList(const viewer::ReplayViewSnapshot& snapshot, int& selectedHouseIndex, float height);
void DrawProductionLines(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver);
void DrawUnitInventory(const viewer::PlayerSummary* player, const ReplayHud::IconResolver& iconResolver);
void DrawRecordedGroups(const viewer::ReplayViewSnapshot& snapshot);
void DrawFrameControls(const viewer::ReplayViewSnapshot& snapshot, viewer::ReplayViewExchange& exchange,
                       std::vector<UiRect>& inputRegions);
UiRect DrawIcon(const std::shared_ptr<const viewer::IconImage>& icon,
              const ReplayHud::IconResolver& iconResolver, const ImVec2& size);
}

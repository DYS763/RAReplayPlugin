#include "OverlayCursor.h"

#include "imgui.h"

#include <cmath>

namespace rareplay::render
{
namespace
{
bool IsForegroundOwner(HWND window) noexcept
{
    if (window == nullptr || !IsWindow(window))
    {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId())
    {
        return false;
    }
    const HWND foreground = GetForegroundWindow();
    return foreground != nullptr &&
           (foreground == window || GetAncestor(window, GA_ROOT) == foreground);
}

bool TryReadClientMouse(HWND window, POINT& outPoint) noexcept
{
    if (!GetCursorPos(&outPoint) || !ScreenToClient(window, &outPoint))
    {
        return false;
    }
    RECT client{};
    return GetClientRect(window, &client) && outPoint.x >= client.left &&
           outPoint.y >= client.top && outPoint.x < client.right &&
           outPoint.y < client.bottom;
}
}

bool ShouldDrawOverlayCursor(bool playbackActive, bool overlayVisible, bool foregroundOwner,
                             bool hasMousePosition, float mouseX, float mouseY,
                             const std::vector<ui::UiRect>& inputRegions) noexcept
{
    if (!playbackActive || !overlayVisible || !foregroundOwner || !hasMousePosition ||
        !std::isfinite(mouseX) || !std::isfinite(mouseY))
    {
        return false;
    }
    for (const ui::UiRect& region : inputRegions)
    {
        if (region.mRight > region.mLeft && region.mBottom > region.mTop &&
            mouseX >= region.mLeft && mouseY >= region.mTop &&
            mouseX < region.mRight && mouseY < region.mBottom)
        {
            return true;
        }
    }
    return false;
}

void RefreshOverlayMousePosition(HWND window) noexcept
{
    if (ImGui::GetCurrentContext() == nullptr || !IsForegroundOwner(window))
    {
        return;
    }
    POINT point{};
    if (TryReadClientMouse(window, point))
    {
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(point.x), static_cast<float>(point.y));
    }
}

void UpdateOverlayCursor(HWND window, bool playbackActive, bool overlayVisible,
                         const std::vector<ui::UiRect>& inputRegions) noexcept
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    const bool foregroundOwner = IsForegroundOwner(window);
    if (!playbackActive || !overlayVisible || !foregroundOwner)
    {
        return;
    }
    POINT clientPoint{};
    const bool hasMousePosition = TryReadClientMouse(window, clientPoint);
    if (!ShouldDrawOverlayCursor(playbackActive, overlayVisible, foregroundOwner,
                                 hasMousePosition, static_cast<float>(clientPoint.x),
                                 static_cast<float>(clientPoint.y), inputRegions))
    {
        return;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    if (ShouldDrawOverlayCursor(playbackActive, overlayVisible, foregroundOwner, true,
                                mouse.x, mouse.y, inputRegions))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        io.MouseDrawCursor = true;
    }
}

void ClearOverlayCursor() noexcept
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui::GetIO().MouseDrawCursor = false;
    }
}
}

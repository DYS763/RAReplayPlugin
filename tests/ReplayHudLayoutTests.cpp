#include "../src/ui/ReplayHudLayout.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
}

int main()
{
    try
    {
        using rareplay::ui::GetSidebarPanelTop;
        using rareplay::viewer::NativeUiLayout;
        const auto native = GetSidebarPanelTop(NativeUiLayout{1600, 900, 235}, 1600, 900, {});
        Require(native && std::abs(*native - 235.0f) < 0.01f, "Native-size radar clearance");
        const auto scaled = GetSidebarPanelTop(NativeUiLayout{1024, 768, 220}, 1920, 1080, {});
        Require(scaled && *scaled >= 220.0f * 1080.0f / 768.0f, "Stretched radar bottom is protected");
        const auto letterboxed = GetSidebarPanelTop(NativeUiLayout{1920, 1080, 220}, 1024, 768, {});
        Require(letterboxed && *letterboxed >= 96.0f + 220.0f * 1024.0f / 1920.0f, "Top letterbox offset is protected");
        const auto viewport = GetSidebarPanelTop(NativeUiLayout{1920, 1080, 220}, 1024, 768, {0, 100, 1024, 668});
        Require(viewport && *viewport >= 100.0f + 220.0f * 568.0f / 1080.0f, "Live viewport offset is protected");
        Require(!GetSidebarPanelTop(std::nullopt, 1024, 768, {}), "Unknown layout cannot authorize covering radar");
        Require(!GetSidebarPanelTop(NativeUiLayout{1024, 0, 220}, 1024, 768, {}), "Invalid canvas rejected");
        std::cout << "Replay HUD layout tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

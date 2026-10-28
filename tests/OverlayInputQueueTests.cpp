#include "../src/render/OverlayInputQueue.h"

#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

LPARAM Position(int x, int y) { return MAKELPARAM(x, y); }
}

int main()
{
    try
    {
        rareplay::render::OverlayInputQueue input;
        const std::vector<rareplay::ui::UiRect> regions{{100, 100, 250, 250}, {10, 270, 90, 330}};
        input.PublishPolicy(true, true, false, regions);

        Require(!input.RouteMessage(nullptr, WM_LBUTTONDOWN, 0, Position(30, 30)), "Map down passes through");
        Require(input.Drain().empty(), "Map button must not reach ImGui and alter native mouse capture");
        input.RouteMessage(nullptr, WM_MOUSEMOVE, MK_LBUTTON, Position(120, 120));
        Require(!input.RouteMessage(nullptr, WM_LBUTTONUP, 0, Position(120, 120)), "Map drag ends on UI without capture theft");
        for (const auto& message : input.Drain())
            Require(message.message != WM_LBUTTONDOWN && message.message != WM_LBUTTONUP, "Map drag has no ImGui button pair");

        Require(input.RouteMessage(nullptr, WM_LBUTTONDOWN, 0, Position(120, 120)), "UI down consumed");
        Require(input.RouteMessage(nullptr, WM_LBUTTONUP, 0, Position(30, 30)), "UI up remains paired outside UI");
        const auto uiClick = input.Drain();
        Require(uiClick.size() == 2 && uiClick[0].message == WM_LBUTTONDOWN && uiClick[1].message == WM_LBUTTONUP,
            "Only a complete UI button pair is delivered");

        HWND testWindow = CreateWindowExW(0, L"STATIC", L"ReplayInputTest", WS_POPUP,
            0, 0, 300, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(testWindow != nullptr, "Hidden input fixture window created");
        SetCapture(testWindow);
        Require(input.RouteMessage(testWindow, WM_LBUTTONDOWN, 0, Position(120, 120)), "Existing game capture on same window allows UI click");
        input.RouteMessage(testWindow, WM_LBUTTONUP, 0, Position(120, 120));
        Require(GetCapture() == testWindow, "UI does not release pre-existing game capture");
        ReleaseCapture();
        Require(input.RouteMessage(testWindow, WM_LBUTTONDOWN, 0, Position(120, 120)), "UI can own capture when game has none");
        input.RouteMessage(testWindow, WM_LBUTTONUP, 0, Position(30, 30));
        Require(GetCapture() == nullptr, "UI releases only its owned capture");
        input.Drain();

        HWND otherWindow = CreateWindowExW(0, L"STATIC", L"ReplayInputOther", WS_POPUP,
            0, 0, 300, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(otherWindow != nullptr, "Secondary capture fixture window created");
        SetCapture(otherWindow);
        Require(input.RouteMessage(testWindow, WM_LBUTTONDOWN, 0, Position(120, 120)),
            "UI click can replace an unrelated stale capture");
        input.RouteMessage(testWindow, WM_LBUTTONUP, 0, Position(120, 120));
        Require(GetCapture() == nullptr, "UI releases capture after replacing stale capture");
        DestroyWindow(otherWindow);
        DestroyWindow(testWindow);
        input.Drain();

        Require(input.RouteMessage(nullptr, WM_LBUTTONDOWN, 0, Position(20, 290)), "Popup region also consumes clicks");
        input.RouteMessage(nullptr, WM_LBUTTONUP, 0, Position(20, 290));
        input.Drain();
        input.PublishPolicy(true, true, true, regions);
        Require(!input.RouteMessage(nullptr, WM_SYSKEYDOWN, VK_F4, 0), "Alt F4 must not be swallowed by UI keyboard focus");
        Require(!input.RouteMessage(nullptr, WM_CLOSE, 0, 0), "Window close always passes through");
        Require(!input.RouteMessage(nullptr, WM_SIZE, 0, 0), "Resize always passes through");
        input.PublishPolicy(false, true, true, regions);
        Require(!input.RouteMessage(nullptr, WM_LBUTTONDOWN, 0, Position(120, 120)), "Inactive replay does not consume input");

        input.Clear();
        input.PublishPolicy(true, true, false, regions);
        for (int index = 0; index < 1000; ++index)
            input.RouteMessage(nullptr, WM_MOUSEMOVE, 0, Position(120, 120));
        Require(input.Drain().size() <= rareplay::render::OverlayInputQueue::CAPACITY, "Queue stays bounded");
        std::cout << "Overlay input routing tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}

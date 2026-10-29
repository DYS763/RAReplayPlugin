#include "ReplayOverlayHook.h"
#include "D3D9HookTargets.h"
#include "OverlayWindowHook.h"
#include "../../render/D3D9OverlayRenderer.h"
#include "../../render/ReplayOverlayRenderer.h"
#include "../../viewer/ReplayViewExchange.h"
#include "../../../third_party/minhook/include/MinHook.h"

#include <Windows.h>
#include <d3d9.h>
#include <atomic>
#include <iterator>
#include <mutex>

namespace rareplay::hooks
{
namespace
{
using SwapFunction = BOOL(WINAPI*)(HDC);
using EndSceneFunction = HRESULT(WINAPI*)(IDirect3DDevice9*);
using PresentFunction = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFunction = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

std::atomic<ReplayOverlayStatus> g_Status{ReplayOverlayStatus::NotStarted};
std::atomic<ReplayGraphicsBackend> g_Backend{ReplayGraphicsBackend::Unknown};
std::atomic<const char*> g_Failure{"none"};
std::atomic<bool> g_OpenGlHooked{false}, g_D3D9Hooked{false};
std::atomic<unsigned long long> g_Swaps{0}, g_GlFrames{0}, g_EndScenes{0}, g_Presents{0}, g_Rendered{0}, g_Failures{0};
std::mutex g_RenderMutex;
bool g_OpenGlAttempted = false;
bool g_D3D9Attempted = false;
void* g_GdiOriginal = nullptr;
void* g_WglOriginal = nullptr;
void* g_EndSceneOriginal = nullptr;
void* g_PresentOriginal = nullptr;
void* g_ResetOriginal = nullptr;
thread_local bool g_InPresentation = false;
thread_local bool g_InReset = false;

bool IsActive() noexcept
{
    const auto snapshot = viewer::GetReplayViewExchange().Read();
    return snapshot && snapshot->mActive;
}

HWND GetDeviceWindow(IDirect3DDevice9* device) noexcept
{
    if (!device)
        return nullptr;
    IDirect3DSwapChain9* chain = nullptr;
    D3DPRESENT_PARAMETERS parameters{};
    if (FAILED(device->GetSwapChain(0, &chain)) || !chain)
        return nullptr;
    const HRESULT result = chain->GetPresentParameters(&parameters);
    chain->Release();
    HWND window = parameters.hDeviceWindow;
    if (!window)
    {
        D3DDEVICE_CREATION_PARAMETERS creation{};
        if (SUCCEEDED(device->GetCreationParameters(&creation)))
            window = creation.hFocusWindow;
    }
    DWORD processId = 0;
    if (FAILED(result) || !window || !IsWindow(window))
        return nullptr;
    GetWindowThreadProcessId(window, &processId);
    wchar_t className[96]{};
    // GetWindowText 对同进程窗口会同步发 WM_GETTEXT，呈现线程不能等待游戏线程。
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    if (processId != GetCurrentProcessId() || wcscmp(className, L"RAReplayPlugin.D3D9Probe") == 0)
        return nullptr;
    return window;
}

void PublishResult(bool drawn, HWND window, render::OverlayInputQueue& queue, const char* reason) noexcept
{
    if (drawn && AttachOverlayWindow(window, queue))
    {
        ++g_Rendered;
        g_Status.store(ReplayOverlayStatus::Ready);
        g_Failure.store("none");
    }
    else
    {
        ++g_Failures;
        g_Failure.store(drawn ? "window_input_attachment_failed" : (reason ? reason : "renderer_not_ready"));
        g_Status.store(ReplayOverlayStatus::HookedWaitingForContext);
        queue.Clear();
    }
}

BOOL HandleSwap(HDC drawDc, void* original) noexcept
{
    if (!original)
        return FALSE;
    if (g_InPresentation)
        return reinterpret_cast<SwapFunction>(original)(drawDc);
    g_InPresentation = true;
    ++g_Swaps;
    try
    {
        const HDC currentDc = wglGetCurrentDC();
        const HWND window = WindowFromDC(drawDc);
        if (window && wglGetCurrentContext() && currentDc && WindowFromDC(currentDc) == window)
        {
            ++g_GlFrames;
            g_Backend.store(ReplayGraphicsBackend::OpenGL);
            std::unique_lock lock(g_RenderMutex, std::try_to_lock);
            if (lock.owns_lock())
            {
                if (!viewer::GetReplayViewExchange().IsFastForwarding() && IsActive())
                {
                    auto& renderer = render::GetReplayOverlayRenderer();
                    // 同一窗口的 HDC 句柄可能不同，应使用真正绑定 RC 的那个。
                    const bool drawn = renderer.Render(currentDc, viewer::GetReplayViewExchange());
                    PublishResult(drawn, window, renderer.GetInputQueue(), "opengl_context_or_initialization_failed");
                }
                else
                    ClearOverlayWindowInput();
            }
        }
    }
    catch (...) { g_Failure.store("opengl_callback_exception"); ++g_Failures; ClearOverlayWindowInput(); }
    const BOOL result = reinterpret_cast<SwapFunction>(original)(drawDc);
    g_InPresentation = false;
    return result;
}

BOOL WINAPI GdiSwapHook(HDC dc) { return HandleSwap(dc, g_GdiOriginal); }
BOOL WINAPI WglSwapHook(HDC dc) { return HandleSwap(dc, g_WglOriginal); }

HRESULT WINAPI EndSceneHook(IDirect3DDevice9* device)
{
    if (!g_InPresentation)
    {
        g_InPresentation = true;
        try
        {
            if (const HWND window = GetDeviceWindow(device))
            {
                ++g_EndScenes;
                g_Backend.store(ReplayGraphicsBackend::Direct3D9);
                std::unique_lock lock(g_RenderMutex, std::try_to_lock);
                if (lock.owns_lock())
                {
                    if (!viewer::GetReplayViewExchange().IsFastForwarding() && IsActive())
                    {
                        auto& renderer = render::GetD3D9OverlayRenderer();
                        const bool drawn = renderer.Render(device, viewer::GetReplayViewExchange());
                        PublishResult(drawn, window, renderer.GetInputQueue(), renderer.GetFailureReason());
                    }
                    else
                        ClearOverlayWindowInput();
                }
            }
        }
        catch (...) { g_Failure.store("d3d9_callback_exception"); ++g_Failures; ClearOverlayWindowInput(); }
        g_InPresentation = false;
    }
    return reinterpret_cast<EndSceneFunction>(g_EndSceneOriginal)(device);
}

HRESULT WINAPI PresentHook(IDirect3DDevice9* device, const RECT* source, const RECT* destination,
    HWND overrideWindow, const RGNDATA* dirty)
{
    if (GetDeviceWindow(device))
    {
        ++g_Presents;
        g_Backend.store(ReplayGraphicsBackend::Direct3D9);
    }
    return reinterpret_cast<PresentFunction>(g_PresentOriginal)(device, source, destination, overrideWindow, dirty);
}

HRESULT WINAPI ResetHook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
{
    if (g_InReset)
        return reinterpret_cast<ResetFunction>(g_ResetOriginal)(device, parameters);
    g_InReset = true;
    std::unique_lock lock(g_RenderMutex);
    auto& renderer = render::GetD3D9OverlayRenderer();
    renderer.BeforeReset(device);
    const HRESULT result = reinterpret_cast<ResetFunction>(g_ResetOriginal)(device, parameters);
    renderer.AfterReset(device, result);
    g_InReset = false;
    return result;
}

bool Install(void* target, void* callback, void** original) noexcept
{
    if (!target || MH_CreateHook(target, callback, original) != MH_OK)
        return false;
    if (MH_EnableHook(target) == MH_OK)
        return true;
    // 此 trampoline 从未启用，只释放我们本次创建的目标。
    MH_RemoveHook(target);
    *original = nullptr;
    return false;
}
}

bool EnsureReplayOverlayStarted() noexcept
{
    const MH_STATUS initialization = MH_Initialize();
    if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
    {
        g_Status.store(ReplayOverlayStatus::HookFailed);
        g_Failure.store("hook_library_initialization_failed");
        return false;
    }
    if (!g_OpenGlAttempted)
    {
        if (HMODULE module = GetModuleHandleW(L"opengl32.dll"))
        {
            g_OpenGlAttempted = true;
            HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
            void* gdiTarget = gdi ? reinterpret_cast<void*>(GetProcAddress(gdi, "SwapBuffers")) : nullptr;
            void* wglTarget = reinterpret_cast<void*>(GetProcAddress(module, "wglSwapBuffers"));
            const bool gdiInstalled = Install(gdiTarget, reinterpret_cast<void*>(GdiSwapHook), &g_GdiOriginal);
            const bool wglInstalled = wglTarget != gdiTarget &&
                Install(wglTarget, reinterpret_cast<void*>(WglSwapHook), &g_WglOriginal);
            g_OpenGlHooked.store(gdiInstalled || wglInstalled);
        }
    }
    if (!g_D3D9Attempted)
    {
        D3D9HookTargets targets;
        const auto probe = FindD3D9HookTargets(targets);
        if (probe != D3D9ProbeResult::WaitingForModule)
        {
            g_D3D9Attempted = true;
            if (probe == D3D9ProbeResult::Ready)
            {
                // 先保证 Reset 可释放默认池资源，再允许 EndScene 创建 HUD 资源。
                const bool reset = Install(targets.mReset, reinterpret_cast<void*>(ResetHook), &g_ResetOriginal);
                const bool present = Install(targets.mPresent, reinterpret_cast<void*>(PresentHook), &g_PresentOriginal);
                const bool endScene = reset && Install(targets.mEndScene, reinterpret_cast<void*>(EndSceneHook), &g_EndSceneOriginal);
                g_D3D9Hooked.store(endScene);
                if (!reset || !present || !endScene)
                    g_Failure.store("d3d9_hook_installation_incomplete");
            }
            else
                g_Failure.store("d3d9_device_probe_failed");
        }
    }
    const bool installed = g_OpenGlHooked.load() || g_D3D9Hooked.load();
    auto expected = ReplayOverlayStatus::NotStarted;
    g_Status.compare_exchange_strong(expected, installed ? ReplayOverlayStatus::HookedWaitingForContext
        : ReplayOverlayStatus::WaitingForRenderer);
    if (installed)
    {
        expected = ReplayOverlayStatus::WaitingForRenderer;
        g_Status.compare_exchange_strong(expected, ReplayOverlayStatus::HookedWaitingForContext);
    }
    return installed;
}

ReplayOverlayStatus GetReplayOverlayStatus() noexcept { return g_Status.load(); }

ReplayOverlayDiagnostics GetReplayOverlayDiagnostics() noexcept
{
    return {g_Backend.load(), g_Status.load(), g_Swaps.load(), g_GlFrames.load(),
        g_EndScenes.load(), g_Presents.load(), g_Rendered.load(), g_Failures.load(),
        g_OpenGlHooked.load(), g_D3D9Hooked.load(), g_Failure.load()};
}
}

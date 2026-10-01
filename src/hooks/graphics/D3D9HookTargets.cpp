#include "D3D9HookTargets.h"

#include <Windows.h>
#include <d3d9.h>
#include <float.h>
#include <xmmintrin.h>

namespace rareplay::hooks
{
namespace
{
using CreateD3D9Function = IDirect3D9*(WINAPI*)(UINT);

class ScopedFloatingPointControl final
{
public:
    ScopedFloatingPointControl() noexcept
    {
        _controlfp_s(&mControl, 0, 0);
        mMxcsr = _mm_getcsr();
    }
    ~ScopedFloatingPointControl()
    {
        unsigned int ignored = 0;
        _controlfp_s(&ignored, mControl, _MCW_EM | _MCW_RC | _MCW_PC);
        _mm_setcsr(mMxcsr);
    }
private:
    unsigned int mControl = 0;
    unsigned int mMxcsr = 0;
};
}

D3D9ProbeResult FindD3D9HookTargets(D3D9HookTargets& outTargets) noexcept
{
    const HMODULE module = GetModuleHandleW(L"d3d9.dll");
    if (!module)
        return D3D9ProbeResult::WaitingForModule;
    const auto create = reinterpret_cast<CreateD3D9Function>(GetProcAddress(module, "Direct3DCreate9"));
    if (!create)
        return D3D9ProbeResult::Failed;

    // D3D9 创建设备可能改变 x87 控制字，游戏线程上的探测必须完整恢复计算环境。
    ScopedFloatingPointControl floatingPointControl;
    IDirect3D9* api = create(D3D_SDK_VERSION);
    if (!api)
        return D3D9ProbeResult::Failed;
    WNDCLASSW probeClass{};
    probeClass.lpfnWndProc = DefWindowProcW;
    probeClass.hInstance = GetModuleHandleW(nullptr);
    probeClass.lpszClassName = L"RAReplayPlugin.D3D9Probe";
    RegisterClassW(&probeClass);
    HWND window = CreateWindowExW(0, probeClass.lpszClassName, L"RAReplayPlugin.D3D9Probe", WS_POPUP,
        0, 0, 32, 32, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window)
    {
        api->Release();
        return D3D9ProbeResult::Failed;
    }
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferWidth = 32;
    parameters.BackBufferHeight = 32;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    HRESULT result = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        &parameters, &device);
    if (FAILED(result))
        result = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
            &parameters, &device);
    if (SUCCEEDED(result) && device)
    {
        void** methods = *reinterpret_cast<void***>(device);
        // IDirect3DDevice9 的公开 COM vtable 顺序；不依赖游戏地址或签名扫描。
        outTargets = {methods[42], methods[17], methods[16]};
        device->Release();
    }
    DestroyWindow(window);
    api->Release();
    return outTargets.mEndScene && outTargets.mPresent && outTargets.mReset
        ? D3D9ProbeResult::Ready : D3D9ProbeResult::Failed;
}
}

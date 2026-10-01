#pragma once

namespace rareplay::hooks
{
struct D3D9HookTargets
{
    void* mEndScene = nullptr;
    void* mPresent = nullptr;
    void* mReset = nullptr;
};

enum class D3D9ProbeResult { WaitingForModule, Ready, Failed };

/** 仅发现已加载 d3d9 的方法入口，不选择或修改游戏的渲染器。 */
D3D9ProbeResult FindD3D9HookTargets(D3D9HookTargets& outTargets) noexcept;
}

#pragma once

namespace rareplay::plugin
{
    /** 安装目标游戏存档链路的只读诊断钩子，不改变存档结果。 */
    bool InstallReplaySaveDiagnostics() noexcept;
}

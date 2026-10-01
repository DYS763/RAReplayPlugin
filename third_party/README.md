# 回放界面依赖

- `imgui`：用户提供的 Dear ImGui 1.89.8 WIP 快照。保留原目录；编译核心及 Win32/OpenGL3/DX9 后端，不编译旧 `imgui_hook.cpp`、toggle、FreeType 或 demo。DX9 两文件来自官方 tag v1.89.8 的 `backends/imgui_impl_dx9.cpp/.h`：https://github.com/ocornut/imgui/tree/v1.89.8/backends 。许可见 `imgui/LICENSE.txt`。
- `kiero`：保留用户提供的 OpenGL 配置快照。其单一全局 RenderType 不适合同时接入多个候选 API，因此生产 Hook 改为直接使用底层 MinHook，按真实回调确认后端。上游许可见 `kiero/LICENSE`。不调用全局 shutdown 来停用其他插件的 Hook。
- `minhook`：保留 MinHook 源码及原有许可、说明；`minhook.h` 仅转发已有头，以匹配 kiero 的相对 include。
- `yrpp`：保留构建所需的本地 SDK 快照及原有注释、声明。其代码标识指向 YRpp/Phobos 生态，但精确上游仓库、提交以及适用于此快照的许可文本尚未核实。这里不以 Phobos 项目许可代替 YRpp 许可，也不为其另行指定许可；发布与再分发条件仍需核实。

## ImGui 快照的最小构建修正

1. 自定义 GLM 转换器以 `RAREPLAY_IMGUI_GLM_COMPAT` 条件编译，本项目未启用，不增加 GLM 依赖；ImVec2 内存布局不变。
2. 恢复默认断言处理，去掉未声明 printf 的自定义断言宏。
3. 无当前上下文的检查使用断言，去掉仅用于该检查的 spdlog 依赖。
4. OpenGL 内置加载器的 GL 1.1 声明保护从错误的 `#ifdef` 修复为 `#ifndef`，让独立翻译单元获得纹理函数声明。
5. 恢复 Win32 后端每帧读取客户区大小的标准逻辑；原快照注释掉了该逻辑，导致界面尺寸为零而不绘制。
6. 新增的官方 DX9 后端按 `FramebufferScale` 设置 viewport 与 scissor；保持逻辑客户区投影，支持 cnc-ddraw 后缓冲与窗口大小不同的情况。

这些补丁不升级整套 ImGui，也不引入旧项目渲染框架。

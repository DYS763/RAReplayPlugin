# RAReplayPlugin

《尤里的复仇》对局录制与回放插件源码。此仓库包含 x86 游戏插件、离线测试以及接入现有 DTAClient 所需的独立 C# 代码；不包含完整客户端、游戏文件或已构建产物。

插件通过 Syringe 加载，目标为 SHA-256 `7cd005d263fde203d9c84548200a057a8df61d724da3c6bd1e521eeb61cd0747` 的 `gamemd.exe`（ImageBase `0x400000`）。客户端负责生成录制或播放请求、检查地图并恢复开局文件；插件负责采集和重演模拟帧、读写回放容器以及显示观看界面。C# 接入范围与接入步骤见 [integrations/csharp/README.md](integrations/csharp/README.md)。

## 构建与测试

需要 Windows、Visual Studio 2022 C++ 工作负载、Windows SDK、CMake 和 PowerShell。按 Win32/x86 与 C++23 构建：

```powershell
.\scripts\build.ps1 -Configuration Release
```

该脚本运行源码边界检查、构建及常规 CTest。插件产物位于 `build/v2/Release/RAReplayPlugin.dll`。C# 与 C++ 的文件协议贯通测试在接入文件齐备后运行：

```powershell
.\scripts\test-protocol-bridge.ps1
```

可选的 Syringe 握手测试需要调用者显式提供与上述哈希匹配的游戏可执行文件。测试在临时目录中使用该文件，不启动游戏：

```powershell
.\scripts\test-plugin-handshake.ps1 -GameExe 'D:\Games\Yuri\gamemd.exe'
```

## 范围与限制

回放容器为 `.yrrep`，未完成写入使用 `.partial`。观看界面使用 Direct3D 9 或 OpenGL 呈现回调；GDI 软件回退不绘制界面。图标读取已接入 PCX 优先、SHP 回退，新游标与图标已获本轮实机运行反馈。录制 FPS、编队记录及逐玩家黑幕尚未完整接入。当前不提供时间轴跳转、倒退或世界快照恢复。已有用户运行验证覆盖遭遇战录制与短回放；这不代表所有游戏版本、对局长度和图形后端均已验证。

第三方依赖及其来源状态见 [third_party/README.md](third_party/README.md)。本仓库未为自有源码附加许可文件；公开可读的源码与授予再分发许可是两件不同的事。

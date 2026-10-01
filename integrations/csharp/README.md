# C# 启动器接入

`RAReplay.Client` 是可独立编译的 .NET Framework 4.8 类库。三个协议类从 DTAClient 的回放模块抽取，保持原有二进制布局；类库不依赖 DTAClient、ClientCore、ClientGUI、XNA 或 Rampastring 程序集，也不包含完整客户端或 UI。公开命名空间为 `RAReplay.Client`。现有客户端若采用工程引用，须将原 `DTAClient.Domain.Replay` 引用改为该命名空间，避免同时编译两份协议实现。

录制时，宿主先按原流程写好最终 `spawn.ini` 和 `spawnmap.ini`，然后调用 `ReplayClient.PrepareRecording(gamePath, baseMapPath, mapName)`。返回的 `RecordingSession` 包含 `ReplayPath` 与 `Manifest`，并拥有写到游戏目录的 `rareplay-runtime.bin` 请求。宿主随后调用自己已有的游戏启动方法。启动失败或游戏进程真正退出时调用 `Dispose`；不要仅因启动方法返回就释放。释放会清理尚未由插件消费的本次请求，不会删除已生成的 `.yrrep`。

播放时，宿主选择 `.yrrep` 后调用 `ReplayClient.PreparePlayback(gamePath, replayPath, gameExecutablePath, resolveBaseMap)`。库校验完整容器、帧流、`gamemd.exe` 哈希及宿主解析的地图哈希，然后暂存原 `spawn.ini`、`spawnmap.ini`，写入录像保存的开局配置与播放请求。地图解析委托可使用 `manifest.FindBaseMap(gamePath)`；原有开局清单要求地图位于游戏目录内，库也会核对地图哈希。`PlaybackSession` 持有恢复责任，游戏退出或启动失败后必须 `Dispose`，包括准备前原文件不存在的情况。准备中途失败也会尝试回滚；若回滚同时失败，异常中保留两项错误。

宿主须保证准备调用与游戏启动串行，且游戏目录没有正在运行的游戏或其他活跃回放会话。每次只保留一个会话对象，并将其释放挂到宿主原有的进程退出及启动失败路径。恢复错误必须报告给调用方，并保留会话对象以便重试；启动与清理同时失败时应保留两项错误。库不订阅宿主事件，也不启动游戏。`HostExample` 是可编译的无 UI 回调示例，不会启动真实游戏。

运行离线验证：

```powershell
dotnet build integrations/csharp/RAReplay.Client/RAReplay.Client.csproj
dotnet run --project integrations/csharp/RAReplay.Client.Tests/RAReplay.Client.Tests.csproj
dotnet build integrations/csharp/HostExample/HostExample.csproj
```

测试在临时目录使用合成地图、可执行文件字节和回放容器，不包含真实地图、录像、客户端配置或游戏二进制。当前接入只覆盖既有录制与离线播放格式，不提供浏览 UI、游戏启动器或独立命令行播放入口。

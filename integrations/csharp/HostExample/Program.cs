using System;
using RAReplay.Client;

// 仅演示宿主已有的启动与退出回调；此程序不会启动游戏。
internal sealed class HostReplayAdapter
{
    private readonly Action startGameProcess;
    private IDisposable activeSession;

    public HostReplayAdapter(Action startGameProcess)
    {
        this.startGameProcess = startGameProcess;
    }

    public void OnLaunchFilesReady(string gamePath, string baseMapPath, string mapName)
    {
        if (activeSession != null) throw new InvalidOperationException("已有活跃回放会话。");
        activeSession = ReplayClient.PrepareRecording(gamePath, baseMapPath, mapName);
        StartPreparedGame();
    }

    public void OnReplaySelected(string gamePath, string replayPath, string gameExecutablePath)
    {
        if (activeSession != null) throw new InvalidOperationException("已有活跃回放会话。");
        activeSession = ReplayClient.PreparePlayback(gamePath, replayPath, gameExecutablePath,
            manifest => manifest.FindBaseMap(gamePath));
        StartPreparedGame();
    }

    private void StartPreparedGame()
    {
        try { startGameProcess(); }
        catch (Exception startError)
        {
            try { OnGameProcessExited(); }
            catch (Exception restoreError)
            {
                throw new AggregateException("启动失败，且回放清理失败。",
                    startError, restoreError);
            }
            throw;
        }
        // 启动调用返回不代表游戏已退出；保留会话供真正的退出回调清理。
    }

    public void OnGameProcessExited()
    {
        IDisposable session = activeSession;
        if (session == null) return;
        session.Dispose();
        activeSession = null;
    }
}

internal static class Program
{
    private static void Main()
    {
        HostReplayAdapter example = new HostReplayAdapter(() =>
            Console.WriteLine("模拟现有宿主的 StartGameProcess 调用"));
        Console.WriteLine("将 OnLaunchFilesReady、OnReplaySelected 和 OnGameProcessExited 接入现有宿主回调。");
    }
}

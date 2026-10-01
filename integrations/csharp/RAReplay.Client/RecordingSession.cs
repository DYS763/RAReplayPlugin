using System;

namespace RAReplay.Client
{
    public sealed class RecordingSession : IDisposable
    {
        private readonly string root;
        private readonly byte[] request;
        private readonly object disposeGate = new object();
        private bool disposed;

        internal RecordingSession(string root, string replayPath,
            ReplayLaunchManifest manifest, byte[] request)
        {
            this.root = root;
            this.request = request;
            ReplayPath = replayPath;
            Manifest = manifest;
        }

        public string ReplayPath { get; private set; }
        public ReplayLaunchManifest Manifest { get; private set; }

        // 只清理由本会话发布且尚未被插件消费的请求，不删除生成的录像。
        public void Dispose()
        {
            lock (disposeGate)
            {
                if (disposed) return;
                ReplayRuntimeRequest.DeleteIfOwned(root, request);
                disposed = true;
            }
        }
    }
}

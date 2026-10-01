using System;
using System.IO;
using System.Diagnostics;
using System.Text;

namespace RAReplay.Client
{
    public sealed class ReplayRuntimeRequest
    {
        private const uint Magic = 0x54525052;
        private const uint Version = 2;
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);
        public const uint Record = 1;
        public const uint Play = 2;
        public uint Mode { get; private set; }
        public long CreatedUnixMilliseconds { get; private set; }
        public byte[] RequestId { get; private set; }
        public string ReplayPath { get; private set; }
        public byte[] ManifestBytes { get; private set; }

        public static ReplayRuntimeRequest Create(uint mode, string path, byte[] manifest)
        {
            if ((mode != Record && mode != Play) || !System.IO.Path.IsPathRooted(path))
                throw new InvalidDataException("回放请求类型或路径无效。");
            return new ReplayRuntimeRequest
            {
                Mode = mode,
                CreatedUnixMilliseconds = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                RequestId = Guid.NewGuid().ToByteArray(),
                ReplayPath = System.IO.Path.GetFullPath(path),
                ManifestBytes = manifest ?? new byte[0]
            };
        }

        public byte[] Serialize()
        {
            byte[] pathBytes = StrictUtf8.GetBytes(ReplayPath);
            if (pathBytes.Length == 0 || pathBytes.Length > 32768 || ManifestBytes.Length > ReplayLaunchManifest.MaximumBytes)
                throw new InvalidDataException("回放请求过大。");
            using (MemoryStream output = new MemoryStream())
            using (BinaryWriter writer = new BinaryWriter(output, Encoding.UTF8))
            {
                writer.Write(Magic);
                writer.Write(Version);
                writer.Write(Mode);
                writer.Write(0u);
                writer.Write(CreatedUnixMilliseconds);
                writer.Write(RequestId);
                writer.Write((uint)pathBytes.Length);
                writer.Write((uint)ManifestBytes.Length);
                writer.Write(pathBytes);
                writer.Write(ManifestBytes);
                return output.ToArray();
            }
        }

        public static ReplayRuntimeRequest Deserialize(byte[] bytes)
        {
            if (bytes == null || bytes.Length < 48 || bytes.Length > 17 * 1024 * 1024)
                throw new InvalidDataException("回放请求长度无效。");
            using (MemoryStream input = new MemoryStream(bytes, false))
            using (BinaryReader reader = new BinaryReader(input, Encoding.UTF8))
            {
                if (reader.ReadUInt32() != Magic || reader.ReadUInt32() != Version)
                    throw new InvalidDataException("不支持的回放请求。");
                uint mode = reader.ReadUInt32();
                if ((mode != Record && mode != Play) || reader.ReadUInt32() != 0)
                    throw new InvalidDataException("回放请求类型或保留位无效。");
                long when = reader.ReadInt64();
                byte[] id = reader.ReadBytes(16);
                uint pathLength = reader.ReadUInt32();
                uint manifestLength = reader.ReadUInt32();
                if (pathLength == 0 || pathLength > 32768 || manifestLength > ReplayLaunchManifest.MaximumBytes ||
                    input.Length - input.Position != (long)pathLength + manifestLength)
                    throw new InvalidDataException("回放请求字段长度无效。");
                string path = StrictUtf8.GetString(reader.ReadBytes((int)pathLength));
                if (!System.IO.Path.IsPathRooted(path)) throw new InvalidDataException("回放请求路径无效。");
                return new ReplayRuntimeRequest
                {
                    Mode = mode,
                    CreatedUnixMilliseconds = when,
                    RequestId = id,
                    ReplayPath = path,
                    ManifestBytes = reader.ReadBytes((int)manifestLength)
                };
            }
        }

        public static void Publish(string gamePath, byte[] request)
        {
            string target = System.IO.Path.Combine(gamePath, "rareplay-runtime.bin");
            string temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try
            {
                using (FileStream output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    output.Write(request, 0, request.Length);
                    output.Flush(true);
                }
                // 不能覆盖另一启动方的请求；旧请求由其所有者清理。
                File.Move(temporary, target);
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }

        public static void DeleteIfOwned(string gamePath, byte[] request)
        {
            string target = System.IO.Path.Combine(gamePath, "rareplay-runtime.bin");
            if (File.Exists(target) && ReplayLaunchManifest.EqualsHash(ReplayLaunchManifest.Hash(File.ReadAllBytes(target)),
                ReplayLaunchManifest.Hash(request))) File.Delete(target);
        }

        public static void ClearStale(string gamePath)
        {
            string target = System.IO.Path.Combine(gamePath, "rareplay-runtime.bin");
            if (!File.Exists(target) || Process.GetProcessesByName("gamemd").Length != 0 ||
                Process.GetProcessesByName("Syringe").Length != 0) return;
            byte[] observed = File.ReadAllBytes(target);
            ReplayRuntimeRequest previous = Deserialize(observed);
            string replayRoot = ReplayLaunchManifest.Root(System.IO.Path.Combine(gamePath, "Replays"));
            if (previous.ReplayPath.StartsWith(replayRoot, StringComparison.OrdinalIgnoreCase) &&
                DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - previous.CreatedUnixMilliseconds > 5 * 60 * 1000 &&
                Process.GetProcessesByName("gamemd").Length == 0 &&
                Process.GetProcessesByName("Syringe").Length == 0)
                DeleteIfOwned(gamePath, observed);
        }
    }
}

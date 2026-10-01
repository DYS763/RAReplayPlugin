using System;
using System.IO;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;

namespace RAReplay.Client
{
    public sealed class ReplayLaunchManifest
    {
        private const uint Magic = 0x4D4C5052;
        private const uint Version = 1;
        internal const int MaximumBytes = 16 * 1024 * 1024;
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        public long RecordedAtUnixSeconds { get; private set; }
        public string BaseMapRelativePath { get; private set; }
        public string MapName { get; private set; }
        public byte[] BaseMapHash { get; private set; }
        public byte[] SpawnMapHash { get; private set; }
        public byte[] SpawnIniBytes { get; private set; }
        public byte[] SpawnMapBytes { get; private set; }

        public static ReplayLaunchManifest Create(string gamePath, string baseMapPath, string mapName)
        {
            string root = Root(gamePath);
            string basePath = Path.GetFullPath(baseMapPath);
            if (!basePath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("地图必须位于游戏目录中。");
            byte[] spawnIni = ReadBounded(Path.Combine(root, "spawn.ini"), 1024 * 1024);
            byte[] spawnMap = ReadBounded(Path.Combine(root, "spawnmap.ini"), 15 * 1024 * 1024);
            return new ReplayLaunchManifest
            {
                RecordedAtUnixSeconds = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
                BaseMapRelativePath = basePath.Substring(root.Length),
                MapName = mapName ?? string.Empty,
                BaseMapHash = HashFile(basePath),
                SpawnMapHash = Hash(spawnMap),
                SpawnIniBytes = spawnIni,
                SpawnMapBytes = spawnMap
            };
        }

        public byte[] Serialize()
        {
            using (MemoryStream output = new MemoryStream())
            using (BinaryWriter writer = new BinaryWriter(output, Encoding.UTF8))
            {
                writer.Write(Magic);
                writer.Write(Version);
                writer.Write(RecordedAtUnixSeconds);
                WriteBytes(writer, StrictUtf8.GetBytes(BaseMapRelativePath), 32768);
                WriteBytes(writer, StrictUtf8.GetBytes(MapName), 32768);
                writer.Write(BaseMapHash);
                writer.Write(SpawnMapHash);
                WriteBytes(writer, SpawnIniBytes, 1024 * 1024);
                WriteBytes(writer, SpawnMapBytes, 15 * 1024 * 1024);
                if (output.Length > MaximumBytes) throw new InvalidDataException("回放清单过大。");
                return output.ToArray();
            }
        }

        public static ReplayLaunchManifest Deserialize(byte[] data)
        {
            if (data == null || data.Length < 88 || data.Length > MaximumBytes)
                throw new InvalidDataException("回放清单长度无效。");
            using (MemoryStream input = new MemoryStream(data, false))
            using (BinaryReader reader = new BinaryReader(input, Encoding.UTF8))
            {
                if (reader.ReadUInt32() != Magic || reader.ReadUInt32() != Version)
                    throw new InvalidDataException("不支持的回放清单。");
                ReplayLaunchManifest result = new ReplayLaunchManifest
                {
                    RecordedAtUnixSeconds = reader.ReadInt64(),
                    BaseMapRelativePath = StrictUtf8.GetString(ReadBytes(reader, 32768)),
                    MapName = StrictUtf8.GetString(ReadBytes(reader, 32768)),
                    BaseMapHash = Exact(reader, 32),
                    SpawnMapHash = Exact(reader, 32),
                    SpawnIniBytes = ReadBytes(reader, 1024 * 1024),
                    SpawnMapBytes = ReadBytes(reader, 15 * 1024 * 1024)
                };
                if (input.Position != input.Length || string.IsNullOrWhiteSpace(result.BaseMapRelativePath) ||
                    !EqualsHash(Hash(result.SpawnMapBytes), result.SpawnMapHash))
                    throw new InvalidDataException("回放清单内容或地图校验无效。");
                if (result.RecordedAtUnixSeconds < -62135596800L ||
                    result.RecordedAtUnixSeconds > 253402300799L)
                    throw new InvalidDataException("回放录制时间无效。");
                return result;
            }
        }

        public string FindBaseMap(string gamePath)
        {
            string root = Root(gamePath);
            if (Path.IsPathRooted(BaseMapRelativePath)) throw new InvalidDataException("回放地图路径无效。");
            string original = Path.GetFullPath(Path.Combine(root, BaseMapRelativePath));
            if (!original.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("回放地图路径越过游戏目录。");
            if (File.Exists(original) && IsSafeChild(root, original) &&
                EqualsHash(HashFile(original), BaseMapHash)) return original;

            // 地图移动后只搜索游戏目录下的地图，哈希仍是开局身份。
            Stack<string> pending = new Stack<string>();
            pending.Push(root);
            while (pending.Count > 0)
            {
                string directory = pending.Pop();
                foreach (string child in Directory.EnumerateDirectories(directory))
                    if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) == 0) pending.Push(child);
                foreach (string candidate in Directory.EnumerateFiles(directory, "*.map"))
                    if (IsSafeChild(root, candidate) && EqualsHash(HashFile(candidate), BaseMapHash))
                        return candidate;
            }
            throw new FileNotFoundException("找不到与回放一致的原始地图。", BaseMapRelativePath);
        }

        internal static string Root(string gamePath)
        {
            return Path.GetFullPath(gamePath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        }

        private static bool IsSafeChild(string root, string path)
        {
            string full = Path.GetFullPath(path);
            if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return false;
            string current = root;
            foreach (string part in full.Substring(root.Length).Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar))
            {
                if (part.Length == 0) continue;
                current = Path.Combine(current, part);
                if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) return false;
            }
            return true;
        }

        internal static byte[] Hash(byte[] data)
        {
            using (SHA256 sha = SHA256.Create()) return sha.ComputeHash(data);
        }

        internal static byte[] HashFile(string path)
        {
            using (FileStream stream = File.OpenRead(path))
            using (SHA256 sha = SHA256.Create()) return sha.ComputeHash(stream);
        }

        internal static bool EqualsHash(byte[] left, byte[] right)
        {
            if (left == null || right == null || left.Length != right.Length) return false;
            int difference = 0;
            for (int i = 0; i < left.Length; i++) difference |= left[i] ^ right[i];
            return difference == 0;
        }

        private static byte[] ReadBounded(string path, int maximum)
        {
            FileInfo info = new FileInfo(path);
            if (info.Length > maximum) throw new InvalidDataException("启动文件过大。");
            return File.ReadAllBytes(path);
        }

        private static void WriteBytes(BinaryWriter writer, byte[] bytes, int maximum)
        {
            if (bytes == null || bytes.Length > maximum) throw new InvalidDataException("回放清单字段过大。");
            writer.Write((uint)bytes.Length);
            writer.Write(bytes);
        }

        private static byte[] ReadBytes(BinaryReader reader, int maximum)
        {
            uint length = reader.ReadUInt32();
            if (length > maximum) throw new InvalidDataException("回放清单字段过大。");
            return Exact(reader, (int)length);
        }

        private static byte[] Exact(BinaryReader reader, int length)
        {
            byte[] data = reader.ReadBytes(length);
            if (data.Length != length) throw new EndOfStreamException("回放清单被截断。");
            return data;
        }
    }
}

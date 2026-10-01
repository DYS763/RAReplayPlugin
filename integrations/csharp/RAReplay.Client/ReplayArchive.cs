using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace RAReplay.Client
{
    public sealed class ReplayArchive
    {
        private const int HeaderBytes = 96;
        private const long MaximumStreamBytes = 1024L * 1024 * 1024;
        public string Path { get; private set; }
        public ReplayLaunchManifest Manifest { get; private set; }
        public long StreamBytes { get; private set; }
        public byte[] GameExeHash { get; private set; }
        public uint StreamVersion { get; private set; }
        public bool HasPlayerInputEffects { get { return StreamVersion >= 2; } }
        private byte[] metadataHash;
        private byte[] contentHash;

        public static ReplayArchive Open(string path)
        {
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (BinaryReader reader = new BinaryReader(stream, Encoding.UTF8))
            {
                byte[] magic = Exact(reader, 8);
                if (Encoding.ASCII.GetString(magic) != "RAREPL2\0" || reader.ReadUInt32() != 2 ||
                    reader.ReadUInt32() != HeaderBytes)
                    throw new InvalidDataException("不支持的回放文件。");
                uint metadataBytes = reader.ReadUInt32();
                uint flags = reader.ReadUInt32();
                ulong streamBytes = reader.ReadUInt64();
                byte[] gameExeHash = Exact(reader, 32);
                byte[] hash = Exact(reader, 32);
                if (metadataBytes == 0 || metadataBytes > ReplayLaunchManifest.MaximumBytes || flags != 1 ||
                    streamBytes < 32 || streamBytes > MaximumStreamBytes ||
                    stream.Length != HeaderBytes + (long)metadataBytes + (long)streamBytes)
                    throw new InvalidDataException("回放长度或完成标志无效。");
                byte[] metadata = Exact(reader, (int)metadataBytes);
                if (reader.ReadUInt32() != 0x32465052)
                    throw new InvalidDataException("回放帧流头无效。");
                uint streamVersion = reader.ReadUInt32();
                if (streamVersion != 1 && streamVersion != 2)
                    throw new InvalidDataException("不支持的回放帧流版本。");
                return new ReplayArchive
                {
                    Path = System.IO.Path.GetFullPath(path),
                    Manifest = ReplayLaunchManifest.Deserialize(metadata),
                    StreamBytes = (long)streamBytes,
                    GameExeHash = gameExeHash,
                    StreamVersion = streamVersion,
                    metadataHash = ReplayLaunchManifest.Hash(metadata),
                    contentHash = hash
                };
            }
        }

        // 播放前流式核对 metadata + stream；列表浏览只解析头与清单。
        public void ValidatePayload()
        {
            ReplayArchive current = Open(Path);
            if (current.StreamBytes != StreamBytes ||
                !ReplayLaunchManifest.EqualsHash(current.contentHash, contentHash))
                throw new InvalidDataException("回放文件在校验前发生变化。");
            using (FileStream stream = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (SHA256 sha = SHA256.Create())
            {
                if (stream.Length != HeaderBytes + current.Manifest.Serialize().Length + StreamBytes)
                    throw new InvalidDataException("回放文件在校验期间发生变化。");
                stream.Position = HeaderBytes;
                long remaining = stream.Length - HeaderBytes;
                byte[] buffer = new byte[64 * 1024];
                while (remaining > 0)
                {
                    int read = stream.Read(buffer, 0, (int)Math.Min(buffer.Length, remaining));
                    if (read == 0) throw new EndOfStreamException("回放数据被截断。");
                    sha.TransformBlock(buffer, 0, read, buffer, 0);
                    remaining -= read;
                }
                sha.TransformFinalBlock(new byte[0], 0, 0);
                if (!ReplayLaunchManifest.EqualsHash(sha.Hash, contentHash))
                    throw new InvalidDataException("回放数据 SHA-256 校验失败。");
            }
            ValidateFrames();
        }

        public bool MatchesCurrentHeader()
        {
            ReplayArchive current = Open(Path);
            return current.StreamBytes == StreamBytes &&
                ReplayLaunchManifest.EqualsHash(current.contentHash, contentHash) &&
                ReplayLaunchManifest.EqualsHash(current.metadataHash, metadataHash) &&
                ReplayLaunchManifest.EqualsHash(current.GameExeHash, GameExeHash);
        }

        private void ValidateFrames()
        {
            using (FileStream stream = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (BinaryReader reader = new BinaryReader(stream, Encoding.UTF8))
            {
                long start = stream.Length - StreamBytes;
                stream.Position = start;
                uint magic = reader.ReadUInt32();
                uint streamVersion = reader.ReadUInt32();
                if (magic != 0x32465052 || streamVersion != StreamVersion ||
                    (streamVersion != 1 && streamVersion != 2) || reader.ReadUInt32() != 111)
                    throw new InvalidDataException("回放帧流头无效。");
                reader.ReadUInt32(); // gameMode
                reader.ReadUInt32(); // localHouseIndex
                if (reader.ReadUInt32() != 0) throw new InvalidDataException("回放起始帧无效。");
                reader.ReadUInt32(); // frameSendRate
                reader.ReadUInt32(); // randomSeed
                uint expected = 0;
                while (stream.Position < stream.Length)
                {
                    long frameHeaderBytes = streamVersion == 2 ? 20 : 16;
                    if (stream.Length - stream.Position < frameHeaderBytes)
                        throw new InvalidDataException("回放帧记录被截断。");
                    uint frame = reader.ReadUInt32();
                    reader.ReadUInt32(); // checksumBeforeDispatch
                    uint count = reader.ReadUInt32();
                    uint flags = reader.ReadUInt32();
                    uint effectCount = streamVersion == 2 ? reader.ReadUInt32() : 0;
                    if (frame != expected || count > 16384 || (flags & ~1u) != 0 ||
                        effectCount > 4096 ||
                        stream.Length - stream.Position < count * 111L + effectCount * 40L)
                        throw new InvalidDataException("回放帧顺序、事件数量或标志无效。");
                    stream.Position += count * 111L;
                    for (uint i = 0; i < effectCount; i++)
                    {
                        uint kind = reader.ReadUInt32();
                        reader.ReadUInt32(); // animationTypeIndex
                        reader.ReadInt32(); // x
                        reader.ReadInt32(); // y
                        reader.ReadInt32(); // z
                        reader.ReadInt32(); // loopDelay
                        reader.ReadInt32(); // loopCount
                        reader.ReadUInt32(); // flags
                        reader.ReadInt32(); // forceZAdjust
                        uint reverse = reader.ReadUInt32();
                        if (kind != 1 || reverse > 1)
                            throw new InvalidDataException("回放玩家输入效果字段无效。");
                    }
                    expected++;
                }
            }
        }

        private static byte[] Exact(BinaryReader reader, int length)
        {
            byte[] data = reader.ReadBytes(length);
            if (data.Length != length) throw new EndOfStreamException("回放文件被截断。");
            return data;
        }
    }
}

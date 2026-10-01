using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using RAReplay.Client;

internal static class Program
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static byte[] Hash(byte[] bytes)
    {
        using (SHA256 sha = SHA256.Create()) return sha.ComputeHash(bytes);
    }

    private static void WriteArchive(string path, byte[] exe, byte[] metadata)
    {
        byte[] stream;
        using (MemoryStream output = new MemoryStream())
        using (BinaryWriter writer = new BinaryWriter(output))
        {
            writer.Write(0x32465052u);
            writer.Write(2u);
            writer.Write(111u);
            writer.Write(3u);
            writer.Write(1u);
            writer.Write(0u);
            writer.Write(6u);
            writer.Write(0x12345678u);
            stream = output.ToArray();
        }
        using (MemoryStream output = new MemoryStream())
        using (BinaryWriter writer = new BinaryWriter(output))
        {
            writer.Write(Encoding.ASCII.GetBytes("RAREPL2\0"));
            writer.Write(2u);
            writer.Write(96u);
            writer.Write((uint)metadata.Length);
            writer.Write(1u);
            writer.Write((ulong)stream.Length);
            writer.Write(Hash(exe));
            writer.Write(Hash(metadata.Concat(stream).ToArray()));
            writer.Write(metadata);
            writer.Write(stream);
            File.WriteAllBytes(path, output.ToArray());
        }
    }

    private static void Run(string root)
    {
        Directory.CreateDirectory(root);
        byte[] map = Encoding.ASCII.GetBytes("synthetic map");
        byte[] exe = Encoding.ASCII.GetBytes("synthetic exe");
        byte[] ini = Encoding.ASCII.GetBytes("[Settings]\nSeed=42\nPort=1234\n[Other1]\nIp=10.0.0.1\n");
        byte[] spawnMap = Encoding.ASCII.GetBytes("[Map]\nWidth=64\n");
        string mapPath = Path.Combine(root, "base.map");
        string exePath = Path.Combine(root, "gamemd.exe");
        string iniPath = Path.Combine(root, "spawn.ini");
        string spawnMapPath = Path.Combine(root, "spawnmap.ini");
        string marker = Path.Combine(root, "rareplay-runtime.bin");
        File.WriteAllBytes(mapPath, map);
        File.WriteAllBytes(exePath, exe);
        File.WriteAllBytes(Path.Combine(root, "RAReplayPlugin.dll"), new byte[0]);
        File.WriteAllBytes(iniPath, ini);
        File.WriteAllBytes(spawnMapPath, spawnMap);

        ReplayLaunchManifest manifest = ReplayLaunchManifest.Create(root, mapPath, "测试地图");
        byte[] metadata = manifest.Serialize();
        Check(ReplayLaunchManifest.Deserialize(metadata).SpawnIniBytes.SequenceEqual(ini), "manifest bytes");
        string archivePath = Path.Combine(root, "synthetic.yrrep");
        WriteArchive(archivePath, exe, metadata);
        ReplayArchive archive = ReplayArchive.Open(archivePath);
        archive.ValidatePayload();
        Check(archive.Manifest.MapName == "测试地图", "archive metadata");

        using (RecordingSession recording = ReplayClient.PrepareRecording(root, mapPath, "测试地图"))
        {
            ReplayRuntimeRequest request = ReplayRuntimeRequest.Deserialize(File.ReadAllBytes(marker));
            Check(request.Mode == ReplayRuntimeRequest.Record &&
                request.ReplayPath == recording.ReplayPath &&
                request.ManifestBytes.SequenceEqual(recording.Manifest.Serialize()), "record request");
        }
        Check(!File.Exists(marker), "record cleanup");

        using (PlaybackSession playback = ReplayClient.PreparePlayback(
            root, archivePath, exePath, loaded => loaded.FindBaseMap(root)))
        {
            ReplayRuntimeRequest request = ReplayRuntimeRequest.Deserialize(File.ReadAllBytes(marker));
            Check(request.Mode == ReplayRuntimeRequest.Play && request.ManifestBytes.Length == 0,
                "play request");
            Check(!File.ReadAllBytes(iniPath).SequenceEqual(ini), "playback endpoints");
            Check(File.ReadAllBytes(spawnMapPath).SequenceEqual(spawnMap), "playback spawn map");
        }
        Check(!File.Exists(marker) && File.ReadAllBytes(iniPath).SequenceEqual(ini) &&
            File.ReadAllBytes(spawnMapPath).SequenceEqual(spawnMap), "dispose restores originals");

        File.Delete(iniPath);
        File.Delete(spawnMapPath);
        using (PlaybackSession playback = ReplayClient.PreparePlayback(
            root, archivePath, exePath, loaded => loaded.FindBaseMap(root)))
        {
            Check(File.Exists(iniPath) && File.Exists(spawnMapPath), "prepare missing files");
        }
        Check(!File.Exists(iniPath) && !File.Exists(spawnMapPath), "dispose restores absence");

        File.WriteAllBytes(iniPath, ini);
        File.WriteAllBytes(spawnMapPath, spawnMap);
        byte[] occupied = ReplayRuntimeRequest.Create(
            ReplayRuntimeRequest.Play, archivePath, new byte[0]).Serialize();
        File.WriteAllBytes(marker, occupied);
        bool rejected = false;
        try
        {
            ReplayClient.PreparePlayback(root, archivePath, exePath, loaded => loaded.FindBaseMap(root));
        }
        catch (IOException) { rejected = true; }
        Check(rejected, "occupied request must reject preparation");
        Check(File.ReadAllBytes(marker).SequenceEqual(occupied) &&
            File.ReadAllBytes(iniPath).SequenceEqual(ini) &&
            File.ReadAllBytes(spawnMapPath).SequenceEqual(spawnMap), "prepare failure rollback");
    }

    private static int Main()
    {
        string root = Path.Combine(Path.GetTempPath(), "RAReplayClientTests-" + Guid.NewGuid().ToString("N"));
        try
        {
            Run(root);
            Console.WriteLine("RAReplay.Client synthetic tests passed");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, true);
        }
    }
}

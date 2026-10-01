using System;
using System.IO;
using System.Linq;
using System.Text;
using RAReplay.Client;

internal static class ProtocolBridge
{
    private static readonly byte[] MapBytes = { 0x4d, 0x41, 0x50, 0x00, 0x80, 0xff };
    private static readonly byte[] SpawnIniBytes = Encoding.UTF8.GetBytes(
        "[Settings]\nSeed=305419896\n[Multi1]\nSide=2\nColor=3\nStart=4\nTeam=1\n");
    private static readonly byte[] SpawnMapBytes = Encoding.UTF8.GetBytes(
        "[Map]\nWidth=64\nHeight=64\n[Waypoints]\n0=100100\n");

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidDataException(message);
    }

    private static void Produce(string directory)
    {
        string game = Path.Combine(directory, "game");
        Directory.CreateDirectory(game);
        string mapPath = Path.Combine(game, "base.map");
        File.WriteAllBytes(mapPath, MapBytes);
        File.WriteAllBytes(Path.Combine(game, "spawn.ini"), SpawnIniBytes);
        File.WriteAllBytes(Path.Combine(game, "spawnmap.ini"), SpawnMapBytes);

        ReplayLaunchManifest manifest = ReplayLaunchManifest.Create(game, mapPath, "测试地图");
        byte[] serialized = manifest.Serialize();
        string replayPath = Path.Combine(directory, "bridge.yrrep");
        ReplayRuntimeRequest request = ReplayRuntimeRequest.Create(
            ReplayRuntimeRequest.Record, replayPath, serialized);
        File.WriteAllBytes(Path.Combine(directory, "manifest.bin"), serialized);
        File.WriteAllBytes(Path.Combine(directory, "request.bin"), request.Serialize());
        Console.WriteLine("C# manifest and request produced");
    }

    private static void Verify(string directory)
    {
        string game = Path.Combine(directory, "game");
        string replayPath = Path.Combine(directory, "bridge.yrrep");
        ReplayArchive archive = ReplayArchive.Open(replayPath);
        archive.ValidatePayload();
        Require(archive.StreamVersion == 2 && archive.HasPlayerInputEffects,
            "input effect stream version was not exposed");
        ReplayLaunchManifest manifest = archive.Manifest;
        Require(manifest.MapName == "测试地图" &&
            manifest.BaseMapRelativePath == "base.map" &&
            manifest.FindBaseMap(game) == Path.Combine(game, "base.map"),
            "map identity was not preserved");
        Require(manifest.SpawnIniBytes.SequenceEqual(SpawnIniBytes) &&
            manifest.SpawnMapBytes.SequenceEqual(SpawnMapBytes) &&
            manifest.BaseMapHash.SequenceEqual(ReplayLaunchManifest.Hash(MapBytes)) &&
            manifest.SpawnMapHash.SequenceEqual(ReplayLaunchManifest.Hash(SpawnMapBytes)),
            "player configuration or map bytes changed");
        Require(archive.StreamBytes == 32 + 20 + 20 + 111 + 40,
            "C++ stream length differs from the two-frame sample");
        Require(archive.GameExeHash.SequenceEqual(Enumerable.Range(1, 32).Select(x => (byte)x)),
            "target hash changed across the archive boundary");

        using (FileStream input = File.OpenRead(replayPath))
        using (BinaryReader reader = new BinaryReader(input))
        {
            input.Position = input.Length - archive.StreamBytes;
            Require(reader.ReadUInt32() == 0x32465052 && reader.ReadUInt32() == 2 &&
                reader.ReadUInt32() == 111 && reader.ReadUInt32() == 3 &&
                reader.ReadUInt32() == 1 && reader.ReadUInt32() == 0 &&
                reader.ReadUInt32() == 6 && reader.ReadUInt32() == 0x12345678,
                "C++ session fields including random seed changed");
            Require(reader.ReadUInt32() == 0 && reader.ReadUInt32() == 0 &&
                reader.ReadUInt32() == 0 && reader.ReadUInt32() == 0 &&
                reader.ReadUInt32() == 0,
                "empty frame changed");
            Require(reader.ReadUInt32() == 1 && reader.ReadUInt32() == 0xdeadbeef &&
                reader.ReadUInt32() == 1 && reader.ReadUInt32() == 1 &&
                reader.ReadUInt32() == 1,
                "event frame header changed");
            byte[] eventBytes = reader.ReadBytes(111);
            Require(eventBytes.Length == 111 &&
                eventBytes.SequenceEqual(Enumerable.Range(0, 111).Select(x => (byte)x)) &&
                reader.ReadUInt32() == 1 && reader.ReadUInt32() == 17 &&
                reader.ReadInt32() == -23 && reader.ReadInt32() == 45 &&
                reader.ReadInt32() == -67 && reader.ReadInt32() == 8 &&
                reader.ReadInt32() == 2 && reader.ReadUInt32() == 0x12345678 &&
                reader.ReadInt32() == -9 && reader.ReadUInt32() == 1 &&
                input.Position == input.Length,
                "111-byte command order or size changed");
        }
        Console.WriteLine("C# archive open, payload validation and manifest assertions passed");
    }

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length != 2) throw new ArgumentException("mode and directory required");
            if (args[0] == "produce") Produce(args[1]);
            else if (args[0] == "verify") Verify(args[1]);
            else throw new ArgumentException("unknown mode");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

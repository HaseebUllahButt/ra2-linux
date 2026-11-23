// Verification harness only. Compiles the unmodified local GPLv3-or-later
// OpenRA sources by reference; it is not a dependency of the C++ reader.
using System;
using System.IO;
using OpenRA.Mods.Cnc.FileFormats;

static class Program
{
    static int Main(string[] args)
    {
        using var input = File.OpenRead(args[0]);
        input.Position = 4;
        var wrapped = new byte[80];
        input.ReadExactly(wrapped);
        var key = new BlowfishKeyProvider().DecryptKey(wrapped);
        var words = new uint[2];
        using var reader = new BinaryReader(input);
        words[0] = reader.ReadUInt32();
        words[1] = reader.ReadUInt32();
        var header = new Blowfish(key).Decrypt(words);
        Console.WriteLine(Convert.ToHexString(key).ToLowerInvariant() + " " + (header[0] & 0xffff));
        return 0;
    }
}

using System.Text;
using System.Text.Json;
using Randomizer.SuperMetroid;

/// <summary>Seeds through the real Randomizer.GenerateSeed: placement per location and every seed patch, for the
/// filler and patch checks of core/smgen. The two GUID fields (0x1C4F10, 0x1C4F30) come from Guid.NewGuid and are
/// left out of the patch dump.</summary>
static class SmPlacement
{
    class Case { public string Seed { get; set; } public string Logic { get; set; } public string Placement { get; set; } public bool Race { get; set; } }

    static readonly int[] GuidFields = { 0x1C4F10, 0x1C4F30 };

    static Dictionary<string, string> Options(Case c) => new() {
        ["logic"] = c.Logic ?? "tournament", ["placement"] = c.Placement ?? "split", ["race"] = c.Race ? "true" : "false",
        ["gamemode"] = "normal", ["goal"] = "defeatmb",
    };

    public static int Batch(string[] args)
    {
        var jopt = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        var sb = new StringBuilder();
        var sm = new Randomizer.SuperMetroid.Randomizer();
        foreach (var line in File.ReadLines(args[1]).Where(l => l.Trim().Length > 0))
        {
            var c = JsonSerializer.Deserialize<Case>(line, jopt) ?? new Case();
            sb.Append("case\n");
            try
            {
                var data = sm.GenerateSeed(Options(c), c.Seed ?? "1", CancellationToken.None);
                var w = data.Worlds[0];
                sb.Append($"seed {data.Seed}\nlocs");
                foreach (var l in w.Locations) sb.Append($" {l.LocationId}:{l.ItemId}");
                sb.Append('\n');
                foreach (var p in w.Patches.Where(p => !GuidFields.Contains(p.Key)).OrderBy(p => p.Key))
                    sb.Append($"patch {p.Key:X6} {Convert.ToHexString(p.Value)}\n");
            }
            catch (Exception e) { sb.Append("error\t").Append(e.Message).Append('\n'); }
        }
        File.WriteAllText(args[2], sb.ToString());
        return 0;
    }

    public static int Rom(string[] args)
    {
        var rom = new byte[0x400000];
        var sm = File.ReadAllBytes(args[1]);
        Array.Copy(sm, rom, Math.Min(sm.Length, rom.Length));
        ApplyIps(rom, File.ReadAllBytes(args[2]));
        var c = new Case { Seed = args[3], Logic = args[4], Placement = args[5], Race = args[6] == "true" };
        var data = new Randomizer.SuperMetroid.Randomizer().GenerateSeed(Options(c), c.Seed, CancellationToken.None);
        foreach (var (addr, bytes) in data.Worlds[0].Patches)
            Array.Copy(GuidFields.Contains(addr) ? new byte[bytes.Length] : bytes, 0, rom, addr, bytes.Length);
        File.WriteAllBytes(args[7], rom);
        return 0;
    }

    static void ApplyIps(byte[] rom, byte[] ips)
    {
        var i = 5;
        while (i + 3 <= ips.Length && !(ips[i] == 'E' && ips[i + 1] == 'O' && ips[i + 2] == 'F'))
        {
            var off = ips[i] << 16 | ips[i + 1] << 8 | ips[i + 2];
            var len = ips[i + 3] << 8 | ips[i + 4];
            i += 5;
            if (len > 0) { Array.Copy(ips, i, rom, off, len); i += len; }
            else { Array.Fill(rom, ips[i + 2], off, ips[i] << 8 | ips[i + 1]); i += 3; }
        }
    }
}

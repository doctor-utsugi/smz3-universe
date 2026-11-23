using Microsoft.Extensions.DependencyInjection;
using TrackerCouncil.Smz3.Data.Interfaces;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.WorldData;
using TrackerCouncil.Smz3.Shared.Enums;

/// <summary>What the app can ask for. Anything left null keeps the tracker's default.</summary>
public class RunSettings
{
    public string? Seed { get; set; }
    public string? Keysanity { get; set; }
    public string? ItemPlacement { get; set; }
    public string? GameMode { get; set; }
    public string? TourianBossDoor { get; set; }
    public string? PyramidHole { get; set; }
    public int? GanonCrystals { get; set; }
    public int? GanonsTowerCrystals { get; set; }
    public int? TourianBosses { get; set; }
    public Dictionary<string, bool>? Logic { get; set; }
    public int? SpazersInPool { get; set; }
    public int? SpazersRequired { get; set; }
    public string? EtecoonsJingle { get; set; }
    /// <summary>Plando text overrides (PlandoTextConfig property name to text); only used by the plando commands.</summary>
    public Dictionary<string, string>? PlandoText { get; set; }
    /// <summary>Start-with / early item choices: option group name (ItemSettingOptions.yml) to the selected option's index.</summary>
    public Dictionary<string, int>? ItemOptions { get; set; }
}

public class Generator(IServiceProvider services, string smPath, string z3Path)
{
    private readonly SemaphoreSlim _lock = new(1);

    public async Task<(byte[]? Rom, string Seed, string? Error)> GenerateAsync(RunSettings settings)
    {
        // The generator keeps state in singletons (database context), so one seed at a time.
        await _lock.WaitAsync();
        var outDir = Path.Combine(Path.GetTempPath(), "smz3-gen-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(outDir);
            var options = new RandomizerOptions();
            options.GeneralOptions.SMRomPath = smPath;
            options.GeneralOptions.Z3RomPath = z3Path;
            options.GeneralOptions.RomOutputPath = outDir;
            Apply(options, settings);

            var result = await services.GetRequiredService<IRomGenerationService>().GenerateRandomRomAsync(options);
            var file = result.Rom == null ? "" : Path.Combine(outDir, result.Rom.RomPath);
            if (result.Rom == null || !File.Exists(file)) return (null, "", result.GenerationError);
            return (File.ReadAllBytes(file), result.Rom.Seed, result.GenerationError);
        }
        finally
        {
            try { Directory.Delete(outDir, true); } catch { /* best effort */ }
            _lock.Release();
        }
    }

    private readonly Dictionary<string, World> _worlds = new();

    /// <summary>Ids of the locations that are in logic for a run with these settings and this inventory (ItemType names, repeated for counts).</summary>
    public List<string> Reachable(RunSettings settings, IEnumerable<string> itemNames)
    {
        var key = System.Text.Json.JsonSerializer.Serialize(settings);
        lock (_worlds)
        {
            if (!_worlds.TryGetValue(key, out var world))
            {
                var options = new RandomizerOptions();
                Apply(options, settings);
                world = new World(options.ToConfig(), "", 0, "");
                _worlds[key] = world;
            }

            var items = itemNames.Select(n => Enum.TryParse<ItemType>(n, out var t) ? t : ItemType.Nothing)
                .Where(t => t != ItemType.Nothing).ToList();
            var progression = new Progression(items, Array.Empty<RewardType>(), Array.Empty<BossType>());
            return world.Locations.Where(l => l.IsAvailable(progression)).Select(l => l.Id.ToString()).ToList();
        }
    }

    /// <summary>Runs the real filler only (no ROM) and dumps what ended up where, for the C port's tests.</summary>
    public string Placement(RunSettings settings, string seed)
    {
        var options = new RandomizerOptions();
        Apply(options, settings);
        var config = options.ToConfig();
        var randomizer = services.GetRequiredService<TrackerCouncil.Smz3.SeedGenerator.Generation.Smz3Randomizer>();
        var data = randomizer.GenerateSeed(config, seed);
        var world = data.WorldGenerationData.LocalWorld.World;
        var sb = new System.Text.StringBuilder();
        sb.Append($"seed {data.PrimaryConfig.Seed}\n");
        sb.Append("placement");
        foreach (var l in world.Locations) sb.Append($" {(int)l.Id}:{(int)l.Item.Type}");
        sb.Append("\nrewards");
        foreach (var r in world.Regions.OfType<TrackerCouncil.Smz3.Data.WorldData.Regions.IHasReward>()) sb.Append($" {world.Regions.ToList().IndexOf((TrackerCouncil.Smz3.Data.WorldData.Regions.Region)r)}:{(int)r.RewardType}");
        sb.Append("\nprereq");
        foreach (var r in world.Regions.OfType<TrackerCouncil.Smz3.Data.WorldData.Regions.IHasPrerequisite>()) sb.Append($" {world.Regions.ToList().IndexOf((TrackerCouncil.Smz3.Data.WorldData.Regions.Region)r)}:{(int)r.RequiredItem}");
        sb.Append('\n');
        sb.Append("names\n");
        foreach (var l in world.Locations) sb.Append($"N\t{(int)l.Id}\t{l}\n");
        sb.Append("hints\n");
        foreach (var h in world.HintTiles)
            sb.Append($"H\t{h.HintTileCode}\t{h.Type}\t{string.Join(",", (h.Locations ?? []).Select(x => (int)x))}\t{h.Usefulness?.ToString() ?? "-"}\t{(h.MedallionType != null ? ((int)h.MedallionType).ToString() : "-")}\n");
        sb.Append("patches\n");
        foreach (var p in TrackerCouncil.Smz3.SeedGenerator.Generation.PatcherService.Trace) sb.Append($"P\t{p}\n");
        TrackerCouncil.Smz3.SeedGenerator.Generation.PatcherService.Trace.Clear();
        sb.Append("trace\n");
        foreach (var t in PlacementTrace.Lines) sb.Append($"T\t{t}\n");
        return sb.ToString();
    }

    /// <summary>Generates a seed, exports its plando config (YAML) to yamlPath, plays that config back through the real plandomizer and dumps the result, for the C plando tests.</summary>
    public string Plando(RunSettings settings, string seed, string yamlPath)
    {
        var options = new RandomizerOptions();
        Apply(options, settings);
        var config = options.ToConfig();
        var randomizer = services.GetRequiredService<TrackerCouncil.Smz3.SeedGenerator.Generation.Smz3Randomizer>();
        var data = randomizer.GenerateSeed(config, seed);
        TrackerCouncil.Smz3.SeedGenerator.Generation.PatcherService.Trace.Clear();
        var plando = new PlandoConfig(data.WorldGenerationData.LocalWorld.World);
        if (settings.PlandoText is { Count: > 0 })
        {
            var text = new PlandoTextConfig();
            foreach (var (name, value) in settings.PlandoText) typeof(PlandoTextConfig).GetProperty(name)!.SetValue(text, value);
            plando.Text = text;
        }
        File.WriteAllText(yamlPath, new YamlDotNet.Serialization.Serializer().Serialize(plando));
        var loaded = services.GetRequiredService<TrackerCouncil.Smz3.SeedGenerator.Contracts.IPlandoConfigLoader>().LoadAsync(yamlPath).GetAwaiter().GetResult();
        var pdata = services.GetRequiredService<IRomGenerationService>().GeneratePlandoSeed(options, loaded);
        var world = pdata.WorldGenerationData.LocalWorld.World;
        var sb = new System.Text.StringBuilder();
        sb.Append($"seed {pdata.PrimaryConfig.Seed}\nplacement");
        foreach (var l in world.Locations) sb.Append($" {(int)l.Id}:{(int)l.Item.Type}");
        sb.Append("\nrewards");
        foreach (var r in world.Regions.OfType<TrackerCouncil.Smz3.Data.WorldData.Regions.IHasReward>()) sb.Append($" {world.Regions.ToList().IndexOf((TrackerCouncil.Smz3.Data.WorldData.Regions.Region)r)}:{(int)r.RewardType}");
        sb.Append("\nprereq");
        foreach (var r in world.Regions.OfType<TrackerCouncil.Smz3.Data.WorldData.Regions.IHasPrerequisite>()) sb.Append($" {world.Regions.ToList().IndexOf((TrackerCouncil.Smz3.Data.WorldData.Regions.Region)r)}:{(int)r.RequiredItem}");
        sb.Append("\npatches\n");
        foreach (var p in TrackerCouncil.Smz3.SeedGenerator.Generation.PatcherService.Trace) sb.Append($"P\t{p}\n");
        TrackerCouncil.Smz3.SeedGenerator.Generation.PatcherService.Trace.Clear();
        return sb.ToString();
    }

    public byte[] CombinedBase()
    {
        using var sm = File.OpenRead(smPath);
        using var z3 = File.OpenRead(z3Path);
        return TrackerCouncil.Smz3.SeedGenerator.FileData.Rom.CombineSMZ3Rom(sm, z3);
    }

    private static void Apply(RandomizerOptions options, RunSettings s)
    {
        var seedOptions = options.SeedOptions;
        var mode = seedOptions.GameModeOptions;
        if (!string.IsNullOrWhiteSpace(s.Seed)) seedOptions.Seed = s.Seed;
        if (Enum.TryParse<KeysanityMode>(s.Keysanity, out var key)) mode.KeysanityMode = key;
        if (Enum.TryParse<ItemPlacementRule>(s.ItemPlacement, out var rule)) seedOptions.ItemPlacementRule = rule;
        if (Enum.TryParse<GameModeType>(s.GameMode, out var type)) mode.SelectedGameModeType = type;
        if (Enum.TryParse<TourianBossDoor>(s.TourianBossDoor, out var door)) mode.TourianBossDoor = door;
        if (Enum.TryParse<PyramidHole>(s.PyramidHole, out var hole)) mode.PyramidHole = hole;
        if (s.GanonCrystals is { } gc) mode.GanonCrystalCount = gc;
        if (s.GanonsTowerCrystals is { } gt) mode.GanonsTowerCrystalCount = gt;
        if (s.TourianBosses is { } tb) mode.TourianBossCount = tb;
        if (s.SpazersInPool is { } sp) mode.SpazersInPool = sp;
        if (s.SpazersRequired is { } sr) mode.SpazersRequired = sr;
        if (Enum.TryParse<EtecoonsJingle>(s.EtecoonsJingle, out var jingle)) options.PatchOptions.EtecoonsJingle = jingle;
        foreach (var (name, value) in s.ItemOptions ?? new()) seedOptions.ItemOptions[name] = value;

        foreach (var (name, value) in s.Logic ?? new())
        {
            var prop = options.LogicConfig.GetType().GetProperty(name);
            if (prop is { PropertyType.Name: "Boolean", CanWrite: true }) prop.SetValue(options.LogicConfig, value);
        }
    }
}

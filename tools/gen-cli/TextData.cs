using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using TrackerCouncil.Smz3.Data.Configuration.ConfigFiles;
using TrackerCouncil.Smz3.Data.Configuration.ConfigTypes;
using TrackerCouncil.Smz3.Data.Interfaces;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.Services;
using TrackerCouncil.Smz3.Data.WorldData.Regions;
using TrackerCouncil.Smz3.Shared.Enums;

/// <summary>Dumps the strings the C text writer needs (game lines, item names and pedestal hints, region names).</summary>
public static class TextData
{
    static List<string> All(SchrodingersString? s) =>
        s == null ? new List<string>() : s.Where(p => p.Weight > 0).Select(p => p.Text).ToList();

    public static string Dump(IServiceProvider services)
    {
        var meta = services.GetRequiredService<IMetadataService>();
        var randomizer = services.GetRequiredService<TrackerCouncil.Smz3.SeedGenerator.Generation.Smz3Randomizer>();
        var options = new RandomizerOptions();
        var config = options.ToConfig();
        var data = randomizer.GenerateSeed(config, "1");
        var world = data.WorldGenerationData.LocalWorld.World;

        var lines = new Dictionary<string, List<string>>();
        foreach (var p in typeof(GameLinesConfig).GetProperties().Where(p => p.PropertyType == typeof(SchrodingersString)))
            lines[p.Name] = All((SchrodingersString?)p.GetValue(meta.GameLines));
        foreach (var p in typeof(GameModesConfig).GetProperties().Where(p => p.PropertyType == typeof(SchrodingersString)))
            lines[p.Name] = All((SchrodingersString?)p.GetValue(meta.GameModes));

        var items = new Dictionary<int, object>();
        foreach (var type in Enum.GetValues<ItemType>())
        {
            var d = meta.Items.FirstOrDefault(x => x.InternalItemType == type);
            if (d == null) continue;
            var articled = All(d.ArticledName);
            if (articled.Count == 0)
                articled = All(d.Name).Select(n => string.Join(" ", d.Article ?? (d.Multiple || d.HasStages ? "a" : "the"), n)).ToList();
            items[(int)type] = new { articled, pedestal = All(d.PedestalHints), name = All(d.Name) };
        }

        var regions = new List<object>();
        var list = world.Regions.ToList();
        for (var i = 0; i < list.Count; i++)
        {
            var r = list[i];
            var info = meta.Regions.FirstOrDefault(x => x.Type == r.GetType());
            regions.Add(new { index = i, name = r.Name, area = r.Area, names = All(info?.Name) });
        }
        return JsonSerializer.Serialize(new { lines, items, regions }, new JsonSerializerOptions { WriteIndented = true });
    }
}

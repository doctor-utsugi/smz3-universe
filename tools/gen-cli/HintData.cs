using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using TrackerCouncil.Smz3.Data.Configuration.ConfigTypes;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.Services;
using TrackerCouncil.Smz3.Data.WorldData;
using TrackerCouncil.Smz3.Data.WorldData.Regions;
using TrackerCouncil.Smz3.SeedGenerator.Contracts;
using TrackerCouncil.Smz3.Shared.Models;

/// <summary>Dumps the world independent part of the hint pool and the names the hint texts use, for the C port.</summary>
public static class HintData
{
    static List<string> All(SchrodingersString? s) =>
        s == null ? new List<string>() : s.Where(p => p.Weight > 0).Select(p => p.Text).ToList();

    public static string Dump(IServiceProvider services)
    {
        var meta = services.GetRequiredService<IMetadataService>();
        var randomizer = services.GetRequiredService<TrackerCouncil.Smz3.SeedGenerator.Generation.Smz3Randomizer>();
        var config = new RandomizerOptions().ToConfig();
        var data = randomizer.GenerateSeed(config, "1");
        var world = data.WorldGenerationData.LocalWorld.World;
        var worlds = new List<World> { world };
        var service = services.GetRequiredService<IGameHintService>();
        var t = service.GetType();
        const BindingFlags f = BindingFlags.NonPublic | BindingFlags.Instance;
        var goal = new List<Location>();

        object Pool(string method, params object[] args)
        {
            var hints = (System.Collections.IEnumerable)t.GetMethod(method, f)!.Invoke(service, args)!;
            var list = new List<object>();
            foreach (var h in hints)
            {
                var tile = (PlayerHintTile)((Delegate)h).DynamicInvoke()!;
                list.Add(new
                {
                    type = tile.Type.ToString(), key = tile.LocationKey,
                    locations = (tile.Locations ?? []).Select(x => (int)x).ToList(),
                    medallionRegion = tile.MedallionType != null ? tile.LocationKey : null,
                    group = tile.Type == TrackerCouncil.Smz3.Shared.Enums.HintTileType.LocationGroup
                });
            }
            return list;
        }

        var regionList = world.Regions.ToList();
        var locations = world.Locations.Select(l => new
        {
            id = (int)l.Id, region = regionList.IndexOf(l.Region), room = l.Room?.Name,
            names = All(meta.Location(l.Id).Name)
        }).ToList();
        var rooms = world.Locations.Where(l => l.Room != null).Select(l => l.Room!).Distinct()
            .Select(r => new { name = r.Name, names = All(meta.Room(r)?.Name) }).ToList();

        object Dungeon(Region r) => new
        {
            region = regionList.IndexOf(r), key = r.Name,
            locations = r.Locations.Where(x => x.Type != LocationType.NotInDungeon).Select(x => (int)x.Id).ToList()
        };
        var rewardRegions = world.RewardRegions.Cast<Region>().OfType<IHasTreasure>().Select(x => Dungeon((Region)x)).ToList();

        return JsonSerializer.Serialize(new
        {
            rewardRegions, fixedDungeons = new[] { Dungeon(world.HyruleCastle), Dungeon(world.GanonsTower) },
            tiles = meta.HintTiles.HintTiles!.Select(x => x.HintTileKey).ToList(),
            locationHints = Pool("GetLocationHints", world, worlds, goal),
            medallions = Pool("GetMedallionHints", world),
            locations, rooms
        }, new JsonSerializerOptions { WriteIndented = true });
    }
}

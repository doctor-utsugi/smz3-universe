using System.Text;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.WorldData;
using TrackerCouncil.Smz3.Data.WorldData.Regions;
using TrackerCouncil.Smz3.Shared.Enums;

/// <summary>
/// Test vectors for the C port of the world: random logic settings, inventories, reward assignments and key placements,
/// with the list of locations the C# says are in logic. Used by core/gen/test_world.c.
/// </summary>
public static class WorldVectors
{
    public static int Run(string[] args)
    {
        var count = int.Parse(args[1]);
        var rng = new Random(int.Parse(args[2]));
        var path = args[3];
        var sb = new StringBuilder();
        var itemTypes = Enum.GetValues<ItemType>().Distinct().Where(t => t != ItemType.Nothing).ToList();
        var rewardTypes = Enum.GetValues<RewardType>().Distinct().ToList();
        var bossTypes = Enum.GetValues<BossType>().Distinct().ToList();
        var placeable = new[] { ItemType.KeyGT, ItemType.BigKeyGT, ItemType.KeyIP, ItemType.BigKeyIP, ItemType.KeyPD, ItemType.BigKeyPD };

        for (var n = 0; n < count; n++)
        {
            var options = new RandomizerOptions();
            var mode = options.SeedOptions.GameModeOptions;
            mode.KeysanityMode = rng.Next(4) switch { 0 => KeysanityMode.None, 1 => KeysanityMode.Zelda, 2 => KeysanityMode.SuperMetroid, _ => KeysanityMode.Both };
            mode.GanonCrystalCount = rng.Next(0, 8);
            mode.GanonsTowerCrystalCount = rng.Next(0, 8);
            mode.TourianBossCount = rng.Next(0, 5);
            foreach (var p in options.LogicConfig.GetType().GetProperties().Where(p => p.CanWrite && (p.PropertyType == typeof(bool) || p.PropertyType.IsEnum)))
            {
                if (p.PropertyType == typeof(bool)) p.SetValue(options.LogicConfig, rng.Next(2) == 1);
                else p.SetValue(options.LogicConfig, Enum.GetValues(p.PropertyType).GetValue(rng.Next(Enum.GetValues(p.PropertyType).Length)));
            }
            var world = new World(options.ToConfig(), "", 0, "");
            var cfg = world.Config.LogicConfig;

            // shuffle the dungeon rewards (Agahnim's stays) and the medallions
            var shuffled = world.Regions.OfType<IHasReward>().Where(r => r.IsShuffledReward).ToList();
            var pool = shuffled.Select(r => r.RewardType).OrderBy(_ => rng.Next()).ToList();
            foreach (var r in shuffled) r.Reward.Region = null;
            for (var i = 0; i < shuffled.Count; i++) shuffled[i].SetRewardType(pool[i]);
            var medallions = new[] { ItemType.Bombos, ItemType.Ether, ItemType.Quake };
            foreach (var r in world.Regions.OfType<IHasPrerequisite>()) r.RequiredItem = medallions[rng.Next(3)];

            // key placements
            var placed = new List<(Location, ItemType)>();
            var allLocs = world.Locations.ToList();
            var regionList = world.Regions.ToList();
            if (rng.Next(2) == 0)
            {
                foreach (var t in placeable)
                {
                    if (rng.Next(3) != 0) continue;
                    var loc = allLocs[rng.Next(allLocs.Count)];
                    if (placed.Any(x => x.Item1 == loc)) continue;
                    loc.Item = new Item(t, world);
                    placed.Add((loc, t));
                }
            }

            var density = rng.NextDouble();
            var items = new List<ItemType>();
            foreach (var t in itemTypes)
            {
                if (rng.NextDouble() > density) continue;
                var c = rng.Next(4) == 0 ? rng.Next(1, 5) : 1;
                for (var i = 0; i < c; i++) items.Add(t);
            }
            var rewards = rewardTypes.Where(_ => rng.NextDouble() < density * 0.5).ToList();
            var bosses = bossTypes.Where(_ => rng.NextDouble() < density * 0.5).ToList();
            var prog = new Progression(items, rewards, bosses);

            sb.Append("case\ncfg");
            foreach (var p in cfg.GetType().GetProperties().Where(p => p.CanWrite && (p.PropertyType == typeof(bool) || p.PropertyType.IsEnum)))
                sb.Append($" {p.Name}={Convert.ToInt32(p.GetValue(cfg))}");
            sb.Append($"\nworld zk={(world.Config.ZeldaKeysanity ? 1 : 0)} mk={(world.Config.MetroidKeysanity ? 1 : 0)} ganon={mode.GanonCrystalCount} gt={mode.GanonsTowerCrystalCount} tourian={mode.TourianBossCount}");
            sb.Append("\nrewards");
            foreach (var r in world.Regions.OfType<IHasReward>()) sb.Append($" {regionList.IndexOf((Region)r)}:{(int)r.RewardType}");
            sb.Append("\nprereq");
            foreach (var r in world.Regions.OfType<IHasPrerequisite>()) sb.Append($" {regionList.IndexOf((Region)r)}:{(int)r.RequiredItem}");
            sb.Append("\nplaced");
            foreach (var (l, t) in placed) sb.Append($" {(int)l.Id}:{(int)t}");
            sb.Append("\nitems");
            foreach (var g in items.GroupBy(x => x)) sb.Append($" {(int)g.Key}:{g.Count()}");
            sb.Append("\nprogrewards");
            foreach (var g in rewards.GroupBy(x => x)) sb.Append($" {(int)g.Key}:{g.Count()}");
            sb.Append("\nbosses");
            foreach (var b in bosses) sb.Append($" {(int)b}");
            sb.Append("\navailable");
            foreach (var l in world.Locations.Where(l => l.IsAvailable(prog))) sb.Append($" {(int)l.Id}");
            sb.Append("\nenter");
            foreach (var r in world.Regions) sb.Append($" {(r.CanEnter(prog, true) ? 1 : 0)}{(r.CanEnter(prog, false) ? 1 : 0)}");
            sb.Append("\nreward");
            foreach (var r in world.Regions.OfType<IHasReward>()) sb.Append($" {(r.CanRetrieveReward(prog, false) ? 1 : 0)}{(r.CanRetrieveReward(prog, true) ? 1 : 0)}");
            sb.Append("\nboss");
            foreach (var r in world.Regions.OfType<IHasBoss>()) sb.Append($" {(r.CanBeatBoss(prog, false) ? 1 : 0)}");
            sb.Append('\n');
        }
        File.WriteAllText(path, sb.ToString());
        Console.WriteLine($"{count} world cases -> {path}");
        return 0;
    }
}

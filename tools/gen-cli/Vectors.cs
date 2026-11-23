using System.Reflection;
using System.Text;
using TrackerCouncil.Smz3.Data.Logic;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.WorldData;
using TrackerCouncil.Smz3.Shared.Enums;

/// <summary>
/// Test vectors for the C port of the logic: random inventories and logic settings, with what the C# Progression
/// properties and Logic methods answer for each. Used by core/gen/test_logic.c.
/// </summary>
public static class Vectors
{
    public static int Run(string[] args)
    {
        var count = int.Parse(args[1]);
        var rng = new Random(int.Parse(args[2]));
        var path = args[3];

        var options = new RandomizerOptions();
        var world = new World(options.ToConfig(), "", 0, "");
        var logic = new Logic(world);
        var logicConfig = world.Config.LogicConfig;
        var cfgProps = logicConfig.GetType().GetProperties()
            .Where(p => p.CanWrite && (p.PropertyType == typeof(bool) || p.PropertyType.IsEnum)).ToList();

        var itemTypes = Enum.GetValues<ItemType>().Distinct().Where(t => t != ItemType.Nothing).ToList();
        var rewardTypes = Enum.GetValues<RewardType>().Distinct().ToList();
        var bossTypes = Enum.GetValues<BossType>().Distinct().ToList();
        var methods = typeof(Logic).GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.DeclaredOnly)
            .Where(m => m.ReturnType == typeof(bool) && m.GetParameters().Length > 0
                        && m.GetParameters()[0].ParameterType == typeof(Progression)
                        && m.GetParameters().Skip(1).All(p => p.ParameterType == typeof(bool) || p.ParameterType == typeof(int))
                        && m.Name is not ("CheckAgahnim" or "CanAccessMaridiaPortal"))
            .ToList();
        var props = typeof(Progression).GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => (p.PropertyType == typeof(bool) || p.PropertyType == typeof(int)) && p.GetIndexParameters().Length == 0
                        && p.Name is not ("Count" or "IsReadOnly"))
            .ToList();

        var sb = new StringBuilder();
        for (var n = 0; n < count; n++)
        {
            foreach (var p in cfgProps)
            {
                if (p.PropertyType == typeof(bool)) p.SetValue(logicConfig, rng.Next(2) == 1);
                else p.SetValue(logicConfig, Enum.GetValues(p.PropertyType).GetValue(rng.Next(Enum.GetValues(p.PropertyType).Length)));
            }

            var density = rng.NextDouble();
            var items = new List<ItemType>();
            foreach (var t in itemTypes)
            {
                if (rng.NextDouble() > density) continue;
                var c = rng.Next(4) == 0 ? rng.Next(1, 5) : 1;
                for (var i = 0; i < c; i++) items.Add(t);
            }
            var rewards = new List<RewardType>();
            foreach (var t in rewardTypes) if (rng.NextDouble() < density) rewards.Add(t);
            var bosses = new List<BossType>();
            foreach (var t in bossTypes) if (rng.NextDouble() < density) bosses.Add(t);
            var prog = new Progression(items, rewards, bosses);

            sb.Append("case\ncfg");
            foreach (var p in cfgProps) sb.Append($" {p.Name}={Convert.ToInt32(p.GetValue(logicConfig))}");
            sb.Append("\nitems");
            foreach (var g in items.GroupBy(x => x)) sb.Append($" {(int)g.Key}:{g.Count()}");
            sb.Append("\nrewards");
            foreach (var g in rewards.GroupBy(x => x)) sb.Append($" {(int)g.Key}:{g.Count()}");
            sb.Append("\nbosses");
            foreach (var b in bosses) sb.Append($" {(int)b}");
            sb.Append("\nres");
            foreach (var p in props) sb.Append($" P.{p.Name}={Convert.ToInt32(p.GetValue(prog))}");
            foreach (var m in methods)
            {
                var ps = m.GetParameters().Skip(1).ToList();
                var combos = new List<int[]> { Array.Empty<int>() };
                foreach (var p in ps)
                {
                    var range = p.ParameterType == typeof(bool) ? new[] { 0, 1 } : Enumerable.Range(0, 8).ToArray();
                    combos = combos.SelectMany(c => range.Select(v => c.Append(v).ToArray())).ToList();
                }
                foreach (var combo in combos)
                {
                    var callArgs = new object[ps.Count + 1];
                    callArgs[0] = prog;
                    for (var i = 0; i < ps.Count; i++) callArgs[i + 1] = ps[i].ParameterType == typeof(bool) ? combo[i] == 1 : combo[i];
                    var r = (bool)m.Invoke(logic, callArgs)!;
                    sb.Append($" {m.Name}{string.Concat(combo.Select(v => "/" + v))}={(r ? 1 : 0)}");
                }
            }
            sb.Append('\n');
        }
        File.WriteAllText(path, sb.ToString());
        Console.WriteLine($"{count} cases -> {path}");
        return 0;
    }
}

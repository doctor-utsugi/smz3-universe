using System.Reflection;
using System.Text;
using Randomizer.SuperMetroid;

/// <summary>Random inventories with the C# answers, for the logic and world checks of core/smgen.</summary>
static class SmVectors
{
    static readonly ItemType[] Types = Enum.GetValues<ItemType>();

    static List<Item> RandomItems(Random rng, World world)
    {
        var density = rng.NextDouble();
        var items = new List<Item>();
        foreach (var t in Types)
        {
            if (rng.NextDouble() > density) continue;
            var c = t is ItemType.ETank or ItemType.ReserveTank or ItemType.Missile or ItemType.Super or ItemType.PowerBomb && rng.Next(3) == 0 ? rng.Next(1, 7) : 1;
            for (var i = 0; i < c; i++) items.Add(new Item(t, world));
        }
        return items;
    }

    static void AppendItems(StringBuilder sb, List<Item> items)
    {
        sb.Append("items");
        foreach (var g in items.GroupBy(x => x.Type)) sb.Append($" {(int)g.Key}:{g.Count()}");
        sb.Append('\n');
    }

    public static int Items(string[] args)
    {
        var count = int.Parse(args[1]);
        var rng = new Random(int.Parse(args[2]));
        var world = new World(new Config(), "", 0, "");
        var methods = typeof(ItemListExtensions).GetMethods(BindingFlags.Public | BindingFlags.Static)
            .Where(m => m.ReturnType == typeof(bool) && m.Name.StartsWith("Can") || m.Name == "HasEnergyReserves").ToList();
        var sb = new StringBuilder();
        for (var n = 0; n < count; n++)
        {
            var items = RandomItems(rng, world);
            sb.Append("case\n");
            AppendItems(sb, items);
            sb.Append("res");
            foreach (var m in methods)
            {
                var ps = m.GetParameters();
                if (ps.Length == 1)
                    sb.Append($" {m.Name}={((bool)m.Invoke(null, new object[] { items })! ? 1 : 0)}");
                else if (ps[1].ParameterType == typeof(Logic))
                    foreach (var l in Enum.GetValues<Logic>())
                        sb.Append($" {m.Name}/{(int)l}={((bool)m.Invoke(null, new object[] { items, l })! ? 1 : 0)}");
                else
                    for (var a = 0; a <= 8; a++)
                        sb.Append($" {m.Name}/{a}={((bool)m.Invoke(null, new object[] { items, a })! ? 1 : 0)}");
            }
            sb.Append('\n');
        }
        File.WriteAllText(args[3], sb.ToString());
        Console.WriteLine($"{count} cases -> {args[3]}");
        return 0;
    }

    public static int World(string[] args)
    {
        var count = int.Parse(args[1]);
        var rng = new Random(int.Parse(args[2]));
        var worlds = Enum.GetValues<Logic>().ToDictionary(l => l, l => new World(new Config { Logic = l }, "", 0, ""));
        var sb = new StringBuilder();
        for (var n = 0; n < count; n++)
        {
            var logic = rng.Next(2) == 0 ? Logic.Casual : Logic.Tournament;
            var world = worlds[logic];
            var items = RandomItems(rng, world);
            sb.Append($"case\nlogic {(int)logic}\n");
            AppendItems(sb, items);
            sb.Append("enter");
            foreach (var r in world.Regions) sb.Append(r.CanEnter(items) ? " 1" : " 0");
            sb.Append("\navailable");
            foreach (var l in world.Locations.Where(l => l.Available(items))) sb.Append($" {l.Id}");
            sb.Append('\n');
        }
        File.WriteAllText(args[3], sb.ToString());
        Console.WriteLine($"{count} world cases -> {args[3]}");
        return 0;
    }
}

using System.Text.Json;
using Randomizer.SuperMetroid;

/// <summary>The option table as the web generator serves it (key, description, type, values, default).</summary>
static class SmSchema
{
    public static int Run(string[] args)
    {
        var sm = new Randomizer.SuperMetroid.Randomizer();
        var list = sm.Options.Select(o => new {
            key = o.Key, description = o.Description, type = o.Type.ToString().ToLowerInvariant(),
            values = o.Values, @default = o.Default,
        });
        File.WriteAllText(args[1], JsonSerializer.Serialize(new { id = sm.Id, name = sm.Name, version = sm.Version, options = list },
            new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
}

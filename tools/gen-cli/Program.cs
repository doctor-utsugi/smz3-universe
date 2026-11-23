// Headless SMZ3 seed generator.
//   gen-cli <sm-rom> <z3-rom> <out.sfc>        generate one ROM with default options
//   gen-cli serve <sm-rom> <z3-rom> <port>     HTTP service: GET /schema, POST /generate (options JSON -> patch)
using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using MSURandomizerLibrary;
using MSURandomizerLibrary.Models;
using MSURandomizerLibrary.Services;
using Serilog;
using TrackerCouncil.Smz3.Data.Configuration;
using TrackerCouncil.Smz3.Data.Interfaces;
using TrackerCouncil.Smz3.Data.Options;
using TrackerCouncil.Smz3.Data.Services;
using TrackerCouncil.Smz3.SeedGenerator;
using TrackerCouncil.Smz3.SeedGenerator.Infrastructure;
using TrackerCouncil.Smz3.Shared.Models;

if (args.Length == 2 && args[0] == "rngvectors") return RngVectors.Run(args);
if (args.Length == 4 && args[0] == "vectors") return Vectors.Run(args);
if (args.Length == 4 && args[0] == "worldvectors") return WorldVectors.Run(args);

if (args.Length < 3 && args[0] != "textdata" && args[0] != "hintdata" && args[0] != "schema")
{
    Console.Error.WriteLine("usage: gen-cli <sm-rom> <z3-rom> <out.sfc>");
    Console.Error.WriteLine("       gen-cli serve <sm-rom> <z3-rom> <port>");
    return 2;
}

var serve = args[0] == "serve";
var placement = args[0] == "placement" || args[0] == "placementbatch" || args[0] == "textdata" || args[0] == "hintdata" || args[0] == "schema";
var smPath = placement ? "" : Path.GetFullPath(serve ? args[1] : args[0]);
var z3Path = placement ? "" : Path.GetFullPath(serve ? args[2] : args[1]);

Log.Logger = placement
    ? new LoggerConfiguration().MinimumLevel.Debug().WriteTo.Sink(new PlacementTrace()).CreateLogger()
    : new LoggerConfiguration().WriteTo.Console().CreateLogger();

var services = new ServiceCollection()
    .AddLogging(l => l.AddSerilog(dispose: true))
    .AddConfigs()
    .AddSingleton<ITrackerStateService, TrackerStateService>()
    .AddSingleton<TrackerSpriteService>()
    .AddMsuRandomizerServices()
    .AddSingleton<SpriteService>()
    .AddRandomizerServices()
    .AddTransient<RomLauncherService>()
    .AddSingleton<OptionsFactory>()
    .BuildServiceProvider();

services.GetRequiredService<IMsuRandomizerInitializationService>().Initialize(new MsuRandomizerInitializationRequest
{
    MsuAppSettingsStream = Assembly.GetExecutingAssembly().GetManifestResourceStream("msu-randomizer-settings.yml"),
    LookupMsus = false
});
services.GetRequiredService<RandomizerContext>().Migrate();

var generator = new Generator(services, smPath, z3Path);

if (args[0] == "schema")
{
    File.WriteAllText(args[1], System.Text.Json.JsonSerializer.Serialize(Server.Schema()));
    return 0;
}

if (args[0] == "textdata")
{
    File.WriteAllText(args[1], TextData.Dump(services));
    return 0;
}

if (args[0] == "hintdata")
{
    File.WriteAllText(args[1], HintData.Dump(services));
    return 0;
}

if (args[0] == "placementbatch")
{
    // gen-cli placementbatch <cases.jsonl> <out.txt>: one {"seed":"..", ...settings} per line
    var jopt = new System.Text.Json.JsonSerializerOptions { PropertyNameCaseInsensitive = true };
    var outText = new System.Text.StringBuilder();
    foreach (var line in File.ReadLines(args[1]).Where(l => l.Trim().Length > 0))
    {
        var cfg = System.Text.Json.JsonSerializer.Deserialize<RunSettings>(line, jopt) ?? new RunSettings();
        PlacementTrace.Lines.Clear();
        try { outText.Append("case\n").Append(generator.Placement(cfg, cfg.Seed ?? "1")); }
        catch (TrackerCouncil.Smz3.SeedGenerator.RandomizerGenerationException e) { outText.Append("error\t").Append(e.Message).Append('\n'); }
    }
    File.WriteAllText(args[2], outText.ToString());
    return 0;
}

if (args[0] == "plandobatch")
{
    // gen-cli plandobatch <cases.jsonl> <out.txt> <yaml dir>: exports each case's plando config to <yaml dir>/<n>.yml and plays it back
    var jopt = new System.Text.Json.JsonSerializerOptions { PropertyNameCaseInsensitive = true };
    var outText = new System.Text.StringBuilder();
    Directory.CreateDirectory(args[3]);
    int n = 0;
    foreach (var line in File.ReadLines(args[1]).Where(l => l.Trim().Length > 0))
    {
        var cfg = System.Text.Json.JsonSerializer.Deserialize<RunSettings>(line, jopt) ?? new RunSettings();
        PlacementTrace.Lines.Clear();
        try { outText.Append("case\n").Append(generator.Plando(cfg, cfg.Seed ?? "1", Path.Combine(args[3], n + ".yml"))); }
        catch (TrackerCouncil.Smz3.SeedGenerator.RandomizerGenerationException e) { outText.Append("error\t").Append(e.Message).Append('\n'); }
        n++;
    }
    File.WriteAllText(args[2], outText.ToString());
    return 0;
}

if (placement)
{
    // gen-cli placement <seed> <settings.json> <out.txt>
    var settings = System.Text.Json.JsonSerializer.Deserialize<RunSettings>(File.ReadAllText(args[2]), new System.Text.Json.JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? new RunSettings();
    File.WriteAllText(args[3], generator.Placement(settings, args[1]));
    return 0;
}

if (serve)
{
    await new Server(generator).RunAsync(int.Parse(args[3]));
    return 0;
}

var (rom, seed, error) = await generator.GenerateAsync(new RunSettings { Seed = args.Length > 3 ? args[3] : null });
if (rom == null)
{
    Console.Error.WriteLine($"failed: [{error}]");
    return 1;
}

File.WriteAllBytes(args[2], rom);
Console.WriteLine($"wrote {args[2]} seed={seed} {error}");
return 0;


/// <summary>Collects "Placed {Item} at {Location}" debug events with the real location ids, for the C filler's trace comparison.</summary>
public class PlacementTrace : Serilog.Core.ILogEventSink
{
    public static readonly List<string> Lines = new();

    public void Emit(Serilog.Events.LogEvent e)
    {
        if (e.MessageTemplate.Text.StartsWith("LISTORDER") && e.Properties.TryGetValue("Ids", out var ids))
        {
            Lines.Add("LISTORDER\t" + ((Serilog.Events.ScalarValue)ids).Value);
            return;
        }
        if (e.MessageTemplate.Text.StartsWith("INV "))
        {
            string P(string k) => ((Serilog.Events.ScalarValue)e.Properties[k]).Value?.ToString() ?? "";
            Lines.Add("INV\t" + P("Inv") + "|" + P("R") + "|" + P("B") + "|" + P("F"));
            return;
        }
        if (!e.Properties.TryGetValue("Location", out var l) || !e.Properties.TryGetValue("Item", out var i)) return;
        if (l is Serilog.Events.ScalarValue { Value: string ls } && i is Serilog.Events.ScalarValue { Value: string iss })
            Lines.Add($"{ls}\t{iss}");
    }
}

using System.Net;
using System.Reflection;
using System.Text;
using System.Text.Json;
using TrackerCouncil.Smz3.Data.Logic;
using TrackerCouncil.Smz3.Shared.Enums;

/// <summary>
/// GET /schema describes the options panel; POST /generate takes a RunSettings JSON and answers with a patch
/// against the raw combined Super Metroid + ALttP ROM (magic "SPT1", then records of u32 offset, u32 length, bytes).
/// The seed comes back in the X-Seed header. The device combines its own base ROMs, so no ROM is ever sent.
/// </summary>
public class LogicRequest
{
    public RunSettings? Settings { get; set; }
    public List<string>? Items { get; set; }
}

public class Server(Generator generator)
{
    private static readonly JsonSerializerOptions Json = new() { PropertyNameCaseInsensitive = true };

    public async Task RunAsync(int port)
    {
        var listener = new HttpListener();
        listener.Prefixes.Add($"http://*:{port}/");
        listener.Start();
        Console.WriteLine($"listening on {port}");
        byte[]? baseRom = null;
        while (true)
        {
            var ctx = await listener.GetContextAsync();
            _ = Task.Run(async () =>
            {
                try
                {
                    var path = ctx.Request.Url!.AbsolutePath;
                    if (ctx.Request.HttpMethod == "GET" && path == "/schema")
                    {
                        Reply(ctx, 200, "application/json", Encoding.UTF8.GetBytes(JsonSerializer.Serialize(Schema())));
                    }
                    else if (ctx.Request.HttpMethod == "POST" && path == "/generate")
                    {
                        var settings = JsonSerializer.Deserialize<RunSettings>(ctx.Request.InputStream, Json) ?? new RunSettings();
                        var (rom, seed, error) = await generator.GenerateAsync(settings);
                        if (rom == null)
                        {
                            Reply(ctx, 500, "text/plain", Encoding.UTF8.GetBytes(error ?? "generation failed"));
                            return;
                        }
                        baseRom ??= generator.CombinedBase();
                        ctx.Response.AddHeader("X-Seed", seed);
                        Reply(ctx, 200, "application/octet-stream", Diff(baseRom, rom));
                    }
                    else if (ctx.Request.HttpMethod == "POST" && path == "/logic")
                    {
                        var request = JsonSerializer.Deserialize<LogicRequest>(ctx.Request.InputStream, Json) ?? new LogicRequest();
                        var ids = generator.Reachable(request.Settings ?? new RunSettings(), request.Items ?? new List<string>());
                        Reply(ctx, 200, "application/json", Encoding.UTF8.GetBytes(JsonSerializer.Serialize(ids)));
                    }
                    else
                    {
                        Reply(ctx, 404, "text/plain", "not found"u8.ToArray());
                    }
                }
                catch (Exception e)
                {
                    Console.Error.WriteLine(e);
                    try { Reply(ctx, 500, "text/plain", Encoding.UTF8.GetBytes(e.Message)); } catch { /* connection gone */ }
                }
            });
        }
    }

    private static void Reply(HttpListenerContext ctx, int status, string type, byte[] body)
    {
        ctx.Response.StatusCode = status;
        ctx.Response.ContentType = type;
        ctx.Response.ContentLength64 = body.Length;
        ctx.Response.OutputStream.Write(body);
        ctx.Response.Close();
    }

    public static byte[] Diff(byte[] baseRom, byte[] rom)
    {
        using var ms = new MemoryStream();
        using var w = new BinaryWriter(ms);
        w.Write("SPT1"u8.ToArray());
        w.Write(rom.Length);
        var i = 0;
        while (i < rom.Length)
        {
            if (i < baseRom.Length && baseRom[i] == rom[i]) { i++; continue; }
            var start = i;
            var lastDiff = i;
            // merge runs separated by fewer than 12 equal bytes
            while (i < rom.Length && i - lastDiff < 12)
            {
                if (i >= baseRom.Length || baseRom[i] != rom[i]) lastDiff = i;
                i++;
            }
            var end = lastDiff + 1;
            w.Write(start);
            w.Write(end - start);
            w.Write(rom, start, end - start);
            i = end;
        }
        return ms.ToArray();
    }

    internal static object Schema()
    {
        var logic = new List<object>();
        foreach (var p in typeof(LogicConfig).GetProperties())
        {
            var attr = p.GetCustomAttributes().FirstOrDefault(a => a.GetType().Name == "DynamicFormFieldCheckBoxAttribute");
            if (attr == null || p.PropertyType != typeof(bool)) continue;
            // The library exposes the checkbox text as Label and the tooltip as ToolTipText.
            string? Read(string n)
            {
                var props = attr.GetType().GetProperties().Where(x => x.PropertyType == typeof(string)).ToList();
                var match = props.FirstOrDefault(x => x.Name.Equals(n == "CheckBoxText" ? "Label" : n, StringComparison.OrdinalIgnoreCase))
                            ?? props.FirstOrDefault(x => n == "CheckBoxText" ? x.Name.Contains("Text", StringComparison.OrdinalIgnoreCase) && !x.Name.Contains("Tip", StringComparison.OrdinalIgnoreCase)
                                : n == "ToolTipText" ? x.Name.Contains("Tip", StringComparison.OrdinalIgnoreCase)
                                : x.Name.Contains("Group", StringComparison.OrdinalIgnoreCase));
                var value = match?.GetValue(attr)?.ToString();
                if (!string.IsNullOrEmpty(value) || n != "CheckBoxText") return value;
                // The checkbox text sits in a non-public member of the attribute class.
                return attr.GetType().GetMembers(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic)
                    .Where(m => m.Name.Contains("Text", StringComparison.OrdinalIgnoreCase) && !m.Name.Contains("Tip", StringComparison.OrdinalIgnoreCase))
                    .Select(m => m is FieldInfo f ? f.GetValue(attr)?.ToString() : (m as PropertyInfo)?.GetValue(attr)?.ToString())
                    .FirstOrDefault(v => !string.IsNullOrEmpty(v));
            }
            logic.Add(new
            {
                name = p.Name,
                label = Read("CheckBoxText"),
                tip = Read("ToolTipText"),
                group = Read("GroupName"),
                value = (bool)p.GetValue(new LogicConfig())!,
            });
        }

        static object Choices<T>() where T : struct, Enum =>
            Enum.GetValues<T>().Select(v => new { value = v.ToString(), label = Describe(v) }).ToArray();

        return new
        {
            choices = new
            {
                keysanity = Choices<KeysanityMode>(),
                itemPlacement = Choices<ItemPlacementRule>(),
                gameMode = Choices<GameModeType>(),
                tourianBossDoor = Choices<TourianBossDoor>(),
                pyramidHole = Choices<PyramidHole>(),
            },
            logic,
        };
    }

    private static string Describe(Enum value) =>
        value.GetType().GetField(value.ToString())?
            .GetCustomAttribute<System.ComponentModel.DescriptionAttribute>()?.Description ?? value.ToString();
}

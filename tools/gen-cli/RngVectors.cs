using System.Text;

public static class RngVectors
{
    public static int Run(string[] args)
    {
        var sb = new StringBuilder();
        foreach (var seed in new[] { 0, 1, 42, 123456789, int.MaxValue, -5, 987654321 })
        {
            var r = new Random(seed);
            sb.Append($"seed {seed}");
            for (var i = 0; i < 30; i++) sb.Append($" {r.Next()}");
            for (var i = 0; i < 30; i++) sb.Append($" {r.Next(7 + i * 31)}");
            for (var i = 0; i < 10; i++) sb.Append($" {r.Next(3, 20 + i)}");
            sb.Append('\n');
        }
        File.WriteAllText(args[1], sb.ToString());
        return 0;
    }
}

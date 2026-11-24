// Oracle for the C port of tewtal's Super Metroid randomizer (core/smgen). Verbs mirror tools/gen-cli:
//   gen-cli-sm vectors <count> <rngseed> <out.txt>        item helpers (CanIbj, CanHellRun, ...) for random inventories
//   gen-cli-sm worldvectors <count> <rngseed> <out.txt>   region CanEnter and location Available for random inventories
//   gen-cli-sm placementbatch <cases.jsonl> <out.txt>     placement and seed patches for {"seed","logic","placement","race"} lines
//   gen-cli-sm rom <sm.sfc> <sm_randomizer.ips> <seed> <logic> <placement> <race> <out.sfc>   whole ROM, as Randomizer.CLI builds it
//   gen-cli-sm schema <out.json>                          the option table (RandomizerOptions.List)
using Randomizer.SuperMetroid;

return args.Length switch {
    4 when args[0] == "vectors" => SmVectors.Items(args),
    4 when args[0] == "worldvectors" => SmVectors.World(args),
    3 when args[0] == "placementbatch" => SmPlacement.Batch(args),
    8 when args[0] == "rom" => SmPlacement.Rom(args),
    2 when args[0] == "schema" => SmSchema.Run(args),
    _ => Usage(),
};

static int Usage()
{
    Console.Error.WriteLine("usage: gen-cli-sm vectors|worldvectors <count> <rngseed> <out> | placementbatch <cases.jsonl> <out> | rom <sm> <ips> <seed> <logic> <placement> <race> <out> | schema <out>");
    return 2;
}

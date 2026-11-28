// Optional offline transport benchmark. Compiles actual game snapshot generation,
// codec and stream test bodies against explicit non-Unreal adapters.
#include "BYMNetworkSnapshotTests.cpp"
#include "BYMSimulation.h"
#include "BYMSettings.h"
#include <fstream>

namespace BYMNetworkBenchmark
{
    using namespace BYMNetworkSnapshotTests;
    FObject Child(const FObject& Object, const TCHAR* Key)
    { const FObject* Result = nullptr; return Object && Object->TryGetObjectField(Key, Result) && Result ? *Result : FObject(); }
    const FArray& Array(const FObject& Object, const TCHAR* Key)
    { static const FArray Empty; const FArray* Result = nullptr; return Object && Object->TryGetArrayField(Key, Result) && Result ? *Result : Empty; }
    FString String(const FObject& Object, const TCHAR* Key)
    { FString Result; if (Object) Object->TryGetStringField(Key, Result); return Result; }
    double Number(const FObject& Object, const TCHAR* Key)
    { double Result = 0; if (Object) Object->TryGetNumberField(Key, Result); return Result; }
    bool Accepted(const FString& Response)
    { bool Result = false; const FObject Object = Parse(Response); return Object && Object->TryGetBoolField(TEXT("ok"), Result) && Result; }
    FObject SavedYard(const FObject& Saved, const FString& Id)
    { for (const auto& Entry : Array(Saved, TEXT("yards"))) if (String(Entry->AsObject(), TEXT("id")) == Id) return Entry->AsObject(); return nullptr; }
    void Require(bool Condition, const char* Message)
    { if (!Condition) throw std::runtime_error(Message); }
    void Initialize(FBYMSimulation& Simulation)
    {
        FString Error;
        Require(Simulation.Initialize(nullptr, Error), "Real simulation failed to load its bundled catalog");
        Require(Simulation.AddPlayer(TEXT("benchmark-player"), TEXT("Benchmark Player"), Error), "Fixture player creation failed");
        Require(Simulation.AddPlayer(TEXT("benchmark-rival"), TEXT("Benchmark Rival"), Error), "Fixture rival creation failed");
    }
    void Record(std::ofstream& Out, const char* Case, int32 Sample, const FString& Current, FString& Baseline)
    {
        using namespace BYMNetworkSnapshot;
        FEncoded Encoded, CompressedFull; FString Error, Decoded = Baseline;
        Require(Encode(Current, Baseline, true, true, Encoded, Error), "Actual bandwidth codec could not encode a real snapshot");
        Require(Encode(Current, FString(), false, true, CompressedFull, Error), "Compressed full-snapshot control failed");
        if (Encoded.bHasUpdate) Require(Decode(Encoded, Baseline, Decoded, Error), "Actual bandwidth codec could not decode a real snapshot");
        FObject Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("case"), Case); Row->SetNumberField(TEXT("sample"), Sample);
        Row->SetStringField(TEXT("before"), Current); Row->SetStringField(TEXT("after"), Decoded);
        Row->SetNumberField(TEXT("old_json_utf8_bytes"), FTCHARToUTF8(*Current, Current.Len()).Length());
        Row->SetNumberField(TEXT("compressed_full_bytes"), CompressedFull.Bytes.Num());
        Row->SetNumberField(TEXT("encoded_bytes"), Encoded.Bytes.Num()); Row->SetBoolField(TEXT("delta"), Encoded.bDelta);
        Row->SetBoolField(TEXT("compressed"), Encoded.bCompressed); Row->SetBoolField(TEXT("has_update"), Encoded.bHasUpdate);
        Out << *Write(Row) << '\n';
        Baseline = Current;
    }
    void ScenarioYard(std::ofstream& Out, bool Production)
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>(); Settings->ResourceProductionMultiplier = Production ? 1.f : 0.f;
        FBYMSimulation Simulation; Initialize(Simulation);
        if (Production) Require(Accepted(Simulation.Execute(TEXT("benchmark-player"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":10}"))), "Real hatchery queue fixture failed");
        const double Start = Number(Parse(Simulation.Snapshot(TEXT("benchmark-player"), FString())), TEXT("now"));
        FString Baseline;
        for (int32 Sample = 0; Sample <= 30; ++Sample)
        {
            if (Sample > 0) Simulation.Tick(Start + Sample * 2., 2.f);
            Record(Out, Production ? "production_and_hatching_60s" : "idle_yard_60s", Sample,
                Simulation.Snapshot(TEXT("benchmark-player"), FString()), Baseline);
        }
    }
    void ScenarioBattle(std::ofstream& Out)
    {
        UBYMSettings* Settings = GetMutableDefault<UBYMSettings>(); Settings->ResourceProductionMultiplier = 1.f;
        FBYMSimulation Simulation; Initialize(Simulation);
        Require(Accepted(Simulation.Execute(TEXT("benchmark-player"), TEXT("train"), TEXT("{\"type\":\"C1\",\"count\":20}"))), "Battle training fixture failed");
        const double Start = Number(Parse(Simulation.Snapshot(TEXT("benchmark-player"), FString())), TEXT("now")) + 301.;
        Simulation.Tick(Start, 0.f);
        Require(Accepted(Simulation.Execute(TEXT("benchmark-player"), TEXT("invade"), TEXT("{\"target\":\"benchmark-rival\"}"))), "Real invasion could not start");
        Require(Accepted(Simulation.Execute(TEXT("benchmark-player"), TEXT("deploy"), TEXT("{\"type\":\"C1\",\"count\":10,\"x\":39,\"y\":39}"))), "Real deployment fixture failed");
        FString Baseline;
        for (int32 Sample = 0; Sample <= 300; ++Sample)
        {
            if (Sample > 0) Simulation.Tick(Start + Sample * .2, .2f);
            Record(Out, "invasion_60s_at_5hz", Sample, Simulation.Snapshot(TEXT("benchmark-player"), FString()), Baseline);
        }
    }
    void ScenarioMap(std::ofstream& Out)
    {
        FBYMSimulation Seed; Initialize(Seed); FObject Saved = Seed.Serialize();
        FObject Yard = SavedYard(Saved, TEXT("benchmark-player")); Require(Yard.IsValid(), "Map fixture yard missing");
        FObject Locker;
        for (const auto& Value : Array(Yard, TEXT("buildings")))
        {
            FObject Building = Value->AsObject(); const int32 Type = static_cast<int32>(Number(Building, TEXT("type")));
            if (Type == 14) { Building->SetNumberField(TEXT("level"), 6); Building->SetNumberField(TEXT("targetLevel"), 6); Building->SetNumberField(TEXT("hp"), 200000.); }
            if (Type == 8) Locker = Building;
        }
        Require(Locker.IsValid(), "Map fixture Locker missing");
        FObject Room = Parse(Write(Locker)); Room->SetStringField(TEXT("id"), TEXT("benchmark-map-room"));
        Room->SetNumberField(TEXT("type"), 11); Room->SetNumberField(TEXT("level"), 2); Room->SetNumberField(TEXT("targetLevel"), 2);
        Room->SetNumberField(TEXT("hp"), 10000.); Room->SetNumberField(TEXT("x"), 40); Room->SetNumberField(TEXT("y"), 1);
        FArray Buildings = Array(Yard, TEXT("buildings")); Buildings.Add(MakeShared<FJsonValueObject>(Room)); Yard->SetArrayField(TEXT("buildings"), Buildings);
        FObject Economy = Child(Yard, TEXT("economy")); if (Economy) Economy->SetArrayField(TEXT("mushrooms"), FArray());
        FBYMSimulation Simulation; FString Error; Require(Simulation.Initialize(Saved, Error), "Real MR3 fixture failed to reload");
        const double Start = Number(Parse(Simulation.Snapshot(TEXT("benchmark-player"), FString())), TEXT("now"));
        FString Baseline;
        for (int32 Sample = 0; Sample <= 30; ++Sample)
        {
            if (Sample > 0) Simulation.Tick(Start + Sample * 2., 2.f);
            const FString Map = Simulation.Execute(TEXT("benchmark-player"), TEXT("mapView"), TEXT("{\"x\":-1,\"y\":-1,\"radius\":15,\"selected\":\"\",\"ownedPage\":0}"));
            Require(Accepted(Map) && Child(Parse(Map), TEXT("map")).IsValid(), "Real map response failed");
            Record(Out, "stationary_map_60s_at_2s", Sample, Map, Baseline);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    for (const auto& Test : OfflineTests())
    {
        const int Before = Failures; Test.Run();
        std::cout << (Before == Failures ? "PASS " : "FAIL ") << Test.Name << std::endl;
    }
    if (Failures) return 1;
    std::cout << "NETWORK_TESTS " << OfflineTests().size() << ' ' << Assertions << ' ' << Failures << std::endl;
    UBYMSettings* Settings = GetMutableDefault<UBYMSettings>(); Settings->RivalBotCount = 0;
    Settings->StartingResources = 5000; Settings->YardCells = 40;
    std::ofstream Out(argv[1]); if (!Out) return 3;
    try
    {
        BYMNetworkBenchmark::ScenarioYard(Out, false);
        BYMNetworkBenchmark::ScenarioYard(Out, true);
        BYMNetworkBenchmark::ScenarioBattle(Out);
        BYMNetworkBenchmark::ScenarioMap(Out);
    }
    catch (const std::exception& Error) { std::cerr << Error.what() << std::endl; return 4; }
    return 0;
}

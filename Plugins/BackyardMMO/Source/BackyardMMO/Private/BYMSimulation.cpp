#include "BYMSimulation.h"
#include "BYMCombatRules.h"
#include "BYMSettings.h"
#include "BYMYardResidents.h"
#include "BYMEconomy.h"
#include "BYMBuildingLifecycle.h"
#include "BYMExperience.h"
#include "BYMBunkerRules.h"
#include "BYMWorkerMovement.h"
#include "BYMMapRoomRules.h"
#include "BYMBotLayout.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <cmath>

namespace BYMSim
{
    constexpr double ResourceLimit = 1000000000.0;
    int32 TroopLimit() { return FMath::Clamp(GetDefault<UBYMSettings>()->MaximumTroopsPerBattle, 1, 500); }
    int32 BattleLimit() { return FMath::Clamp(GetDefault<UBYMSettings>()->MaximumConcurrentBattles, 1, 100); }
    double BattleSeconds() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->MaximumBattleSeconds), 30.0, 3600.0); }
    double ConstructionMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->ConstructionTimeMultiplier), 0.01, 100.0); }
    double TrainingMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->TrainingTimeMultiplier), 0.01, 100.0); }
    double ProgressionMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->MonsterProgressionTimeMultiplier), 0.01, 100.0); }
    double ProductionMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->ResourceProductionMultiplier), 0.0, 100.0); }
    double DamageMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->CombatDamageMultiplier), 0.01, 100.0); }
    double MovementMultiplier() { return FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->MonsterMovementMultiplier), 0.01, 10.0); }
    constexpr double SourcePixelsPerCell = 20.0;
    constexpr double SourceSimulationHz = 72.0; // Supplied refitted GLOBAL.SIM_STEPS_PER_MS.
    double ProjectedLength(const FVector2D& Delta)
    { const double X = Delta.X - Delta.Y, Y = (Delta.X + Delta.Y) * 0.5; return FMath::Sqrt(X * X + Y * Y); }
    double SourceGroundDistancePixels(const FVector2D& A, const FVector2D& B)
    {
        // Targeting.getCreepsInRange uses PATHING.FromISO int coordinates.
        // Truncate each endpoint before measuring; projected contact is separate.
        return FVector2D(std::trunc(A.X * SourcePixelsPerCell) - std::trunc(B.X * SourcePixelsPerCell),
            std::trunc(A.Y * SourcePixelsPerCell) - std::trunc(B.Y * SourcePixelsPerCell)).Size();
    }
    bool SegmentHitsRect(const FVector2D& Start, const FVector2D& End, const FVector2D& Minimum, const FVector2D& Maximum)
    {
        const FVector2D Delta = End - Start; double Entry = 0, Exit = 1;
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            const double Origin = Axis == 0 ? Start.X : Start.Y, Direction = Axis == 0 ? Delta.X : Delta.Y;
            const double Min = (Axis == 0 ? Minimum.X : Minimum.Y) + 0.0001, Max = (Axis == 0 ? Maximum.X : Maximum.Y) - 0.0001;
            if (FMath::Abs(Direction) < 0.000001) { if (Origin <= Min || Origin >= Max) return false; }
            else
            {
                const double A = (Min - Origin) / Direction, C = (Max - Origin) / Direction;
                Entry = FMath::Max(Entry, FMath::Min(A, C)); Exit = FMath::Min(Exit, FMath::Max(A, C));
                if (Entry >= Exit) return false;
            }
        }
        return true;
    }
    const TCHAR* Resources[4] = { TEXT("twigs"), TEXT("pebbles"), TEXT("putty"), TEXT("goo") };
    using FObject = TSharedPtr<FJsonObject>;
    using FArray = TArray<TSharedPtr<FJsonValue>>;
    bool IsOperationalMonster(const FString& Type);

    FObject Obj() { return MakeShared<FJsonObject>(); }
    FObject Child(const FObject& O, const TCHAR* K)
    {
        const FObject* Value = nullptr;
        return O.IsValid() && O->TryGetObjectField(K, Value) && Value ? *Value : FObject();
    }
    const FArray& Array(const FObject& O, const TCHAR* K)
    {
        static const FArray Empty;
        const FArray* Value = nullptr;
        return O.IsValid() && O->TryGetArrayField(K, Value) && Value ? *Value : Empty;
    }
    double Number(const FObject& O, const TCHAR* K, double Fallback = 0)
    {
        double V = Fallback;
        if (!O.IsValid() || !O->TryGetNumberField(K, V) || !FMath::IsFinite(V)) return Fallback;
        return V;
    }
    FString Str(const FObject& O, const TCHAR* K, const FString& Fallback = FString())
    {
        FString V;
        return O.IsValid() && O->TryGetStringField(K, V) ? V : Fallback;
    }
    bool Flag(const FObject& O, const TCHAR* K, bool Fallback = false)
    {
        bool V = Fallback;
        return O.IsValid() && O->TryGetBoolField(K, V) ? V : Fallback;
    }
    bool Integer(const FObject& O, const TCHAR* K, int32 Min, int32 Max, int32& Out)
    {
        double V = 0;
        if (!O.IsValid() || !O->TryGetNumberField(K, V) || !FMath::IsFinite(V) || V < Min || V > Max || V != FMath::FloorToDouble(V)) return false;
        Out = static_cast<int32>(V);
        return true;
    }
    FString Encode(const FObject& O)
    {
        FString Out;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
        FJsonSerializer::Serialize(O.ToSharedRef(), Writer);
        return Out;
    }
    bool Decode(const FString& Text, FObject& Out)
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
    }
    FString Reply(bool Ok, const FString& Message)
    {
        FObject R = Obj(); R->SetBoolField(TEXT("ok"), Ok);
        R->SetStringField(Ok ? TEXT("message") : TEXT("error"), Message);
        return Encode(R);
    }
    FString NewId() { return FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    FObject ResourceObject(const int64* Values)
    {
        FObject O = Obj(); for (int32 I = 0; I < 4; ++I) O->SetNumberField(Resources[I], static_cast<double>(Values[I])); return O;
    }
    bool ReadResources(const FObject& O, int64* Values)
    {
        if (!O.IsValid()) return false;
        for (int32 I = 0; I < 4; ++I)
        {
            double V = -1;
            if (!O->TryGetNumberField(Resources[I], V) || !FMath::IsFinite(V) || V < 0 || V > ResourceLimit || FMath::FloorToDouble(V) != V) return false;
            Values[I] = static_cast<int64>(V);
        }
        return true;
    }

    struct FRequirement { int32 Type = 0, Count = 0, Level = 0; };
    struct FLevel
    {
        int64 Cost[4] = {0, 0, 0, 0};
        int64 SourceSeconds = 0;
        double Seconds = 1, HP = 1, Production = 0, ProductionCycle = 10, Capacity = 0;
        double Damage = 0, Range = 0, Splash = 0, AttackInterval = 1, RepairSeconds = 30;
        int32 BurstShots = 0;
        FString Texture;
        TArray<FRequirement> Requirements;
    };
    int64 SourceResourceCost(const FLevel& Level)
    {
        int64 Total = 0;
        for (const int64 Cost : Level.Cost) Total = BYMExperience::Add(Total, Cost);
        return Total;
    }
    struct FCombatRect { FVector2D Min = FVector2D::ZeroVector, Max = FVector2D(1, 1); };
    struct FBuildingDef
    {
        int32 Id = 0, Cells = 1, TargetFlyerMode = 1;
        FString Name, Type, Texture;
        TArray<FLevel> Levels;
        TArray<int32> Quantities;
        TArray<FCombatRect> CombatRects;
        bool bBuildable = true;
    };
    struct FMonsterStats
    {
        double HP = 1, Damage = 1, Speed = 1, Range = 0.8, Seconds = 1, AttackInterval = 1;
        double SourceSpeed = 1, SourceRange = 0;
        int32 Housing = 1, TargetGroup = 1;
        int64 Goo = 0;
    };
    struct FMonsterResearchDef { int32 FromLevel = 1, ToLevel = 2, AcademyLevel = 1; int64 Putty = 0; double Seconds = 1; };
    struct FMonsterDef : FMonsterStats
    {
        FString Id, Name, Texture;
        int32 RequiredLevel = 1;
        int64 UnlockPutty = 0;
        double UnlockSeconds = 1;
        bool bInitiallyUnlocked = false, bSourceBlocked = false, bSourceFake = false;
        bool bFlying = false;
        TArray<FMonsterStats> Levels;
        TArray<FMonsterResearchDef> Research;
    };
    struct FBuilding
    {
        FString Id;
        int32 Type = 0, Level = 1, TargetLevel = 1, Fortification = 0;
        double X = 0, Y = 0; // Original fixed territory layouts retain sub-cell source coordinates.
        double HP = 1, ReadyAt = 0, Stored = 0, ProductionRemaining = 0; // ReadyAt is legacy input only.
        FString JobId;
        int32 SpeedupRevision = 0;
        double WorkRemaining = 0;
        bool bRepairing = false;
        TMap<FString, int32> Garrison; // Ordinary Bunker stock includes living dispatched defenders.
    };
    bool HasWork(const FBuilding& B) { return B.TargetLevel > B.Level; }
    BYMEconomy::FCollectorRules CollectorRules(const FLevel& L)
    {
        BYMEconomy::FCollectorRules Rules;
        Rules.BaseCycle = L.ProductionCycle; Rules.Amount = L.Production;
        Rules.Capacity = L.Capacity; Rules.RateMultiplier = ProductionMultiplier();
        return Rules;
    }
    BYMEconomy::FRepairState RepairState(const FBuilding& B, const FLevel& L)
    {
        BYMEconomy::FRepairState State;
        State.Health = B.HP; State.MaxHealth = L.HP; State.bRepairing = B.bRepairing;
        State.Rate = BYMEconomy::RepairRate(L.HP, L.RepairSeconds, ConstructionMultiplier());
        return State;
    }
    BYMEconomy::FCollectorState CollectorState(const FBuilding& B)
    {
        BYMEconomy::FCollectorState State; State.Stored = B.Stored; State.Remaining = B.ProductionRemaining; return State;
    }
    struct FTraining
    {
        FString Id, Building, Type;
        int32 Count = 0, PriceScale = 1; // Paid before/after World Map entry; never retroactively repriced.
        double Remaining = 0, ReadyAt = 0; // ReadyAt is read only for schema-1/2 migration.
        bool bActive = false, bLegacy = false;
    };
    struct FMonsterJob
    {
        FString Type, Building;
        int32 FromLevel = 0, ToLevel = 1;
        double StartedAt = 0, ReadyAt = 0;
        int64 PuttyPaid = 0;
    };
    struct FWorker
    {
        FString Id, Kind, Task, Job;
        bool bLegacy = false;
        FVector2D Position = FVector2D::ZeroVector;
        double StartedAt = 0, ArrivalAt = 0;
        TArray<BYMWorkerMovement::FRoutePoint> Route;
    };
    struct FYard
    {
        FString Id, Name;
        bool bBot = false;
        int32 MapWidth = 0, MapHeight = 0, MapMinX = 0, MapMinY = 0; // Nonzero only for fixed MR3 sites.
        int64 RP = 0, Resource[4] = {0, 0, 0, 0};
        int64 Shiny = 0;
        int32 PurchasedWorkers = 0;
        TArray<FWorker> Workers;
#include "BYMSimulationEconomyState.inl"
        int64 EarnedExperience = 0, BaseValue = 0;
        int32 Wins = 0, Losses = 0, Decisions = 0, LayoutRevision = 0;
        double ShieldUntil = 0, LastEconomy = 0, NextDecision = 0;
        TArray<FBuilding> Buildings;
        TMap<FString, int32> Army;
        TArray<FTraining> Queue;
        TMap<FString, int32> MonsterLevels;
        FMonsterJob MonsterUnlock;
        TArray<FMonsterJob> MonsterResearch;
        TArray<FObject> Log;
    };
    struct FTroop
    {
        FString Id, Type, Target, TargetCreep;
        int32 Level = 1;
        FVector2D Position = FVector2D::ZeroVector;
        double HP = 1, AttackCooldown = 0, LastAttackAt = 0, DeployedAt = 0;
        int32 AttackSequence = 0;
        FString State = TEXT("idle");
        FVector2D Facing = FVector2D(0, 1), TargetPosition = FVector2D::ZeroVector;
        TArray<FIntPoint> Path;
        int32 PathIndex = 0;
        double RepathAt = 0;
    };
    struct FBunkerDefender : FTroop
    {
        FString Bunker;
        bool bReturned = false, bDeathHandled = false;
        double RetargetRemaining = 150.0 / 40.0;
    };
    struct FBunkerState
    {
        double DoorFrame = 0, ReleaseRemaining = BYMBunkerRules::ReleaseIntervalSeconds;
        double StateStartedAt = 0, TargetRefreshRemaining = 0;
        FString State = TEXT("closed");
        TArray<FString> Targets;
    };
    struct FDefenseVisual
    {
        FString Target;
        FVector2D Facing = FVector2D(0, 1);
        double LastAttackAt = 0;
        int32 AttackSequence = 0;
    };
    struct FDefenseBurst
    {
        FString Phase = TEXT("idle");
        double Remaining = 0, PhaseSeconds = 0, StartedAt = 0;
        int32 ShotsRemaining = 0, CoolingStartFrame = 32;
    };
#include "BYMSimulationMapCombatState.inl"
    struct FBattle
    {
        FString Id, Attacker, Defender, MapCellId;
        double StartedAt = 0, EndsAt = 0;
        int32 Deployed = 0;
        TArray<FTroop> Troops;
        TMap<FString, FMapTowerState> MapTowers;
        TArray<FBunkerDefender> Defenders;
        TMap<FString, FBunkerState> Bunkers;
        TMap<FString, double> DefenseCooldown;
        TMap<FString, FDefenseVisual> DefenseVisuals;
        TMap<FString, FDefenseBurst> DefenseBursts;
        double SimulatedAt = 0;
        int32 EventSequence = 0;
        FString EventEpoch = NewId(); // Do not reuse presentation event IDs after a host reload.
        TArray<FObject> Events; // Presentation only; damage and cooldowns remain authoritative.
        double InitialBuildingHP = 1;
        int32 InitialBuildings = 0;
    };
#include "BYMSimulationMapState.inl"
    FObject MonsterJobJSON(const FMonsterJob& Job, bool bResearch)
    {
        FObject O = Obj(); if (Job.Type.IsEmpty()) return O;
        O->SetStringField(TEXT("type"), Job.Type); O->SetNumberField(TEXT("startedAt"), Job.StartedAt);
        O->SetNumberField(TEXT("readyAt"), Job.ReadyAt); O->SetNumberField(TEXT("puttyPaid"), static_cast<double>(Job.PuttyPaid));
        if (bResearch)
        { O->SetStringField(TEXT("building"), Job.Building); O->SetNumberField(TEXT("fromLevel"), Job.FromLevel); O->SetNumberField(TEXT("toLevel"), Job.ToLevel); }
        return O;
    }
}

using namespace BYMSim;

struct FBYMSimulation::FImpl
{
    TMap<int32, FBuildingDef> Buildings;
    TMap<FString, FMonsterDef> Monsters;
    TMap<FString, FYard> Yards;
    TMap<FString, FBattle> Battles; // Key = attacking yard id. One outgoing battle per yard.
    TMap<FString, FObject> CompletedBattles; // Bounded, three-second presentation cache; never serialized.
    TArray<FString> BotIds;
    int32 BotCursor = 0, EconomyCursor = 0;
    TArray<FString> YardIds;
    double Now = 0, CombatAccumulator = 0, BotAccumulator = 0;
    bool bInitialized = false;

    int32 Cells() const { return FMath::Clamp(GetDefault<UBYMSettings>()->YardCells, 40, 100); }
    int32 MonsterLevel(const FYard& Y, const FString& Type) const { return Y.MonsterLevels.FindRef(Type); }
    const FMonsterStats& MonsterStats(const FMonsterDef& D, int32 Level) const
    {
        return D.Levels.IsEmpty() ? static_cast<const FMonsterStats&>(D) : D.Levels[FMath::Clamp(Level - 1, 0, D.Levels.Num() - 1)];
    }
    const FMonsterStats& MonsterStats(const FYard& Y, const FMonsterDef& D) const { return MonsterStats(D, MonsterLevel(Y, D.Id)); }
    bool AcademyBusy(const FYard& Y, const FString& Building) const
    {
        return Y.MonsterResearch.ContainsByPredicate([&Building](const FMonsterJob& Job) { return Job.Building == Building; });
    }
    const FBuilding* ResearchAcademy(const FYard& Y, int32 RequiredLevel) const
    {
        const FBuilding* Best = nullptr;
        for (const FBuilding& B : Y.Buildings)
            // ACADEMY.StartMonsterUpgrade is also reached through registered
            // front-page shortcuts, without BUILDINGINFO's health/work gate.
            if (B.Type == 26 && B.Level > 0 && B.Level >= RequiredLevel && !AcademyBusy(Y, B.Id)
                && (!Best || B.Level < Best->Level)) Best = &B;
        return Best;
    }
    const FLevel* Stats(const FBuilding& B) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type);
        return D && D->Levels.IsValidIndex(FMath::Max(1, B.Level) - 1) ? &D->Levels[FMath::Max(1, B.Level) - 1] : nullptr;
    }
    int32 Footprint(const FBuilding& B) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type); return D ? D->Cells : 1;
    }
    FVector2D CombatPoint(const FBuilding& B, const FVector2D& P) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type);
        FVector2D Result(B.X, B.Y); double Best = TNumericLimits<double>::Max();
        if (D && D->Type != TEXT("decoration")) for (const FCombatRect& R : D->CombatRects)
        {
            const FVector2D Min(B.X + R.Min.X, B.Y + R.Min.Y), Max(B.X + R.Max.X, B.Y + R.Max.Y);
            if (P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y) return P;
            const FVector2D Corners[4] = { Min, FVector2D(Max.X, Min.Y), Max, FVector2D(Min.X, Max.Y) };
            for (int32 I = 0; I < 4; ++I)
            {
                const FVector2D A = Corners[I], Edge = Corners[(I + 1) % 4] - A, Relative = P - A;
                const FVector2D ProjectedEdge(Edge.X - Edge.Y, (Edge.X + Edge.Y) * 0.5);
                const FVector2D ProjectedRelative(Relative.X - Relative.Y, (Relative.X + Relative.Y) * 0.5);
                const double Denominator = ProjectedEdge.X * ProjectedEdge.X + ProjectedEdge.Y * ProjectedEdge.Y;
                const double Fraction = Denominator > 0 ? FMath::Clamp((ProjectedRelative.X * ProjectedEdge.X + ProjectedRelative.Y * ProjectedEdge.Y) / Denominator, 0.0, 1.0) : 0;
                const FVector2D Point = A + Edge * Fraction;
                const double Distance = ProjectedLength(Point - P);
                if (Distance < Best) { Best = Distance; Result = Point; }
            }
        }
        return Result;
    }
    double CombatDistance(const FBuilding& B, const FVector2D& P) const { return ProjectedLength(CombatPoint(B, P) - P); }
    FVector2D CombatCenter(const FBuilding& B) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type);
        if (D && !D->CombatRects.IsEmpty()) return FVector2D(B.X, B.Y) + (D->CombatRects[0].Min + D->CombatRects[0].Max) * 0.5;
        return FVector2D(B.X + 0.5, B.Y + 0.5);
    }
    bool IsCombatObjective(const FBuilding& B) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type);
        return D && D->Type != TEXT("decoration") && D->Type != TEXT("immovable") && D->Type != TEXT("enemy") && D->Type != TEXT("trap") && D->Type != TEXT("cage");
    }
    bool InsideCombat(const FBuilding& B, const FVector2D& P) const
    {
        const FBuildingDef* D = Buildings.Find(B.Type);
        if (D) for (const FCombatRect& R : D->CombatRects)
            if (P.X > B.X + R.Min.X + 0.0001 && P.X < B.X + R.Max.X - 0.0001 && P.Y > B.Y + R.Min.Y + 0.0001 && P.Y < B.Y + R.Max.Y - 0.0001) return true;
        return false;
    }
    bool ClearSegment(const FYard& Y, const FVector2D& Start, const FVector2D& End, const FString& Ignore = FString()) const
    {
        for (const FBuilding& B : Y.Buildings) if (B.HP > 0 && B.Id != Ignore)
        {
            const FBuildingDef* D = Buildings.Find(B.Type); if (!D || D->Type == TEXT("decoration")) continue;
            for (const FCombatRect& R : D->CombatRects)
                if (SegmentHitsRect(Start, End, FVector2D(B.X, B.Y) + R.Min, FVector2D(B.X, B.Y) + R.Max)) return false;
        }
        return true;
    }
    bool CanAttack(const FYard& Y, const FBuilding& B, const FVector2D& P, double Range) const
    { return CombatDistance(B, P) <= Range + 0.0001 && ClearSegment(Y, P, CombatPoint(B, P), B.Id); }
    void CombatEvent(FBattle& B, const TCHAR* Kind, const FString& Actor, const FString& Target,
        const FVector2D& Source, const FVector2D& Destination, const FString& MonsterType, const FBuilding& Building, bool bRanged, double Damage)
    {
        FObject E = Obj(); E->SetStringField(TEXT("id"), B.Id + TEXT(":") + B.EventEpoch + TEXT(":") + FString::FromInt(++B.EventSequence));
        E->SetStringField(TEXT("kind"), Kind); E->SetNumberField(TEXT("at"), B.SimulatedAt);
        E->SetStringField(TEXT("actor"), Actor); E->SetStringField(TEXT("target"), Target);
        E->SetNumberField(TEXT("sourceX"), Source.X); E->SetNumberField(TEXT("sourceY"), Source.Y);
        E->SetNumberField(TEXT("targetX"), Destination.X); E->SetNumberField(TEXT("targetY"), Destination.Y);
        E->SetStringField(TEXT("monsterType"), MonsterType); E->SetNumberField(TEXT("buildingType"), Building.Type);
        E->SetNumberField(TEXT("buildingLevel"), Building.Level); E->SetBoolField(TEXT("ranged"), bRanged);
        E->SetNumberField(TEXT("damage"), Damage); E->SetNumberField(TEXT("travelSeconds"), 0);
        B.Events.Add(E);
        if (B.Events.Num() > 128) B.Events.RemoveAt(0, B.Events.Num() - 128);
    }
    FBuilding* FindBuilding(FYard& Y, const FString& Id)
    {
        return Y.Buildings.FindByPredicate([&Id](const FBuilding& B) { return B.Id == Id; });
    }
    const FBuilding* FindBuilding(const FYard& Y, const FString& Id) const
    {
        return Y.Buildings.FindByPredicate([&Id](const FBuilding& B) { return B.Id == Id; });
    }
    int32 LevelOf(const FYard& Y, int32 Type) const
    {
        int32 Result = 0;
        for (const FBuilding& B : Y.Buildings) if (B.Type == Type && B.HP > 0 && !HasWork(B) && !B.bRepairing) Result = FMath::Max(Result, B.Level);
        return Result;
    }
    int32 AttainedLevel(const FYard& Y, int32 Type) const
    {
        int32 Result = 0;
        for (const FBuilding& B : Y.Buildings) if (B.Type == Type) Result = FMath::Max(Result, B.Level);
        return Result;
    }
    int32 CountOf(const FYard& Y, int32 Type, int32 Level = 0) const
    {
        int32 Result = 0;
        for (const FBuilding& B : Y.Buildings) if (B.Type == Type && (Level == 0 || B.Level >= Level)) ++Result;
        return Result;
    }
    bool UnderAttack(const FString& Id) const
    {
        for (const TPair<FString, FBattle>& P : Battles) if (P.Value.Defender == Id) return true;
        return false;
    }
    bool Busy(const FString& Id) const { return Battles.Contains(Id) || UnderAttack(Id); }
    bool ActiveBot(const FString& Id) const
    {
        const int32 Index = BotIds.Find(Id);
        return Index != INDEX_NONE && Index < FMath::Clamp(GetDefault<UBYMSettings>()->RivalBotCount, 0, 10000);
    }
    bool CanAfford(const FYard& Y, const FLevel& L) const
    {
        for (int32 I = 0; I < 4; ++I) if (Y.Resource[I] < L.Cost[I]) return false; return true;
    }
    void Pay(FYard& Y, const FLevel& L) { for (int32 I = 0; I < 4; ++I) Y.Resource[I] -= L.Cost[I]; }
    bool RequirementsMet(const FYard& Y, const FLevel& L) const
    {
        for (const FRequirement& R : L.Requirements) if (CountOf(Y, R.Type, R.Level) < R.Count) return false;
        return true;
    }
    bool ConstructionBusy(const FYard& Y) const { return AvailableWorkers(Y) <= 0; }
    int32 QuantityLimit(const FYard& Y, const FBuildingDef& D) const
    {
        return D.Quantities.IsEmpty() ? FMath::Clamp(GetDefault<UBYMSettings>()->MaxBuildingsPerYard, 1, 1000)
            : D.Quantities[FMath::Clamp(AttainedLevel(Y, 14), 0, D.Quantities.Num() - 1)];
    }
    int32 NextQuantityLevel(const FYard& Y, const FBuildingDef& D) const
    {
        const int32 Owned = CountOf(Y, D.Id);
        for (int32 Level = AttainedLevel(Y, 14) + 1; Level < D.Quantities.Num(); ++Level)
            if (D.Quantities[Level] > Owned) return Level;
        return -1;
    }
    int64 FlingerCapacity(const FYard& Y) const
    {
        int64 Capacity = 0;
        for (const FBuilding& B : Y.Buildings) if (B.Type == 5 && B.HP > 0 && !HasWork(B) && !B.bRepairing)
        { const FLevel* L = Stats(B); if (L) Capacity = FMath::Max(Capacity, static_cast<int64>(L->Capacity)); }
        return Capacity;
    }
    int64 FlingerUsed(const FBattle& B) const
    {
        int64 Used = 0;
        // Original ATTACK.USE_CUMULATIVE_FLINGER_CAPACITY includes defeated units.
        for (const FTroop& T : B.Troops)
        { const FMonsterDef* M = Monsters.Find(T.Type); if (M) Used += MonsterStats(*M, T.Level).Housing; }
        return Used;
    }
    bool HasDeployableReserves(const FBattle& B) const;
    FString MissingRequirements(const FYard& Y, const FLevel& L) const;
    FString MissingResources(const FYard& Y, const FLevel& L) const;
    FString BuildReason(const FYard& Y, const FBuildingDef& D) const;
    FString UpgradeReason(const FYard& Y, const FBuilding& B) const;
    FObject BuildOptions(const FYard& Y) const;
    FString RepairReason(const FYard& Y, const FBuilding& B) const;
    FString CancelWorkReason(const FYard& Y, const FBuilding& B) const;
    FString MoveReason(const FYard& Y, const FBuilding& B) const;
    void WorkRefund(const FYard& Y, const FBuilding& B, int64 (&Refund)[4]) const;
    double WorkReadyAt(const FYard& Y, const FBuilding& B) const;
    double RepairReadyAt(const FBuilding& B) const;
    FString UnlockReason(const FYard& Y, const FMonsterDef& D) const;
    FString ResearchReason(const FYard& Y, const FMonsterDef& D) const;
    FString TrainReason(const FYard& Y, const FMonsterDef& D, const FString& Building = FString()) const;
    int32 MaxTrainCount(const FYard& Y, const FMonsterDef& D, const FString& Building = FString()) const;
    const FBuilding* SelectHatchery(const FYard& Y, const FMonsterDef& D, const FString& Requested) const;
    FString HatcheryPauseReason(const FBuilding& B) const;
    int32 HatcherySpace(const FYard& Y, const FBuilding& B, const FString& Type) const;
    int32 QueuedMonsters(const FYard& Y) const;
    void AdvanceHatcheries(FYard& Y, double Elapsed);
    void DestroyHatcheryQueue(FYard& Y, const FString& BuildingId);
    FArray HatcheriesJSON(const FYard& Y, bool bSave) const;
    FObject MonsterOptions(const FYard& Y) const;
    FObject MonsterStatsJSON(const FMonsterDef& D, int32 Level, int32 PriceScale = 1) const;
    int64 Storage(const FYard& Y) const
    {
        double Capacity = FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->ResourceStorageFloor), 0.0, ResourceLimit);
        for (const FBuilding& B : Y.Buildings) if (B.Type == 6 && B.Level > 0)
        {
            const FLevel* L = Stats(B); if (L) Capacity += BYMEconomy::StorageContribution(B.Level, L->Capacity);
        }
        return static_cast<int64>(FMath::Clamp(Capacity * (1.0 + 0.1 * FMath::Clamp(Y.PackingLevel, 0, 10)) + static_cast<double>(MapStorage(Y.Id)), 0.0, ResourceLimit));
    }
    void RefreshBaseValue(FYard& Y) const
    {
        int64 Total = 0;
        for (const FBuilding& B : Y.Buildings)
        {
            const FBuildingDef* D = Buildings.Find(B.Type);
            if (!D || B.Level <= 0 || !D->Levels.IsValidIndex(B.Level - 1)
                || D->Type == TEXT("decoration") || D->Type == TEXT("enemy")
                || D->Type == TEXT("immovable") || D->Type == TEXT("trap")) continue;
            const FLevel& Completed = D->Levels[B.Level - 1];
            Total = BYMExperience::Add(Total, BYMExperience::Add(Completed.SourceSeconds, SourceResourceCost(Completed)));
        }
        // Round the sum once and retain the highest completed main-yard value.
        // Health and pending upgrades do not remove an attained level.
        Y.BaseValue = BYMExperience::RetainBaseValue(Y.BaseValue, Total);
    }
    FObject ExperienceJSON(const FYard& Y) const
    {
        const BYMExperience::FProgress Progress = BYMExperience::Progress(Y.EarnedExperience, Y.BaseValue);
        FObject Result = Obj();
        Result->SetNumberField(TEXT("level"), Progress.Level); Result->SetNumberField(TEXT("maxLevel"), Progress.MaxLevel);
        Result->SetNumberField(TEXT("earned"), static_cast<double>(Progress.Earned));
        Result->SetNumberField(TEXT("baseValue"), static_cast<double>(Progress.BaseValue));
        Result->SetNumberField(TEXT("points"), static_cast<double>(Progress.Points));
        Result->SetNumberField(TEXT("lower"), static_cast<double>(Progress.Lower));
        Result->SetNumberField(TEXT("upper"), static_cast<double>(Progress.Upper));
        Result->SetNumberField(TEXT("needed"), static_cast<double>(Progress.Needed));
        Result->SetNumberField(TEXT("progress"), Progress.Fraction); Result->SetBoolField(TEXT("atMaxLevel"), Progress.bAtMaxLevel);
        return Result;
    }
    int32 Housing(const FYard& Y) const
    {
        double Capacity = 0;
        for (const FBuilding& B : Y.Buildings) if (B.Type == 15 && B.Level > 0 && B.HP > 10)
        { const FLevel* L = Stats(B); if (L) Capacity += L->Capacity; }
        return static_cast<int32>(FMath::Clamp(Capacity, 0.0, 100000.0));
    }
    int32 UsedHousing(const FYard& Y) const
    {
        int64 Used = 0;
        for (const TPair<FString, int32>& P : Y.Army) { const FMonsterDef* D = Monsters.Find(P.Key); if (D) Used += static_cast<int64>(MonsterStats(Y, *D).Housing) * P.Value; }
        return static_cast<int32>(FMath::Min<int64>(Used, 1000000));
    }
    int32 ArmyCount(const FYard& Y) const { int32 N = 0; for (const TPair<FString, int32>& P : Y.Army) N += P.Value; return N; }
    bool Fits(const FYard& Y, int32 X, int32 PosY, int32 Size, const FString& Ignore = FString()) const
    {
        if (X < YardMinX(Y) + 1 || PosY < YardMinY(Y) + 1 || X + Size >= YardMinX(Y) + YardWidth(Y) || PosY + Size >= YardMinY(Y) + YardHeight(Y)) return false;
        if (!EconomyFits(Y, X, PosY, Size, Ignore)) return false;
        for (const FBuilding& B : Y.Buildings)
        {
            if (B.Id == Ignore) continue;
            // Original MR3 core collision squares are 130 source pixels,
            // independent of the catalog's nominal 100-pixel icon footprint.
            const double S = (B.Type == 138 || B.Type == 139 || B.Type == 140) ? 6.5 : static_cast<double>(Footprint(B));
            if (X < B.X + S && X + Size > B.X && PosY < B.Y + S && PosY + Size > B.Y) return false;
        }
        return true;
    }
    bool FreeSpot(const FYard& Y, int32 Size, int32& X, int32& PosY, int32 Type = 0) const
    {
        // Scan outwards from the central town to leave an outer deployment band free.
        const int32 MinX = YardMinX(Y), MinY = YardMinY(Y), Width = YardWidth(Y), Height = YardHeight(Y);
        if (Y.bBot)
        {
            const BYMBotLayout::FPlan Plan = BYMBotLayout::MakePlan(Y.Id, Type, CountOf(Y, Type), FIntRect(MinX, MinY, MinX + Width, MinY + Height), Size);
            if (!Plan.bValid) return false;
            bool bFound = false; uint64 Best = TNumericLimits<uint64>::Max();
            for (int32 SY = MinY + 4; SY <= MinY + Height - Size - 4; ++SY) for (int32 SX = MinX + 4; SX <= MinX + Width - Size - 4; ++SX)
                if (Fits(Y, SX, SY, Size)) { const uint64 Score = BYMBotLayout::RankCandidate(Plan, SX, SY); if (!bFound || Score < Best) { Best = Score; X = SX; PosY = SY; bFound = true; } }
            return bFound;
        }
        const int32 MidX = MinX + Width / 2, MidY = MinY + Height / 2;
        for (int32 Radius = 0; Radius < FMath::Max(Width, Height); ++Radius)
            for (int32 SY = FMath::Max(MinY + 4, MidY - Radius); SY <= FMath::Min(MinY + Height - Size - 4, MidY + Radius); ++SY)
                for (int32 SX = FMath::Max(MinX + 4, MidX - Radius); SX <= FMath::Min(MinX + Width - Size - 4, MidX + Radius); ++SX)
                    if (Fits(Y, SX, SY, Size)) { X = SX; PosY = SY; return true; }
        return false;
    }
    bool MigrateBotLayout(FYard& Y);
    bool BotLayoutMove(FYard& Y);
    bool LoadCatalog(FString& Error);
    bool LoadSave(const FObject& Saved, FString& Error);
    FYard NewYard(const FString& Id, const FString& Name, bool bBot);
    void Advance(FYard& Y, double Until = -1.);
    FObject YardJSON(const FYard& Y, bool bSave, bool bPrivate = true) const;
    FObject BattleJSON(const FBattle& B, bool bSave) const;
    FString Execute(const FString& Id, const FString& Op, const FObject& P);
    void PruneCompletedBattles()
    {
        TArray<FString> Keys; CompletedBattles.GetKeys(Keys);
        for (const FString& Key : Keys) if (Number(CompletedBattles.FindChecked(Key), TEXT("finishedAt")) + 3.0 < Now) CompletedBattles.Remove(Key);
        while (CompletedBattles.Num() >= 100)
        {
            FString Oldest; double At = TNumericLimits<double>::Max();
            for (const TPair<FString, FObject>& P : CompletedBattles)
                if (Number(P.Value, TEXT("finishedAt")) < At) { At = Number(P.Value, TEXT("finishedAt")); Oldest = P.Key; }
            if (Oldest.IsEmpty()) break;
            CompletedBattles.Remove(Oldest);
        }
    }
    FVector2D BunkerDoorPoint(const FBuilding& Building, const FVector2D& DirectionPoint) const
    {
        // BUILDING22.EjectCreeps / CreepBase.changeModeBunker share the
        // target-quadrant exit in original projected pixels.
        const FVector2D Relative = DirectionPoint - FVector2D(Building.X, Building.Y);
        const double X = Relative.X - Relative.Y, Y = (Relative.X + Relative.Y) * .5;
        const double Width = 90., Height = 90.;
        const double DoorX = X <= 0 ? (Y <= 0 ? -Width / 3. : -Width / 4.) : Width / 2.;
        const double DoorY = Y <= 0 ? Height / 4. : Height / 2.;
        return FVector2D(Building.X, Building.Y) + FVector2D(DoorY + DoorX * .5, DoorY - DoorX * .5) / SourcePixelsPerCell;
    }
    int32 BunkerUsed(const FYard& Y, const FBuilding& Building) const;
    FString BunkerManageReason(const FYard& Y, const FBuilding& Building) const;
    FArray BunkersJSON(const FYard& Y) const;
    void BuildUnitPath(FTroop& Unit, const FYard& Yard, const FVector2D& Destination, double Range, const FString& Ignore);
    bool MoveToUnit(FTroop& Unit, const FYard& Yard, const FVector2D& Destination, double Range, double Speed, double Step, const FString& Ignore);
    void TickBunkers(FBattle& B, FYard& Yard, double Step);
    void RemoveBunkerDefender(FBattle& B, FYard& Yard, FBunkerDefender& Unit);
    void DestroyBattleBuilding(FBattle& B, FYard& Yard, FBuilding& Victim, const FTroop& Source, double AppliedDamage);
    void ExplodeAttacker(FBattle& B, FYard& Yard, FTroop& Attacker, const FString& PrimaryDefender = FString());
    bool FightDefender(FBattle& B, FYard& Yard, FTroop& Attacker, double Step);
    void TickBattle(FBattle& B, double Step);
    void EndBattle(const FString& AttackerId, bool bAttackerWon, const FString& Reason, bool bContested = true);
    void BotAction(FYard& Y);
    void BuildPath(FTroop& T, const FYard& Defender, const FBuilding& Target, double Range);
    int32 WorkerCapacity(const FYard& Y) const { return 1 + FMath::Clamp(Y.PurchasedWorkers, 0, 4); }
    int32 AvailableWorkers(const FYard& Y) const;
    bool NeedsWorker(const FBuilding& B) const;
    FVector2D WorkerPosition(const FWorker& W, double At) const;
    FVector2D BuildingWorkerPoint(const FBuilding& B) const;
    void SyncWorkers(FYard& Y, double At);
    void CompleteBuildingWork(FYard& Y, FBuilding& B, double At);
    void MigrateWorkers(FYard& Y);
    bool ReadWorkers(FYard& Y, const FObject& Source, FString& Error);
    FObject WorkersJSON(const FYard& Y, bool bSave, bool bPrivate = true) const;
    bool AssignWorker(FYard& Y, const FString& Kind, const FString& Task, const FString& Job, const FVector2D& Target, double& OutArrivalAt);
    void ReleaseWorker(FYard& Y, const FString& Kind, const FString& Task, double At);
    bool BuildingWorkerReady(const FYard& Y, const FBuilding& B, double At) const;
    bool AssignMushroomWorker(FYard& Y, const FString& Id, const FVector2D& Position, double& OutArrivalAt);
    bool IsMushroomWorkerArrived(const FYard& Y, const FString& Id, double At) const;
    void ReleaseMushroomWorker(FYard& Y, const FString& Id, double At);
#include "BYMSimulationEconomyDeclarations.inl"
#include "BYMSimulationMapDeclarations.inl"
};

#include "BYMSimulationEconomy.inl"

int32 FBYMSimulation::FImpl::AvailableWorkers(const FYard& Y) const
{
    int32 BusyCount = 0;
    for (const FWorker& W : Y.Workers) if (!W.Kind.IsEmpty()) ++BusyCount;
    return FMath::Max(0, WorkerCapacity(Y) - BusyCount);
}

bool FBYMSimulation::FImpl::NeedsWorker(const FBuilding& B) const
{
    if (!HasWork(B)) return false;
    const FBuildingDef* D = Buildings.Find(B.Type);
    // Source BASE.addBuildingB exempts instant construction, not ordinary upgrades.
    return B.Level > 0 || !D || D->Levels.IsEmpty() || D->Levels[0].SourceSeconds > 0;
}

FVector2D FBYMSimulation::FImpl::WorkerPosition(const FWorker& W, double At) const
{
    return W.Route.IsEmpty() ? W.Position : BYMWorkerMovement::SampleRoute(W.Route, FMath::Max(0.0, At - W.StartedAt)).Position;
}

FVector2D FBYMSimulation::FImpl::BuildingWorkerPoint(const FBuilding& B) const
{
    return CombatCenter(B);
}

void FBYMSimulation::FImpl::SyncWorkers(FYard& Y, double At)
{
    Y.Workers.RemoveAll([](const FWorker& W) { return W.bLegacy && W.Kind.IsEmpty(); });
    for (int32 Index = 1; Index <= WorkerCapacity(Y); ++Index)
    {
        const FString Id = TEXT("worker_") + FString::FromInt(Index);
        if (Y.Workers.ContainsByPredicate([&](const FWorker& W) { return W.Id == Id; })) continue;
        FWorker W; W.Id = Id;
        const uint32 Seed = GetTypeHash(Y.Id + Id);
        W.Position = FVector2D(YardMinX(Y) + 2.5 + (Seed % FMath::Max(1, YardWidth(Y) - 5)),
            YardMinY(Y) + 2.5 + ((Seed / 997U) % FMath::Max(1, YardHeight(Y) - 5)));
        W.StartedAt = At; W.ArrivalAt = At;
        Y.Workers.Add(MoveTemp(W));
    }
}

void FBYMSimulation::FImpl::MigrateWorkers(FYard& Y)
{
    Y.Workers.Reset(); SyncWorkers(Y, Y.LastEconomy);
    for (const FBuilding& B : Y.Buildings) if (NeedsWorker(B))
    {
        FWorker* W = Y.Workers.FindByPredicate([](const FWorker& Entry) { return Entry.Kind.IsEmpty() && !Entry.bLegacy; });
        if (!W)
        {
            FWorker Legacy; Legacy.Id = TEXT("legacy_") + B.JobId; Legacy.bLegacy = true;
            Y.Workers.Add(MoveTemp(Legacy)); W = &Y.Workers.Last();
        }
        W->Kind = B.Level == 0 ? TEXT("construction") : TEXT("upgrade"); W->Task = B.Id; W->Job = B.JobId;
        W->Position = BuildingWorkerPoint(B); W->StartedAt = Y.LastEconomy; W->ArrivalAt = Y.LastEconomy;
    }
}

bool FBYMSimulation::FImpl::AssignWorker(FYard& Y, const FString& Kind, const FString& Task, const FString& Job, const FVector2D& Target, double& OutArrivalAt)
{
    SyncWorkers(Y, Now);
    if (AvailableWorkers(Y) <= 0 || Task.IsEmpty() || Job.IsEmpty()) return false;
    FWorker* Selected = nullptr; double Closest = TNumericLimits<double>::Max();
    for (FWorker& W : Y.Workers) if (!W.bLegacy && W.Kind.IsEmpty())
    {
        const double Distance = ProjectedLength(WorkerPosition(W, Now) - Target);
        if (Distance < Closest) { Closest = Distance; Selected = &W; }
    }
    if (!Selected) return false;
    TArray<FIntRect> Occupied;
    for (const FBuilding& B : Y.Buildings)
    {
        const FBuildingDef* D = Buildings.Find(B.Type); if (!D || D->Type == TEXT("decoration") || B.HP <= 0) continue;
        for (const FCombatRect& R : D->CombatRects)
            Occupied.Add(FIntRect(FMath::FloorToInt(B.X + R.Min.X), FMath::FloorToInt(B.Y + R.Min.Y),
                FMath::CeilToInt(B.X + R.Max.X), FMath::CeilToInt(B.Y + R.Max.Y)));
    }
    const FVector2D Start = WorkerPosition(*Selected, Now);
    TArray<BYMWorkerMovement::FRoutePoint> Route; double Duration = 0;
    const FIntRect Bounds(YardMinX(Y), YardMinY(Y), YardMinX(Y) + YardWidth(Y), YardMinY(Y) + YardHeight(Y));
    if (!BYMWorkerMovement::PlanRoute(Start, Target, Bounds, Occupied, Route, Duration)) return false;
    Selected->Kind = Kind; Selected->Task = Task; Selected->Job = Job; Selected->Position = Start;
    Selected->StartedAt = Now; Selected->ArrivalAt = Now + Duration; Selected->Route = MoveTemp(Route);
    OutArrivalAt = Selected->ArrivalAt; return true;
}

void FBYMSimulation::FImpl::ReleaseWorker(FYard& Y, const FString& Kind, const FString& Task, double At)
{
    for (FWorker& W : Y.Workers) if (W.Kind == Kind && W.Task == Task)
    {
        W.Position = WorkerPosition(W, At); W.Kind.Reset(); W.Task.Reset(); W.Job.Reset(); W.Route.Reset();
        W.StartedAt = At; W.ArrivalAt = At;
    }
    SyncWorkers(Y, At);
}

bool FBYMSimulation::FImpl::BuildingWorkerReady(const FYard& Y, const FBuilding& B, double At) const
{
    if (!NeedsWorker(B)) return true;
    return Y.Workers.ContainsByPredicate([&](const FWorker& W)
        { return W.Task == B.Id && W.Job == B.JobId && (W.Kind == TEXT("construction") || W.Kind == TEXT("upgrade")) && W.ArrivalAt <= At + .000001; });
}

bool FBYMSimulation::FImpl::AssignMushroomWorker(FYard& Y, const FString& Id, const FVector2D& Position, double& OutArrivalAt)
{
    return AssignWorker(Y, TEXT("mushroom"), Id, TEXT("mushroom:") + Id, Position, OutArrivalAt);
}

bool FBYMSimulation::FImpl::IsMushroomWorkerArrived(const FYard& Y, const FString& Id, double At) const
{
    return Y.Workers.ContainsByPredicate([&](const FWorker& W) { return W.Kind == TEXT("mushroom") && W.Task == Id && W.ArrivalAt <= At + .000001; });
}

void FBYMSimulation::FImpl::ReleaseMushroomWorker(FYard& Y, const FString& Id, double At)
{
    ReleaseWorker(Y, TEXT("mushroom"), Id, At);
}

FObject FBYMSimulation::FImpl::WorkersJSON(const FYard& Y, bool bSave, bool bPrivate) const
{
    FObject O = Obj(); O->SetNumberField(TEXT("version"), 1);
    FArray Entries; int32 BusyCount = 0, LegacyBusy = 0;
    for (const FWorker& W : Y.Workers)
    {
        const bool Active = !W.Kind.IsEmpty(); if (Active) { ++BusyCount; if (W.bLegacy) ++LegacyBusy; }
        const FVector2D Position = bSave ? W.Position : WorkerPosition(W, Now);
        FObject Item = Obj(); Item->SetStringField(TEXT("id"), W.Id); Item->SetBoolField(TEXT("legacy"), W.bLegacy);
        Item->SetStringField(TEXT("kind"), W.Kind); Item->SetStringField(TEXT("task"), W.Task);
        Item->SetNumberField(TEXT("x"), Position.X); Item->SetNumberField(TEXT("y"), Position.Y);
        Item->SetNumberField(TEXT("startedAt"), W.StartedAt); Item->SetNumberField(TEXT("arrivalAt"), W.ArrivalAt);
        if (bSave || bPrivate) Item->SetStringField(TEXT("job"), W.Job);
        FArray Route;
        for (const BYMWorkerMovement::FRoutePoint& Point : W.Route)
        {
            FObject P = Obj(); P->SetNumberField(TEXT("x"), Point.Position.X); P->SetNumberField(TEXT("y"), Point.Position.Y);
            P->SetNumberField(TEXT("at"), Point.AtSeconds); P->SetBoolField(TEXT("hop"), Point.bHop); Route.Add(MakeShared<FJsonValueObject>(P));
        }
        Item->SetArrayField(TEXT("route"), Route);
        if (!bSave)
        {
            const FBuilding* B = Active && W.Kind != TEXT("mushroom") ? FindBuilding(Y, W.Task) : nullptr;
            const FLevel* Level = B ? Stats(*B) : nullptr;
            FString State = Active ? (W.ArrivalAt > Now ? TEXT("walking") : TEXT("working")) : TEXT("idle");
            if (B && W.ArrivalAt <= Now && Level && B->HP < Level->HP) State = B->bRepairing ? TEXT("repairing") : TEXT("damaged");
            Item->SetStringField(TEXT("state"), State); Item->SetStringField(TEXT("building"), B ? B->Id : FString());
            const FVector2D Target = W.Route.IsEmpty() ? Position : W.Route.Last().Position;
            const FVector2D Facing = W.Route.IsEmpty() ? FVector2D(0, 1) : BYMWorkerMovement::SampleRoute(W.Route, FMath::Max(0.0, Now - W.StartedAt)).Facing;
            Item->SetNumberField(TEXT("targetX"), Target.X); Item->SetNumberField(TEXT("targetY"), Target.Y);
            Item->SetNumberField(TEXT("facingX"), Facing.X); Item->SetNumberField(TEXT("facingY"), Facing.Y);
            const FYard::FMushroom* Mushroom = W.Kind == TEXT("mushroom") ? Y.Mushrooms.FindByPredicate([&](const FYard::FMushroom& M) { return M.Id == W.Task; }) : nullptr;
            const double Remaining = B ? B->WorkRemaining : (Mushroom ? FMath::Max(0.0, Mushroom->PickFinishAt - Now) : 0);
            Item->SetNumberField(TEXT("remaining"), Remaining); Item->SetNumberField(TEXT("readyAt"), B ? WorkReadyAt(Y, *B) : (Mushroom ? Mushroom->PickFinishAt : 0));
        }
        Entries.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("entries"), Entries);
    if (!bSave)
    {
        O->SetNumberField(TEXT("total"), WorkerCapacity(Y)); O->SetNumberField(TEXT("max"), 5);
        O->SetNumberField(TEXT("busy"), BusyCount); O->SetNumberField(TEXT("available"), AvailableWorkers(Y));
        O->SetNumberField(TEXT("legacyBusy"), LegacyBusy);
    }
    return O;
}

bool FBYMSimulation::FImpl::ReadWorkers(FYard& Y, const FObject& Source, FString& Error)
{
    auto Fail = [&Error](const TCHAR* Reason) { Error = FString(TEXT("World worker state is invalid: ")) + Reason + TEXT(". Original save has been preserved."); return false; };
    if (!Source->HasField(TEXT("workerState")))
    {
        if (Y.Mushrooms.ContainsByPredicate([](const FYard::FMushroom& M) { return M.bPicking; })) return Fail(TEXT("picked mushroom has no saved worker assignments"));
        MigrateWorkers(Y); return true;
    }
    const FObject State = Child(Source, TEXT("workerState")); int32 Version = 0; const FArray* Entries = nullptr;
    if (!State || !Integer(State, TEXT("version"), 1, 1, Version) || !State->TryGetArrayField(TEXT("entries"), Entries) || !Entries || Entries->Num() > 1005)
        return Fail(TEXT("missing or oversized worker assignments"));
    TSet<FString> Ids, Jobs;
    auto ValidPosition = [&](const FVector2D& P) { return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y)
        && P.X >= YardMinX(Y) - 20 && P.Y >= YardMinY(Y) - 20 && P.X <= YardMinX(Y) + YardWidth(Y) + 20 && P.Y <= YardMinY(Y) + YardHeight(Y) + 20; };
    for (const TSharedPtr<FJsonValue>& Entry : *Entries)
    {
        if (!Entry.IsValid() || Entry->Type != EJson::Object) return Fail(TEXT("invalid worker record"));
        const FObject O = Entry->AsObject(); FWorker W;
        if (!O->TryGetStringField(TEXT("id"), W.Id) || !O->TryGetStringField(TEXT("kind"), W.Kind)
            || !O->TryGetStringField(TEXT("task"), W.Task) || !O->TryGetStringField(TEXT("job"), W.Job)) return Fail(TEXT("invalid worker identity field type"));
        W.Position = FVector2D(Number(O, TEXT("x"), 1e9), Number(O, TEXT("y"), 1e9));
        W.StartedAt = Number(O, TEXT("startedAt"), -1); W.ArrivalAt = Number(O, TEXT("arrivalAt"), -1);
        if (W.Id.IsEmpty() || W.Id.Len() > 100 || Ids.Contains(W.Id) || !O->TryGetBoolField(TEXT("legacy"), W.bLegacy)
            || !ValidPosition(W.Position) || W.StartedAt < 0 || W.ArrivalAt < W.StartedAt || W.ArrivalAt - W.StartedAt > 3600 || W.ArrivalAt > Now + 86400)
            return Fail(TEXT("invalid worker identity, position, or travel clock"));
        if (!W.bLegacy)
        {
            bool ValidId = false;
            for (int32 Index = 1; Index <= WorkerCapacity(Y); ++Index) if (W.Id == TEXT("worker_") + FString::FromInt(Index)) ValidId = true;
            if (!ValidId) return Fail(TEXT("worker exceeds purchased capacity"));
        }
        if (W.Kind.IsEmpty())
        {
            if (W.bLegacy || !W.Task.IsEmpty() || !W.Job.IsEmpty()) return Fail(TEXT("idle worker has a paid task"));
        }
        else
        {
            if (W.Job.IsEmpty() || W.Job.Len() > 100 || Jobs.Contains(W.Job)) return Fail(TEXT("duplicate or missing worker job"));
            if (W.Kind == TEXT("mushroom"))
            {
                if (W.bLegacy || W.Job != TEXT("mushroom:") + W.Task || !HasMushroomTask(Y, W.Task)) return Fail(TEXT("orphan mushroom worker"));
            }
            else
            {
                const FBuilding* B = FindBuilding(Y, W.Task);
                if (!B || !NeedsWorker(*B) || B->JobId != W.Job || W.Kind != (B->Level == 0 ? TEXT("construction") : TEXT("upgrade")))
                    return Fail(TEXT("worker does not match paid building work"));
                if (W.bLegacy && W.Id != TEXT("legacy_") + W.Job) return Fail(TEXT("invalid legacy worker identity"));
            }
            Jobs.Add(W.Job);
        }
        const FArray* Route = nullptr;
        if (!O->TryGetArrayField(TEXT("route"), Route) || !Route || Route->Num() > 2048) return Fail(TEXT("invalid worker route"));
        double Previous = -1;
        for (const TSharedPtr<FJsonValue>& Point : *Route)
        {
            if (!Point.IsValid() || Point->Type != EJson::Object) return Fail(TEXT("invalid worker waypoint"));
            const FObject P = Point->AsObject(); BYMWorkerMovement::FRoutePoint R;
            R.Position = FVector2D(Number(P, TEXT("x"), 1e9), Number(P, TEXT("y"), 1e9)); R.AtSeconds = Number(P, TEXT("at"), -1);
            if (!ValidPosition(R.Position) || !P->TryGetBoolField(TEXT("hop"), R.bHop) || R.AtSeconds < 0 || R.AtSeconds > 3600
                || (Previous < 0 ? R.AtSeconds != 0 : R.AtSeconds <= Previous)) return Fail(TEXT("invalid worker waypoint timing"));
            Previous = R.AtSeconds; W.Route.Add(R);
        }
        if ((!W.Route.IsEmpty() && ((W.Route[0].Position - W.Position).Size() > .0001 || FMath::Abs(W.Route.Last().AtSeconds - (W.ArrivalAt - W.StartedAt)) > .001))
            || (W.Route.IsEmpty() && W.ArrivalAt != W.StartedAt) || (W.Kind.IsEmpty() && !W.Route.IsEmpty())) return Fail(TEXT("inconsistent worker route or idle state"));
        if (!W.Kind.IsEmpty())
        {
            FVector2D Expected;
            if (W.Kind == TEXT("mushroom"))
            {
                const FYard::FMushroom* M = Y.Mushrooms.FindByPredicate([&](const FYard::FMushroom& Candidate) { return Candidate.Id == W.Task; });
                if (!M || FMath::Abs(M->PickFinishAt - (W.ArrivalAt + BYMStoreRules::PickSeconds)) > .001) return Fail(TEXT("mushroom work clock does not match worker arrival"));
                Expected = FVector2D(M->X + .75, M->Y + .75);
            }
            else Expected = BuildingWorkerPoint(*FindBuilding(Y, W.Task));
            const FVector2D Destination = W.Route.IsEmpty() ? W.Position : W.Route.Last().Position;
            if ((Destination - Expected).Size() > .0001) return Fail(TEXT("worker route does not reach its assigned task"));
        }
        Ids.Add(W.Id); Y.Workers.Add(MoveTemp(W));
    }
    for (int32 Index = 1; Index <= WorkerCapacity(Y); ++Index) if (!Ids.Contains(TEXT("worker_") + FString::FromInt(Index))) return Fail(TEXT("missing purchased worker"));
    for (const FBuilding& B : Y.Buildings) if (NeedsWorker(B) && !Jobs.Contains(B.JobId)) return Fail(TEXT("paid building work has no assigned worker"));
    for (const FYard::FMushroom& M : Y.Mushrooms) if (M.bPicking && !Jobs.Contains(TEXT("mushroom:") + M.Id)) return Fail(TEXT("picked mushroom has no assigned worker"));
    return true;
}

bool FBYMSimulation::FImpl::LoadCatalog(FString& Error)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
    FString Text; FObject Root;
    if (!Plugin.IsValid() || !FFileHelper::LoadFileToString(Text, *FPaths::Combine(Plugin->GetContentDir(), TEXT("Data/catalog.json"))) || !Decode(Text, Root))
    { Error = TEXT("The original game catalog is missing or invalid. Run Backyard Monsters NXT > Set Up Everything in the editor and rebuild the package."); return false; }
    for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("buildings")))
    {
        const FObject O = V.IsValid() && V->Type == EJson::Object ? V->AsObject() : FObject();
        int32 Id = 0;
        if (!Integer(O, TEXT("id"), 1, 100000, Id)) continue;
        FBuildingDef D; D.Id = Id; D.Name = Str(O, TEXT("name")); D.Type = Str(O, TEXT("type")); D.Texture = Str(O, TEXT("texture"));
        D.Cells = FMath::Clamp(FMath::CeilToInt(Number(O, TEXT("size"), 20) / SourcePixelsPerCell), 1, 20);
        D.bBuildable = Flag(O, TEXT("buildable"), true);
        // Original BTOWER._targetFlyerMode / Targeting.getOldStyleTargets:
        // 0 = ground, 1 = ground and air, 2 = air.
        const int32 SourceTargetMode = Id == 115 ? 2 : ((Id == 20 || Id == 23 || Id == 118 || Id == 129 || Id == 130) ? 0 : 1);
        D.TargetFlyerMode = FMath::Clamp(static_cast<int32>(Number(O, TEXT("targetFlyerMode"), SourceTargetMode)), 0, 2);
        // BFOUNDATION.GridCost/GRID.Block: only the rectangle origin is in
        // projected screen coordinates. Width and height are ground pixels.
        // Artwork/UI size is deliberately NOT the combat collision footprint.
        for (const TSharedPtr<FJsonValue>& RV : Array(O, TEXT("collisionFootprints")))
        {
            if (!RV.IsValid() || RV->Type != EJson::Array) continue;
            const FArray& A = RV->AsArray(); if (A.Num() != 4) continue;
            bool bValid = true;
            for (const TSharedPtr<FJsonValue>& CoordinateValue : A)
            {
                if (!CoordinateValue.IsValid() || CoordinateValue->Type != EJson::Number
                    || !FMath::IsFinite(CoordinateValue->AsNumber())) bValid = false;
            }
            if (!bValid || A[2]->AsNumber() <= 0 || A[3]->AsNumber() <= 0) continue;
            FCombatRect R;
            R.Min = FVector2D(A[1]->AsNumber() + A[0]->AsNumber() * 0.5, A[1]->AsNumber() - A[0]->AsNumber() * 0.5) / SourcePixelsPerCell;
            R.Max = R.Min + FVector2D(A[2]->AsNumber(), A[3]->AsNumber()) / SourcePixelsPerCell;
            D.CombatRects.Add(R);
        }
        if (D.CombatRects.IsEmpty())
        { FCombatRect R; R.Max = FVector2D(D.Cells, D.Cells); D.CombatRects.Add(R); }
        for (const TSharedPtr<FJsonValue>& Q : Array(O, TEXT("quantityByTownHall")))
            if (Q.IsValid() && Q->Type == EJson::Number) D.Quantities.Add(FMath::Clamp(static_cast<int32>(Q->AsNumber()), 0, 1000));
        for (const TSharedPtr<FJsonValue>& LV : Array(O, TEXT("levels")))
        {
            const FObject LO = LV.IsValid() && LV->Type == EJson::Object ? LV->AsObject() : FObject(); if (!LO.IsValid()) continue;
            FLevel L;
            for (int32 I = 0; I < 4; ++I) L.Cost[I] = static_cast<int64>(FMath::Clamp(Number(LO, Resources[I]), 0.0, ResourceLimit));
            L.Seconds = FMath::Clamp(Number(LO, TEXT("time"), 1), 1.0, 31536000.0);
            // XP uses authored time, including zero, independently of runtime
            // timer minimums and the host's construction speed setting.
            L.SourceSeconds = static_cast<int64>(FMath::Clamp(Number(LO, TEXT("time"), 1), 0.0, static_cast<double>(BYMExperience::MaximumPoints)));
            L.HP = FMath::Clamp(Number(LO, TEXT("hp"), 1), 1.0, ResourceLimit);
            L.Production = FMath::Clamp(Number(LO, TEXT("production")), 0.0, ResourceLimit);
            L.ProductionCycle = FMath::Clamp(Number(LO, TEXT("productionCycleSeconds"), 10), 0.1, 86400.0);
            L.Capacity = FMath::Clamp(Number(LO, TEXT("capacity")), 0.0, ResourceLimit);
            L.Damage = FMath::Clamp(Number(LO, TEXT("damage")), 0.0, ResourceLimit);
            L.Range = FMath::Clamp(Number(LO, TEXT("range")) / SourcePixelsPerCell, 0.0, 100.0);
            L.Splash = FMath::Clamp(Number(LO, TEXT("splash")) / SourcePixelsPerCell, 0.0, 20.0);
            L.AttackInterval = FMath::Clamp(Number(LO, TEXT("attackIntervalSeconds"), Number(LO, TEXT("attackRateFrames"), 36) * 2.0 / SourceSimulationHz), 0.1, 60.0);
            L.BurstShots = Id == 25 ? FMath::Clamp(static_cast<int32>(Number(LO, TEXT("burstShots"), Number(LO, TEXT("attackRateFrames"), 10))), 1, 1000) : 0;
            L.RepairSeconds = FMath::Clamp(Number(LO, TEXT("repairTime"), 30), 1.0, 31536000.0);
            L.Texture = Str(LO, TEXT("texture"), D.Texture);
            for (const TSharedPtr<FJsonValue>& RV : Array(LO, TEXT("requirements")))
            {
                if (!RV.IsValid() || RV->Type != EJson::Array) continue;
                const FArray& RA = RV->AsArray();
                if (RA.Num() == 3 && RA[0]->Type == EJson::Number && RA[1]->Type == EJson::Number && RA[2]->Type == EJson::Number)
                { FRequirement R; R.Type = static_cast<int32>(RA[0]->AsNumber()); R.Count = static_cast<int32>(RA[1]->AsNumber()); R.Level = static_cast<int32>(RA[2]->AsNumber()); if (R.Type > 0 && R.Count > 0 && R.Level > 0) L.Requirements.Add(R); }
            }
            D.Levels.Add(MoveTemp(L));
        }
        if (D.Levels.Num() > 0 && !D.Name.IsEmpty()) Buildings.Add(Id, MoveTemp(D));
    }
    for (const TSharedPtr<FJsonValue>& V : Array(Root, TEXT("monsters")))
    {
        const FObject O = V.IsValid() && V->Type == EJson::Object ? V->AsObject() : FObject(); if (!O.IsValid()) continue;
        FMonsterDef D; D.Id = Str(O, TEXT("id")); D.Name = Str(O, TEXT("name")); D.Texture = Str(O, TEXT("texture"));
        D.HP = FMath::Clamp(Number(O, TEXT("hp"), 1), 1.0, 100000000.0); D.Damage = FMath::Clamp(Number(O, TEXT("damage"), 1), -10000000.0, 10000000.0);
        const double SimulationHz = FMath::Clamp(Number(O, TEXT("sourceSimulationHz"), SourceSimulationHz), 1.0, 240.0);
        D.Speed = FMath::Clamp(Number(O, TEXT("sourceMovePixelsPerSecond"), Number(O, TEXT("speed"), 1) * SimulationHz * 0.25) / SourcePixelsPerCell, 0.1, 30.0);
        D.Range = FMath::Clamp(Number(O, TEXT("range"), 16) / SourcePixelsPerCell, 0.6, 40.0);
        const double DelayFrames = Number(O, TEXT("attackDelayTicks"), Number(O, TEXT("attackDelayFrames"))); D.AttackInterval = FMath::Clamp(Number(O, TEXT("attackIntervalSeconds"), (DelayFrames > 0 ? DelayFrames : 60.0) / SimulationHz), 0.1, 60.0);
        D.Housing = FMath::Clamp(static_cast<int32>(Number(O, TEXT("housing"), 1)), 1, 10000);
        D.Goo = static_cast<int64>(FMath::Clamp(Number(O, TEXT("goo")), 0.0, ResourceLimit));
        D.Seconds = FMath::Clamp(Number(O, TEXT("time"), 1), 1.0, 86400.0);
        D.RequiredLevel = FMath::Clamp(static_cast<int32>(Number(O, TEXT("requiredLevel"), 1)), 1, 20);
        D.TargetGroup = static_cast<int32>(Number(O, TEXT("targetGroup"), 1)); D.bFlying = Flag(O, TEXT("flying"));
        D.SourceSpeed = Number(O, TEXT("speed"), 1); D.SourceRange = Number(O, TEXT("range"));
        D.bSourceBlocked = Flag(O, TEXT("sourceBlocked")); D.bSourceFake = Flag(O, TEXT("sourceFake"));
        const FObject Unlock = Child(O, TEXT("unlock"));
        D.UnlockPutty = static_cast<int64>(FMath::Clamp(Number(Unlock, TEXT("putty"), Number(O, TEXT("unlockResource"))), 0.0, ResourceLimit));
        D.UnlockSeconds = FMath::Clamp(Number(Unlock, TEXT("time"), Number(O, TEXT("unlockTime"), 1)), 1.0, 31536000.0);
        D.bInitiallyUnlocked = Flag(Unlock, TEXT("initiallyUnlocked"), D.Id == TEXT("C1"));
        for (const TSharedPtr<FJsonValue>& LV : Array(O, TEXT("levels")))
        {
            if (!LV.IsValid() || LV->Type != EJson::Object) { Error = TEXT("Invalid original monster level data."); return false; }
            const FObject LO = LV->AsObject(); FMonsterStats L;
            int32 Level = 0;
            if (!Integer(LO, TEXT("level"), 1, 20, Level) || Level != D.Levels.Num() + 1) { Error = TEXT("Original monster levels must be contiguous."); return false; }
            L.HP = FMath::Clamp(Number(LO, TEXT("hp"), D.HP), 1.0, 100000000.0);
            L.Damage = FMath::Clamp(Number(LO, TEXT("damage"), D.Damage), -10000000.0, 10000000.0);
            L.SourceSpeed = FMath::Clamp(Number(LO, TEXT("speed"), D.SourceSpeed), 0.0, 1000.0);
            L.SourceRange = FMath::Clamp(Number(LO, TEXT("range"), D.SourceRange), 0.0, 10000.0);
            L.Speed = FMath::Clamp(Number(LO, TEXT("sourceMovePixelsPerSecond"), L.SourceSpeed * SimulationHz * 0.25) / SourcePixelsPerCell, 0.1, 30.0);
            L.Range = FMath::Clamp(L.SourceRange / SourcePixelsPerCell, 0.6, 40.0);
            L.Housing = FMath::Clamp(static_cast<int32>(Number(LO, TEXT("housing"), D.Housing)), 1, 10000);
            L.Goo = static_cast<int64>(FMath::Clamp(Number(LO, TEXT("goo"), static_cast<double>(D.Goo)), 0.0, ResourceLimit));
            L.Seconds = FMath::Clamp(Number(LO, TEXT("time"), D.Seconds), 1.0, 86400.0);
            L.TargetGroup = static_cast<int32>(Number(LO, TEXT("targetGroup"), D.TargetGroup));
            const double Frames = Number(LO, TEXT("attackDelayTicks"), Number(LO, TEXT("attackDelayFrames"), DelayFrames));
            L.AttackInterval = FMath::Clamp(Number(LO, TEXT("attackIntervalSeconds"), (Frames > 0 ? Frames : 60.0) / SimulationHz), 0.1, 60.0);
            D.Levels.Add(L);
        }
        for (const TSharedPtr<FJsonValue>& RV : Array(O, TEXT("research")))
        {
            if (!RV.IsValid() || RV->Type != EJson::Object) { Error = TEXT("Invalid original monster research data."); return false; }
            const FObject RO = RV->AsObject(); FMonsterResearchDef R;
            if (!Integer(RO, TEXT("fromLevel"), 1, 20, R.FromLevel) || !Integer(RO, TEXT("toLevel"), 2, 20, R.ToLevel)
                || !Integer(RO, TEXT("requiredAcademyLevel"), 1, 20, R.AcademyLevel)
                || R.FromLevel != D.Research.Num() + 1 || R.ToLevel != R.FromLevel + 1)
            { Error = TEXT("Original monster research transitions are inconsistent."); return false; }
            R.Putty = static_cast<int64>(FMath::Clamp(Number(RO, TEXT("putty")), 0.0, ResourceLimit));
            R.Seconds = FMath::Clamp(Number(RO, TEXT("time"), 1), 1.0, 31536000.0); D.Research.Add(R);
        }
        if (D.Levels.IsEmpty() || D.Research.Num() != D.Levels.Num() - 1)
        { Error = TEXT("The monster progression catalog is incomplete. Use the complete updated source and content together."); return false; }
        if (!D.Id.IsEmpty() && !D.Id.StartsWith(TEXT("IC"))) { const FString MonsterId = D.Id; Monsters.Add(MonsterId, MoveTemp(D)); } // Inferno requires its own yard/ruleset.
    }
    for (const int32 Required : {14, 1, 2, 3, 4, 6, 13, 15, 8, 5, 20, 26})
        if (!Buildings.Contains(Required)) { Error = FString::Printf(TEXT("Required original building %d is absent from the catalog."), Required); return false; }
    if (!Monsters.Contains(TEXT("C1"))) { Error = TEXT("Original Pokey (C1) definition is absent from the catalog."); return false; }
    return true;
}

FYard FBYMSimulation::FImpl::NewYard(const FString& Id, const FString& Name, bool bBot)
{
    FYard Y; Y.Id = Id; Y.Name = Name; Y.bBot = bBot; Y.LastEconomy = Now;
    for (const TPair<FString, FMonsterDef>& P : Monsters) if (P.Value.bInitiallyUnlocked) Y.MonsterLevels.Add(P.Key, 1);
    Y.NextDecision = Now + (bBot ? static_cast<double>(GetTypeHash(Id) % 60) : 0);
    for (int32 I = 0; I < 4; ++I) Y.Resource[I] = FMath::Clamp<int64>(GetDefault<UBYMSettings>()->StartingResources, 0, static_cast<int64>(ResourceLimit));
    // An equal, immediately usable starting yard for humans and rivals; no bot resource grants afterwards.
    const TArray<int32> Starter = bBot ? TArray<int32>{14,15,5,6,13,8,1,2,3,4,20} : TArray<int32>{14,1,2,3,4,6,13,15,8,5,20};
    for (const int32 Type : Starter)
    {
        const FBuildingDef& D = Buildings.FindChecked(Type); int32 X = 0, PosY = 0;
        if (!FreeSpot(Y, D.Cells, X, PosY, Type)) continue;
        FBuilding B; B.Id = NewId(); B.Type = Type; B.X = X; B.Y = PosY; B.HP = D.Levels[0].HP;
        Y.Buildings.Add(MoveTemp(B));
    }
    Y.LayoutRevision = bBot ? 1 : 0;
    InitializeEconomy(Y, false); SyncWorkers(Y, Now);
    RefreshBaseValue(Y);
    return Y;
}

FString FBYMSimulation::FImpl::HatcheryPauseReason(const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    if (!L || B.Level == 0) return TEXT("construction");
    if (HasWork(B)) return TEXT("upgrading");
    if (B.HP < L->HP * 0.5 || B.HP <= 10) return B.bRepairing ? TEXT("repairing") : TEXT("damaged");
    return FString();
}

int32 FBYMSimulation::FImpl::QueuedMonsters(const FYard& Y) const
{
    int32 Count = 0; for (const FTraining& T : Y.Queue) Count += T.Count; return Count;
}

int32 FBYMSimulation::FImpl::HatcherySpace(const FYard& Y, const FBuilding& B, const FString& Type) const
{
    const FLevel* L = Stats(B);
    // Original BUILDINGINFO opens Hatchery queue editing only when fully
    // repaired and finished. Already paid hatching uses the separate half-HP gate.
    if (B.Type != 13 || !L || B.Level == 0 || HasWork(B) || B.bRepairing || B.HP < L->HP) return 0;
    int32 Groups = 0, MatchingSpace = 0; bool bActive = false;
    for (const FTraining& T : Y.Queue) if (T.Building == B.Id)
    {
        if (T.bActive) bActive = true;
        else { ++Groups; if (T.Type == Type && !T.bLegacy && T.PriceScale == TrainingScale(Y)) MatchingSpace += FMath::Max(0, 20 - T.Count); }
    }
    const int32 Capacity = 1 + B.Level;
    // Grandfathered old paid queues may exceed the original number of groups.
    // They drain normally, but cannot be expanded until back within capacity.
    if (Groups > Capacity) return 0;
    return MatchingSpace + FMath::Max(0, Capacity - Groups) * 20 + (!bActive && Groups == 0 ? 1 : 0);
}

const FBuilding* FBYMSimulation::FImpl::SelectHatchery(const FYard& Y, const FMonsterDef& D, const FString& Requested) const
{
    if (!Requested.IsEmpty())
    {
        const FBuilding* B = FindBuilding(Y, Requested); return B && B->Type == 13 ? B : nullptr;
    }
    const FBuilding* Best = nullptr; int32 BestCount = MAX_int32;
    for (const FBuilding& B : Y.Buildings) if (B.Type == 13 && HatcherySpace(Y, B, D.Id) > 0)
    {
        int32 Count = 0; for (const FTraining& T : Y.Queue) if (T.Building == B.Id) Count += T.Count;
        if (!Best || Count < BestCount) { Best = &B; BestCount = Count; }
    }
    return Best;
}

void FBYMSimulation::FImpl::AdvanceHatcheries(FYard& Y, double Elapsed)
{
    double Left = FMath::Max(0.0, Elapsed);
    // Each iteration either consumes elapsed time or completes/promotes a paid
    // creature. No per-second offline loop, and no shared yard-wide incubator.
    const int32 Budget = QueuedMonsters(Y) * 2 + Y.Buildings.Num() + 2;
    for (int32 Iteration = 0; Iteration < Budget; ++Iteration)
    {
        bool bTransition = false;
        for (const FBuilding& B : Y.Buildings) if (B.Type == 13 && HatcheryPauseReason(B).IsEmpty())
        {
            int32 ActiveIndex = Y.Queue.IndexOfByPredicate([&B](const FTraining& T) { return T.Building == B.Id && T.bActive; });
            if (ActiveIndex != INDEX_NONE && Y.Queue[ActiveIndex].Remaining <= 0.000001)
            {
                const FTraining& Active = Y.Queue[ActiveIndex]; const FMonsterDef* D = Monsters.Find(Active.Type);
                if (D && UsedHousing(Y) + MonsterStats(Y, *D).Housing <= Housing(Y))
                {
                    Y.Army.FindOrAdd(Active.Type) += 1; Y.Queue.RemoveAt(ActiveIndex);
                    ActiveIndex = INDEX_NONE; bTransition = true;
                }
            }
            if (ActiveIndex == INDEX_NONE)
            {
                const int32 WaitingIndex = Y.Queue.IndexOfByPredicate([&B](const FTraining& T) { return T.Building == B.Id && !T.bActive; });
                if (WaitingIndex != INDEX_NONE)
                {
                    FTraining Active = Y.Queue[WaitingIndex]; const FMonsterDef* D = Monsters.Find(Active.Type);
                    if (!D) continue;
                    Active.Id = NewId(); Active.Count = 1; Active.bActive = true;
                    Active.Remaining = Active.bLegacy ? Active.Remaining : FMath::Clamp(MonsterStats(Y, *D).Seconds * TrainingMultiplier() * Active.PriceScale, 1.0, 86400.0);
                    if (--Y.Queue[WaitingIndex].Count <= 0) Y.Queue.RemoveAt(WaitingIndex);
                    Y.Queue.Add(MoveTemp(Active)); bTransition = true;
                }
            }
        }
        if (bTransition) continue;
        if (Left <= 0.000001) break;
        double Step = Left; bool bAdvancing = false;
        for (const FTraining& T : Y.Queue) if (T.bActive && T.Remaining > 0.000001)
        {
            const FBuilding* B = FindBuilding(Y, T.Building);
            if (B && HatcheryPauseReason(*B).IsEmpty()) { Step = FMath::Min(Step, T.Remaining); bAdvancing = true; }
        }
        if (!bAdvancing) break; // All active outputs are held for housing or paused.
        for (FTraining& T : Y.Queue) if (T.bActive)
        {
            const FBuilding* B = FindBuilding(Y, T.Building);
            if (B && HatcheryPauseReason(*B).IsEmpty()) T.Remaining = FMath::Max(0.0, T.Remaining - Step);
        }
        Left = FMath::Max(0.0, Left - Step);
    }
}

void FBYMSimulation::FImpl::DestroyHatcheryQueue(FYard& Y, const FString& BuildingId)
{
    int64 Value = 0;
    for (int32 Index = Y.Queue.Num() - 1; Index >= 0; --Index) if (Y.Queue[Index].Building == BuildingId)
    {
        const FMonsterDef* D = Monsters.Find(Y.Queue[Index].Type);
        if (D) Value += MonsterStats(Y, *D).Goo * Y.Queue[Index].PriceScale * Y.Queue[Index].Count;
        Y.Queue.RemoveAt(Index);
    }
    // BUILDING13.Destroyed: ceil(75% of the current researched Goo value).
    // BASE.Fund caps the returned amount at available storage, never lowers an
    // already over-cap wallet. Removing the entries makes the refund idempotent.
    const int64 Refund = (Value * 3 + 3) / 4;
    Y.Resource[3] += FMath::Min<int64>(Refund, FMath::Max<int64>(0, Storage(Y) - Y.Resource[3]));
}

void FBYMSimulation::FImpl::CompleteBuildingWork(FYard& Y, FBuilding& B, double At)
{
    if (!HasWork(B)) return;
    // One authoritative completion point, including migrated jobs.
    const FBuildingDef* Definition = Buildings.Find(B.Type);
    const int32 PriorLevel = B.Level;
    if (Definition && Definition->Levels.IsValidIndex(FMath::Max(1, PriorLevel) - 1))
    {
        const FLevel& RewardLevel = Definition->Levels[FMath::Max(1, PriorLevel) - 1];
        const int64 Award = PriorLevel == 0
            ? BYMExperience::ConstructionReward(RewardLevel.SourceSeconds, SourceResourceCost(RewardLevel), B.Type == 14)
            : BYMExperience::UpgradeReward(RewardLevel.SourceSeconds, SourceResourceCost(RewardLevel));
        Y.EarnedExperience = BYMExperience::Add(Y.EarnedExperience, Award);
    }
    ReleaseWorker(Y, PriorLevel == 0 ? TEXT("construction") : TEXT("upgrade"), B.Id, At);
    B.Level = B.TargetLevel; B.WorkRemaining = 0.; B.JobId.Reset(); B.SpeedupRevision = 0; B.bRepairing = false;
    const FLevel* Completed = Stats(B); if (Completed) B.HP = Completed->HP;
}

void FBYMSimulation::FImpl::Advance(FYard& Y, double Until)
{
    const double End = Until >= 0. ? FMath::Min(Now, Until) : Now;
    if (!FMath::IsFinite(End) || End < Y.LastEconomy) return;
    if (IsMapSite(Y.Id)) { AdvanceMapSite(Y, End); return; }
    AdvanceMapResources(Y, End);
    double Cursor = FMath::Max(0.0, FMath::Max(Y.LastEconomy, End - 31536000.));
    const bool bRaided = UnderAttack(Y.Id);
    for (;;)
    {
        SyncWorkers(Y, Cursor); AdvanceEconomy(Y, Cursor);
        for (FBuilding& B : Y.Buildings)
        {
            const FLevel* L = Stats(B); if (!L) continue;
            if (B.bRepairing)
            {
                const BYMEconomy::FRepairState State = RepairState(B, *L);
                const double ToHalf = BYMEconomy::SecondsToHealth(State, .5);
                if (B.Type == 13 && ToHalf > 0. && Cursor + ToHalf <= Cursor) B.HP = L->HP * .5;
                const double ToFull = BYMEconomy::RepairDuration(RepairState(B, *L));
                if (B.HP >= L->HP || Cursor + ToFull <= Cursor)
                { B.HP = L->HP; B.bRepairing = false; }
            }
            if (HasWork(B) && BuildingWorkerReady(Y, B, Cursor) && BYMBuildingLifecycle::CanWork(B.HP, L->HP, B.bRepairing)
                && (B.WorkRemaining <= 0. || Cursor + B.WorkRemaining <= Cursor))
            {
                CompleteBuildingWork(Y, B, Cursor);
            }
        }
        if (!Y.MonsterUnlock.Type.IsEmpty() && Y.MonsterUnlock.ReadyAt <= Cursor)
        { Y.MonsterLevels.FindOrAdd(Y.MonsterUnlock.Type) = 1; Y.MonsterUnlock = FMonsterJob(); }
        for (int32 Index = Y.MonsterResearch.Num() - 1; Index >= 0; --Index)
            if (Y.MonsterResearch[Index].ReadyAt <= Cursor)
            { Y.MonsterLevels.FindOrAdd(Y.MonsterResearch[Index].Type) = Y.MonsterResearch[Index].ToLevel; Y.MonsterResearch.RemoveAt(Index); }
        AdvanceHatcheries(Y, 0);
        if (Cursor >= End) break;
        double Boundary = EconomyNextBoundary(Y, Cursor, End);
        for (const FWorker& W : Y.Workers) if (!W.Kind.IsEmpty() && W.ArrivalAt > Cursor)
            Boundary = FMath::Min(Boundary, W.ArrivalAt);
        for (const FBuilding& B : Y.Buildings)
        {
            const FLevel* L = Stats(B); if (!L) continue;
            if (B.bRepairing)
            {
                Boundary = FMath::Min(Boundary, Cursor + BYMEconomy::RepairDuration(RepairState(B, *L)));
                if (B.Type == 13 && B.HP < L->HP * .5)
                {
                    const double ToWorking = BYMEconomy::SecondsToHealth(RepairState(B, *L), .5);
                    if (ToWorking > 0.) Boundary = FMath::Min(Boundary, Cursor + ToWorking);
                }
            }
            else if (HasWork(B) && BuildingWorkerReady(Y, B, Cursor) && BYMBuildingLifecycle::CanWork(B.HP, L->HP, false))
                Boundary = FMath::Min(Boundary, Cursor + B.WorkRemaining);
        }
        if (!Y.MonsterUnlock.Type.IsEmpty() && Y.MonsterUnlock.ReadyAt > Cursor) Boundary = FMath::Min(Boundary, Y.MonsterUnlock.ReadyAt);
        for (const FMonsterJob& Job : Y.MonsterResearch) if (Job.ReadyAt > Cursor) Boundary = FMath::Min(Boundary, Job.ReadyAt);
        const double Elapsed = FMath::Max(0.0, Boundary - Cursor);
        // Every subsystem uses eligibility at the beginning of this phase.
        // Completing repair at its end cannot retroactively advance paid work.
        AdvanceHatcheries(Y, Elapsed);
        for (FBuilding& B : Y.Buildings)
        {
            const FLevel* L = Stats(B); if (!L) continue;
            BYMEconomy::FRepairState State = RepairState(B, *L);
            if (HasWork(B) && BuildingWorkerReady(Y, B, Cursor)) B.WorkRemaining = BYMBuildingLifecycle::AdvanceWork(B.WorkRemaining,
                Elapsed, B.HP, L->HP, B.bRepairing);
            if (B.Type >= 1 && B.Type <= 4)
            {
                BYMEconomy::FCollectorState Collector = CollectorState(B);
                BYMEconomy::Advance(Collector, State, CollectorRules(*L), bRaided || HasWork(B), Elapsed);
                B.Stored = Collector.Stored; B.ProductionRemaining = Collector.Remaining;
            }
            else BYMEconomy::AdvanceRepair(State, Elapsed);
            B.HP = State.Health; B.bRepairing = State.bRepairing;
        }
        Cursor = Boundary;
    }
    Y.LastEconomy = FMath::Max(Y.LastEconomy, End);
    RefreshBaseValue(Y);
}

FArray FBYMSimulation::FImpl::HatcheriesJSON(const FYard& Y, bool bSave) const
{
    FArray Result;
    for (const FBuilding& B : Y.Buildings) if (B.Type == 13)
    {
        FObject H = Obj(); H->SetStringField(TEXT("building"), B.Id);
        FObject Active = Obj(); FArray Waiting; int32 Count = 0;
        const FString Pause = HatcheryPauseReason(B);
        FString State = Pause.IsEmpty() ? TEXT("idle") : Pause;
        for (const FTraining& T : Y.Queue) if (T.Building == B.Id)
        {
            const FMonsterDef* D = Monsters.Find(T.Type); if (!D) continue;
            FObject Entry = Obj(); Entry->SetStringField(TEXT("id"), T.Id); Entry->SetStringField(TEXT("type"), T.Type);
            Entry->SetNumberField(TEXT("count"), T.Count); Entry->SetNumberField(TEXT("remaining"), T.Remaining);
            if (bSave) { Entry->SetBoolField(TEXT("legacy"), T.bLegacy); Entry->SetNumberField(TEXT("priceScale"), T.PriceScale); }
            else
            {
                const FMonsterStats& S = MonsterStats(Y, *D);
                Entry->SetNumberField(TEXT("refundGoo"), static_cast<double>(S.Goo * T.PriceScale));
                Entry->SetNumberField(TEXT("totalGooValue"), static_cast<double>(S.Goo * T.PriceScale * T.Count));
                Entry->SetNumberField(TEXT("readyAt"), T.bActive && Pause.IsEmpty() && T.Remaining > 0 ? Now + T.Remaining : 0);
                Entry->SetNumberField(TEXT("duration"), T.bLegacy ? FMath::Max(T.Remaining, 1.0) : FMath::Clamp(S.Seconds * TrainingMultiplier() * T.PriceScale, 1.0, 86400.0));
            }
            if (T.bActive)
            {
                Active = Entry;
                if (Pause.IsEmpty()) State = T.Remaining <= 0.000001 ? TEXT("housing_full") : TEXT("hatching");
            }
            else Waiting.Add(MakeShared<FJsonValueObject>(Entry));
            Count += T.Count;
        }
        H->SetObjectField(TEXT("active"), Active); H->SetArrayField(TEXT("queue"), Waiting);
        if (!bSave)
        {
            H->SetNumberField(TEXT("level"), B.Level); H->SetNumberField(TEXT("queueCapacity"), 1 + B.Level);
            H->SetNumberField(TEXT("groupCapacity"), 20); H->SetNumberField(TEXT("queuedCount"), Count);
            H->SetStringField(TEXT("state"), State);
            FString Reason;
            if (State == TEXT("construction")) Reason = TEXT("Complete this Hatchery before hatching monsters.");
            else if (State == TEXT("upgrading")) Reason = TEXT("Hatching is paused until this Hatchery finishes upgrading.");
            else if (State == TEXT("repairing")) Reason = TEXT("Hatching resumes when repairs restore at least half health.");
            else if (State == TEXT("damaged")) Reason = TEXT("Repair this Hatchery to at least half health to resume hatching.");
            else if (State == TEXT("housing_full")) Reason = TEXT("This monster has hatched and is waiting for free housing.");
            H->SetStringField(TEXT("pauseReason"), Reason);
            FObject Options = Obj();
            for (const TPair<FString, FMonsterDef>& Pair : Monsters) if (IsOperationalMonster(Pair.Key))
            {
                FObject Choice = Obj(); const FString Error = TrainReason(Y, Pair.Value, B.Id);
                Choice->SetBoolField(TEXT("canTrain"), Error.IsEmpty()); Choice->SetStringField(TEXT("reason"), Error);
                Choice->SetNumberField(TEXT("maxTrainCount"), MaxTrainCount(Y, Pair.Value, B.Id)); Options->SetObjectField(Pair.Key, Choice);
            }
            H->SetObjectField(TEXT("trainOptions"), Options);
        }
        Result.Add(MakeShared<FJsonValueObject>(H));
    }
    return Result;
}

FObject FBYMSimulation::FImpl::YardJSON(const FYard& Y, bool bSave, bool bPrivate) const
{
    FObject O = Obj(); O->SetStringField(TEXT("id"), Y.Id); O->SetStringField(TEXT("name"), Y.Name); O->SetBoolField(TEXT("bot"), Y.bBot);
    if (!bSave) O->SetBoolField(TEXT("activeBot"), Y.bBot && ActiveBot(Y.Id));
    O->SetNumberField(TEXT("rp"), static_cast<double>(Y.RP)); O->SetNumberField(TEXT("wins"), Y.Wins); O->SetNumberField(TEXT("losses"), Y.Losses);
    if (bSave)
    {
        O->SetNumberField(TEXT("earnedExperience"), static_cast<double>(Y.EarnedExperience));
        O->SetNumberField(TEXT("baseValue"), static_cast<double>(Y.BaseValue));
    }
    else O->SetObjectField(TEXT("experience"), ExperienceJSON(Y));
    O->SetNumberField(TEXT("shieldUntil"), Y.ShieldUntil); O->SetObjectField(TEXT("resources"), ResourceObject(Y.Resource));
    O->SetNumberField(TEXT("storageCapacity"), static_cast<double>(Storage(Y))); O->SetNumberField(TEXT("housingCapacity"), Housing(Y));
    O->SetNumberField(TEXT("housingUsed"), UsedHousing(Y)); O->SetBoolField(TEXT("underAttack"), UnderAttack(Y.Id));
    if (!bSave && bPrivate)
    {
        int64 ReadyHousing = 0, QueuedHousing = 0;
        for (const TPair<FString, int32>& P : Y.Army) { const FMonsterDef* D = Monsters.Find(P.Key); if (D) ReadyHousing += static_cast<int64>(P.Value) * MonsterStats(Y, *D).Housing; }
        for (const FTraining& T : Y.Queue) { const FMonsterDef* D = Monsters.Find(T.Type); if (D) QueuedHousing += static_cast<int64>(T.Count) * MonsterStats(Y, *D).Housing; }
        O->SetNumberField(TEXT("housingReady"), static_cast<double>(ReadyHousing));
        O->SetNumberField(TEXT("housingQueued"), static_cast<double>(QueuedHousing));
    }
    if (!bSave)
    {
        O->SetNumberField(TEXT("townHallLevel"), AttainedLevel(Y, 14));
        O->SetBoolField(TEXT("constructionBusy"), ConstructionBusy(Y));
        FArray Residents;
        for (const BYMYardResidents::FResident& R : BYMYardResidents::SelectRoster(Y.Army, GetDefault<UBYMSettings>()->MaxVisibleYardMonsters))
        {
            FObject Resident = Obj(); Resident->SetStringField(TEXT("id"), R.Id); Resident->SetStringField(TEXT("type"), R.Type);
            Residents.Add(MakeShared<FJsonValueObject>(Resident));
        }
        O->SetArrayField(TEXT("residentRoster"), Residents);
        O->SetStringField(TEXT("residentYardId"), Y.Id);
    }
    if (bSave) { WriteEconomy(Y, O); O->SetObjectField(TEXT("workerState"), WorkersJSON(Y, true)); }
    else
    {
        O->SetNumberField(TEXT("minX"), YardMinX(Y)); O->SetNumberField(TEXT("minY"), YardMinY(Y));
        O->SetNumberField(TEXT("maxX"), YardMinX(Y) + YardWidth(Y)); O->SetNumberField(TEXT("maxY"), YardMinY(Y) + YardHeight(Y));
        O->SetNumberField(TEXT("width"), YardWidth(Y)); O->SetNumberField(TEXT("height"), YardHeight(Y));
        O->SetObjectField(TEXT("workers"), WorkersJSON(Y, false, bPrivate));
        O->SetObjectField(TEXT("economy"), EconomySnapshot(Y, false));
        if (bPrivate) O->SetObjectField(TEXT("shop"), EconomySnapshot(Y, true));
        if (bPrivate) O->SetNumberField(TEXT("shiny"), static_cast<double>(Y.Shiny));
    }
    if (!bSave && bPrivate) O->SetArrayField(TEXT("bunkers"), BunkersJSON(Y));
    FArray BA;
    for (const FBuilding& B : Y.Buildings)
    {
        const FBuildingDef* D = Buildings.Find(B.Type); const FLevel* L = Stats(B); if (!D || !L) continue;
        FObject BO = Obj(); BO->SetStringField(TEXT("id"), B.Id); BO->SetNumberField(TEXT("type"), B.Type);
        BO->SetNumberField(TEXT("level"), B.Level); BO->SetNumberField(TEXT("targetLevel"), B.TargetLevel);
        BO->SetNumberField(TEXT("x"), B.X); BO->SetNumberField(TEXT("y"), B.Y); BO->SetNumberField(TEXT("hp"), B.HP);
        BO->SetNumberField(TEXT("readyAt"), B.bRepairing ? RepairReadyAt(B) : WorkReadyAt(Y, B));
        BO->SetNumberField(TEXT("stored"), B.Stored); BO->SetBoolField(TEXT("repairing"), B.bRepairing);
        BO->SetStringField(TEXT("jobId"), bSave || bPrivate ? B.JobId : FString());
        BO->SetNumberField(TEXT("workRemaining"), B.WorkRemaining);
        if (bSave || bPrivate) BO->SetNumberField(TEXT("speedupRevision"), B.SpeedupRevision);
        if (B.Type >= 1 && B.Type <= 4) BO->SetNumberField(TEXT("productionRemaining"), B.ProductionRemaining);
        if (bSave && B.Type == 22)
        {
            FObject Stock = Obj();
            if (B.HP > 0) for (const TPair<FString, int32>& Entry : B.Garrison) if (Entry.Value > 0) Stock->SetNumberField(Entry.Key, Entry.Value);
            BO->SetObjectField(TEXT("garrison"), Stock);
        }
        if (!bSave)
        {
            BO->SetNumberField(TEXT("size"), D->Cells); BO->SetNumberField(TEXT("maxHp"), L->HP);
            BO->SetStringField(TEXT("jobKind"), HasWork(B) ? (B.Level == 0 ? TEXT("construction") : TEXT("upgrade")) : TEXT("none"));
            BO->SetNumberField(TEXT("workReadyAt"), WorkReadyAt(Y, B));
            BO->SetStringField(TEXT("workPausedReason"), HasWork(B) ? (B.bRepairing ? TEXT("repairing") : (B.HP < L->HP ? TEXT("damaged") : (BuildingWorkerReady(Y, B, Now) ? TEXT("") : TEXT("worker_walking")))) : TEXT(""));
            const FWorker* Worker = Y.Workers.FindByPredicate([&](const FWorker& W) { return W.Task == B.Id && W.Job == B.JobId && !W.Kind.IsEmpty(); });
            BO->SetStringField(TEXT("workerId"), Worker ? Worker->Id : FString());
            BO->SetNumberField(TEXT("workerArrivalAt"), Worker ? Worker->ArrivalAt : 0);
            BO->SetNumberField(TEXT("repairRemaining"), B.bRepairing ? BYMEconomy::RepairDuration(RepairState(B, *L)) : 0.);
            BO->SetNumberField(TEXT("repairReadyAt"), RepairReadyAt(B));
            const FString RepairError = bPrivate ? RepairReason(Y, B) : TEXT("Repair buildings in your own yard.");
            const FString CancelError = bPrivate ? CancelWorkReason(Y, B) : TEXT("Cancel work in your own yard.");
            const FString MoveError = bPrivate ? MoveReason(Y, B) : TEXT("Move buildings in your own yard.");
            BO->SetBoolField(TEXT("canRepair"), RepairError.IsEmpty()); BO->SetStringField(TEXT("repairReason"), RepairError);
            BO->SetBoolField(TEXT("canCancelConstruction"), CancelError.IsEmpty() && B.Level == 0);
            BO->SetBoolField(TEXT("canCancelUpgrade"), CancelError.IsEmpty() && B.Level > 0);
            BO->SetStringField(TEXT("cancelReason"), CancelError);
            BO->SetBoolField(TEXT("canMove"), MoveError.IsEmpty()); BO->SetStringField(TEXT("moveReason"), MoveError);
            int64 Refund[4] = {0, 0, 0, 0}; if (bPrivate) WorkRefund(Y, B, Refund);
            BO->SetObjectField(TEXT("cancelRefund"), ResourceObject(Refund));
            bool bWorking = false;
            if (B.Type >= 1 && B.Type <= 4)
            {
                const BYMEconomy::FCollectorRules Rules = CollectorRules(*L);
                const BYMEconomy::FRepairState State = RepairState(B, *L);
                const bool bBlocked = HasWork(B) || UnderAttack(Y.Id);
                bWorking = BYMEconomy::CanProduce(CollectorState(B), Rules, State, bBlocked);
                FString ProductionState = TEXT("producing");
                if (B.Level == 0) ProductionState = TEXT("constructing");
                else if (HasWork(B)) ProductionState = TEXT("upgrading");
                else if (B.HP < L->HP * .5) ProductionState = TEXT("damaged");
                else if (B.Stored >= L->Capacity) ProductionState = TEXT("full");
                else if (!bWorking) ProductionState = TEXT("paused");
                BO->SetStringField(TEXT("productionState"), ProductionState);
                BO->SetNumberField(TEXT("productionBatch"), L->Production);
                BO->SetNumberField(TEXT("productionCycleSeconds"), BYMEconomy::CycleSeconds(Rules, State));
                BO->SetNumberField(TEXT("collectorCapacity"), L->Capacity);
                FString CollectReason;
                if (!bPrivate) CollectReason = TEXT("Collect resources from your own yard.");
                else if (Busy(Y.Id)) CollectReason = TEXT("Collect resources after the current battle.");
                else if (B.Level == 0) CollectReason = TEXT("This collector is still under construction.");
                else if (B.HP < L->HP) CollectReason = TEXT("Fully repair this collector before collecting.");
                else if (HasWork(B) || B.bRepairing) CollectReason = TEXT("Wait for this collector's upgrade to finish.");
                else if (B.Stored < 1.) CollectReason = TEXT("This collector has no resources ready.");
                else if (Y.Resource[B.Type - 1] >= Storage(Y)) CollectReason = TEXT("Your resource storage is full.");
                BO->SetBoolField(TEXT("canCollect"), CollectReason.IsEmpty());
                BO->SetStringField(TEXT("collectReason"), CollectReason);
            }
            else if (B.Type == 13 && HatcheryPauseReason(B).IsEmpty())
                bWorking = Y.Queue.ContainsByPredicate([&B](const FTraining& T) { return T.Building == B.Id && T.bActive && T.Remaining > .000001; });
            else if (B.Level > 0 && !HasWork(B))
            {
                if (B.Type == 8) bWorking = !Y.MonsterUnlock.Type.IsEmpty();
                else if (B.Type == 26) bWorking = AcademyBusy(Y, B.Id);
            }
            // Only the coarse visible working pose is public, never private
            // monster identities, quantities or research queue contents.
            BO->SetBoolField(TEXT("working"), bWorking);

            const bool bNext = B.Level > 0 && D->Levels.IsValidIndex(B.Level);
            const FString Reason = UpgradeReason(Y, B);
            BO->SetBoolField(TEXT("upgradeAvailable"), bNext);
            BO->SetBoolField(TEXT("canUpgrade"), Reason.IsEmpty());
            BO->SetStringField(TEXT("upgradeReason"), Reason);
            BO->SetNumberField(TEXT("nextLevel"), bNext ? B.Level + 1 : B.Level);
            if (bNext)
            {
                BO->SetObjectField(TEXT("upgradeCosts"), ResourceObject(D->Levels[B.Level].Cost));
                BO->SetNumberField(TEXT("upgradeSeconds"), FMath::Clamp(D->Levels[B.Level].Seconds * ConstructionMultiplier(), 1.0, 31536000.0));
            }
        }
        BA.Add(MakeShared<FJsonValueObject>(BO));
    }
    O->SetArrayField(TEXT("buildings"), BA);
    FObject Army = Obj();
    if (bPrivate || bSave) for (const TPair<FString, int32>& P : Y.Army) Army->SetNumberField(P.Key, P.Value);
    O->SetObjectField(TEXT("army"), Army);
    FArray Training;
    if (bPrivate || bSave)
    {
        O->SetArrayField(TEXT("hatcheries"), HatcheriesJSON(Y, bSave));
        // Backward-compatible, per-monster UI projection; schema 3 loads the
        // authoritative grouped Hatchery state above, never these estimates.
        for (const FBuilding& B : Y.Buildings) if (B.Type == 13)
        {
            const bool bRunning = HatcheryPauseReason(B).IsEmpty(); double Ready = Now;
            const FTraining* Active = Y.Queue.FindByPredicate([&B](const FTraining& T) { return T.Building == B.Id && T.bActive; });
            auto Append = [&](const FTraining& T, bool bCurrent)
            {
                const FMonsterDef* D = Monsters.Find(T.Type); if (!D) return;
                for (int32 Unit = 0; Unit < T.Count; ++Unit)
                {
                    Ready += bCurrent || T.bLegacy ? T.Remaining : FMath::Clamp(MonsterStats(Y, *D).Seconds * TrainingMultiplier() * T.PriceScale, 1.0, 86400.0);
                    FObject Entry = Obj(); Entry->SetStringField(TEXT("id"), T.Id); Entry->SetStringField(TEXT("building"), B.Id);
                    Entry->SetStringField(TEXT("type"), T.Type); Entry->SetNumberField(TEXT("count"), 1);
                    Entry->SetNumberField(TEXT("readyAt"), bRunning ? Ready : 0); Entry->SetBoolField(TEXT("active"), bCurrent);
                    Training.Add(MakeShared<FJsonValueObject>(Entry));
                }
            };
            if (Active) Append(*Active, true);
            for (const FTraining& T : Y.Queue) if (T.Building == B.Id && !T.bActive) Append(T, false);
        }
    }
    O->SetArrayField(TEXT("training"), Training);
    if (bPrivate || bSave)
    {
        FObject Levels = Obj();
        for (const TPair<FString, int32>& P : Y.MonsterLevels) Levels->SetNumberField(P.Key, P.Value);
        O->SetObjectField(TEXT("monsterLevels"), Levels);
        O->SetObjectField(TEXT("monsterUnlock"), MonsterJobJSON(Y.MonsterUnlock, false));
        FArray Research; for (const FMonsterJob& Job : Y.MonsterResearch) Research.Add(MakeShared<FJsonValueObject>(MonsterJobJSON(Job, true)));
        O->SetArrayField(TEXT("monsterResearch"), Research);
        FArray Logs; for (const FObject& L : Y.Log) Logs.Add(MakeShared<FJsonValueObject>(L)); O->SetArrayField(TEXT("log"), Logs);
    }
    if (bSave)
    { O->SetNumberField(TEXT("lastEconomy"), Y.LastEconomy); O->SetNumberField(TEXT("nextDecision"), Y.NextDecision); O->SetNumberField(TEXT("decisions"), Y.Decisions); O->SetNumberField(TEXT("layoutRevision"), Y.LayoutRevision); }
    return O;
}

FObject FBYMSimulation::FImpl::BattleJSON(const FBattle& B, bool bSave) const
{
    FObject O = Obj(); O->SetStringField(TEXT("id"), B.Id); O->SetStringField(TEXT("attacker"), B.Attacker); O->SetStringField(TEXT("defender"), B.Defender);
    O->SetStringField(TEXT("mapCellId"), B.MapCellId);
    O->SetNumberField(TEXT("startedAt"), B.StartedAt); O->SetNumberField(TEXT("endsAt"), B.EndsAt); O->SetNumberField(TEXT("deployed"), B.Deployed);
    O->SetNumberField(TEXT("initialBuildingHP"), B.InitialBuildingHP); O->SetNumberField(TEXT("initialBuildings"), B.InitialBuildings);
    if (!bSave)
    {
        const FYard* Attacker = Yards.Find(B.Attacker);
        const int64 Capacity = Attacker ? FlingerCapacity(*Attacker) : 0;
        const int64 Used = FlingerUsed(B);
        O->SetStringField(TEXT("phase"), B.Deployed > 0 ? TEXT("attacking") : TEXT("deploying"));
        O->SetNumberField(TEXT("remainingSeconds"), FMath::Max(0.0, B.EndsAt - Now));
        O->SetBoolField(TEXT("canDeploy"), B.EndsAt > Now && HasDeployableReserves(B));
        O->SetNumberField(TEXT("flingerCapacity"), Capacity);
        O->SetNumberField(TEXT("flingerUsed"), Used);
        O->SetNumberField(TEXT("flingerRemaining"), FMath::Max<int64>(0, Capacity - Used));
        O->SetNumberField(TEXT("remainingTroopSlots"), FMath::Max(0, TroopLimit() - B.Deployed));
        O->SetNumberField(TEXT("deploymentBandCells"), 3);
        O->SetNumberField(TEXT("serverTime"), B.SimulatedAt);
        FArray Events; for (const FObject& Event : B.Events) Events.Add(MakeShared<FJsonValueObject>(Event));
        O->SetArrayField(TEXT("events"), Events);
        FArray Defenses; const FYard* Yard = Yards.Find(B.Defender);
        if (Yard) for (const FBuilding& Defense : Yard->Buildings)
        {
            const FLevel* L = Stats(Defense); if (!L || L->Damage <= 0 || L->Range <= 0) continue;
            const FDefenseVisual* Visual = B.DefenseVisuals.Find(Defense.Id);
            FObject D = Obj(); D->SetStringField(TEXT("id"), Defense.Id);
            D->SetStringField(TEXT("state"), Defense.HP <= 0 ? TEXT("dead") : (Visual && B.SimulatedAt - Visual->LastAttackAt < FMath::Min(0.8, L->AttackInterval) ? TEXT("attacking") : TEXT("idle")));
            D->SetNumberField(TEXT("facingX"), Visual ? Visual->Facing.X : 0); D->SetNumberField(TEXT("facingY"), Visual ? Visual->Facing.Y : 1);
            D->SetNumberField(TEXT("lastAttackAt"), Visual ? Visual->LastAttackAt : 0);
            D->SetNumberField(TEXT("attackSequence"), Visual ? Visual->AttackSequence : 0);
            D->SetNumberField(TEXT("attackInterval"), L->AttackInterval); D->SetStringField(TEXT("target"), Visual ? Visual->Target : FString());
            const FDefenseBurst* Burst = B.DefenseBursts.Find(Defense.Id);
            D->SetNumberField(TEXT("stateStartedAt"), Burst ? Burst->StartedAt : (Visual ? Visual->LastAttackAt : 0));
            D->SetNumberField(TEXT("burstShotsRemaining"), Burst ? Burst->ShotsRemaining : 0);
            D->SetNumberField(TEXT("coolingStartFrame"), Burst ? Burst->CoolingStartFrame : 32);
            if (Burst && Defense.HP > 0)
                D->SetStringField(TEXT("state"), Burst->Phase == TEXT("active") ? TEXT("attacking") : Burst->Phase);
            Defenses.Add(MakeShared<FJsonValueObject>(D));
        }
        O->SetArrayField(TEXT("defenses"), Defenses);
    }
    const FYard* Defender = Yards.Find(B.Defender); double RemainingHP = 0; int32 RemainingBuildings = 0;
    if (Defender) for (const FBuilding& BD : Defender->Buildings) if (IsCombatObjective(BD)) { RemainingHP += BD.HP; if (BD.HP > 0) ++RemainingBuildings; }
    O->SetNumberField(TEXT("destructionPercent"), !B.MapCellId.IsEmpty() && Defender ? MapDamagePercent(*Defender) : (B.InitialBuildings > 0 ? 100.0 * (B.InitialBuildings - RemainingBuildings) / B.InitialBuildings : 0));
    auto TroopObject = [&](const FTroop& T)
    {
        FObject TO = Obj(); TO->SetStringField(TEXT("id"), T.Id); TO->SetStringField(TEXT("type"), T.Type);
        TO->SetNumberField(TEXT("level"), T.Level);
        TO->SetNumberField(TEXT("x"), T.Position.X); TO->SetNumberField(TEXT("y"), T.Position.Y); TO->SetNumberField(TEXT("hp"), T.HP);
        TO->SetStringField(TEXT("target"), T.Target); TO->SetStringField(TEXT("targetCreep"), T.TargetCreep);
        if (!bSave)
        {
            const FMonsterDef* M = Monsters.Find(T.Type);
            if (M) { TO->SetNumberField(TEXT("maxHp"), MonsterStats(*M, T.Level).HP); TO->SetNumberField(TEXT("attackInterval"), MonsterStats(*M, T.Level).AttackInterval); }
            TO->SetStringField(TEXT("state"), T.HP <= 0 ? TEXT("dead") : T.State);
            TO->SetNumberField(TEXT("targetX"), T.TargetPosition.X); TO->SetNumberField(TEXT("targetY"), T.TargetPosition.Y);
            TO->SetNumberField(TEXT("facingX"), T.Facing.X); TO->SetNumberField(TEXT("facingY"), T.Facing.Y);
            TO->SetNumberField(TEXT("lastAttackAt"), T.LastAttackAt); TO->SetNumberField(TEXT("attackSequence"), T.AttackSequence); TO->SetNumberField(TEXT("deployedAt"), T.DeployedAt);
        }
        else TO->SetNumberField(TEXT("attackCooldown"), FMath::Max(0.0, T.AttackCooldown));
        return TO;
    };
    FArray TA; for (const FTroop& T : B.Troops) TA.Add(MakeShared<FJsonValueObject>(TroopObject(T)));
    O->SetArrayField(TEXT("troops"), TA);
    FArray DA;
    for (const FBunkerDefender& Unit : B.Defenders)
    {
        FObject Item = TroopObject(Unit); Item->SetStringField(TEXT("bunker"), Unit.Bunker);
        Item->SetBoolField(TEXT("returned"), Unit.bReturned); Item->SetBoolField(TEXT("deathHandled"), Unit.bDeathHandled);
        if (bSave) Item->SetNumberField(TEXT("retargetRemaining"), Unit.RetargetRemaining);
        if (!bSave && Unit.bReturned) Item->SetStringField(TEXT("state"), TEXT("returned"));
        DA.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("defenders"), DA);
    FArray BunkerStates;
    for (const TPair<FString, FBunkerState>& Entry : B.Bunkers)
    {
        const FBunkerState& State = Entry.Value; FObject Item = Obj();
        Item->SetStringField(TEXT("id"), Entry.Key); Item->SetNumberField(TEXT("doorFrame"), State.DoorFrame);
        Item->SetNumberField(TEXT("releaseRemaining"), State.ReleaseRemaining); Item->SetStringField(TEXT("state"), State.State);
        Item->SetNumberField(TEXT("stateStartedAt"), State.StateStartedAt); Item->SetNumberField(TEXT("doorAt"), B.SimulatedAt);
        Item->SetBoolField(TEXT("doorOpening"), State.State == TEXT("opening"));
        if (bSave)
        {
            Item->SetNumberField(TEXT("targetRefreshRemaining"), State.TargetRefreshRemaining);
            FArray Targets; for (const FString& Id : State.Targets) Targets.Add(MakeShared<FJsonValueString>(Id));
            Item->SetArrayField(TEXT("targets"), Targets);
        }
        BunkerStates.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("bunkers"), BunkerStates);
    if (bSave)
    {
        FObject Cooldowns = Obj();
        for (const TPair<FString, double>& P : B.DefenseCooldown) Cooldowns->SetNumberField(P.Key, FMath::Max(0.0, P.Value));
        O->SetObjectField(TEXT("defenseCooldown"), Cooldowns);
        FObject Bursts = Obj();
        for (const TPair<FString, FDefenseBurst>& P : B.DefenseBursts)
        {
            const FDefenseBurst& Burst = P.Value; FObject State = Obj();
            State->SetStringField(TEXT("phase"), Burst.Phase); State->SetNumberField(TEXT("remaining"), FMath::Max(0.0, Burst.Remaining));
            State->SetNumberField(TEXT("phaseSeconds"), Burst.PhaseSeconds); State->SetNumberField(TEXT("shotsRemaining"), Burst.ShotsRemaining);
            State->SetNumberField(TEXT("coolingStartFrame"), Burst.CoolingStartFrame);
            Bursts->SetObjectField(P.Key, State);
        }
        O->SetObjectField(TEXT("defenseBursts"), Bursts);
        WriteMapCombat(B, O);
    }
    return O;
}

bool FBYMSimulation::FImpl::LoadSave(const FObject& Saved, FString& Error)
{
    auto Fail = [&Error](const TCHAR* Reason) { Error = FString(TEXT("World simulation save is invalid: ")) + Reason + TEXT(". Original save has been preserved."); return false; };
    int32 Schema = 0; if (!Integer(Saved, TEXT("schema"), 1, 5, Schema)) return Fail(TEXT("unsupported schema"));
    const FArray* YA = nullptr;
    if (!Saved->TryGetArrayField(TEXT("yards"), YA) || !YA || YA->Num() > 100000) return Fail(TEXT("yard list missing or oversized"));
    for (const TSharedPtr<FJsonValue>& V : *YA)
    {
        if (!V.IsValid() || V->Type != EJson::Object) return Fail(TEXT("yard entry is not an object"));
        FObject O = V->AsObject(); FYard Y; Y.Id = Str(O, TEXT("id")); Y.Name = Str(O, TEXT("name"));
        if (!O->TryGetBoolField(TEXT("bot"), Y.bBot)) return Fail(TEXT("missing bot ownership flag"));
        if (Y.Id.IsEmpty() || Y.Id.Len() > 80 || Y.Name.IsEmpty() || Y.Name.Len() > 64 || Yards.Contains(Y.Id)) return Fail(TEXT("duplicate or malformed identity"));
        if (!ReadResources(Child(O, TEXT("resources")), Y.Resource)) return Fail(TEXT("invalid resource wallet"));
        const double RP = Number(O, TEXT("rp"), 2 * ResourceLimit);
        if (RP < -ResourceLimit || RP > ResourceLimit || RP != FMath::FloorToDouble(RP)) return Fail(TEXT("invalid ranking points"));
        Y.RP = static_cast<int64>(RP);
        double EarnedValue = 0., BaseValue = 0.;
        const bool HasEarned = O->TryGetNumberField(TEXT("earnedExperience"), EarnedValue);
        const bool HasBase = O->TryGetNumberField(TEXT("baseValue"), BaseValue);
        if ((Schema >= 4 && (!HasEarned || !HasBase))
            || (O->HasField(TEXT("earnedExperience")) && !HasEarned)
            || (O->HasField(TEXT("baseValue")) && !HasBase)
            || !BYMExperience::IsValidSavedPoints(EarnedValue) || !BYMExperience::IsValidSavedPoints(BaseValue))
            return Fail(TEXT("invalid or missing player experience counters"));
        Y.EarnedExperience = static_cast<int64>(EarnedValue); Y.BaseValue = static_cast<int64>(BaseValue);
        if (!Integer(O, TEXT("wins"), 0, 100000000, Y.Wins) || !Integer(O, TEXT("losses"), 0, 100000000, Y.Losses)) return Fail(TEXT("invalid win-loss record"));
        Y.ShieldUntil = Number(O, TEXT("shieldUntil"), -1); Y.LastEconomy = Number(O, TEXT("lastEconomy"), -1);
        if (O->HasField(TEXT("layoutRevision")) && !Integer(O, TEXT("layoutRevision"), 0, 1, Y.LayoutRevision)) return Fail(TEXT("invalid bot layout revision"));
        Y.NextDecision = Number(O, TEXT("nextDecision"), Now); Y.Decisions = static_cast<int32>(FMath::Clamp(Number(O, TEXT("decisions")), 0.0, 1000000000.0));
        if (Y.ShieldUntil < 0 || Y.LastEconomy < 0 || Y.ShieldUntil > Now + 31536000 || Y.LastEconomy > Now + 86400) return Fail(TEXT("invalid yard timestamps"));
        if (!ReadEconomy(Y, O, Error)) return false;
        TSet<FString> BuildingIds, BuildingJobIds;
        const FArray* BuildingArray = nullptr;
        if (!O->TryGetArrayField(TEXT("buildings"), BuildingArray) || !BuildingArray || BuildingArray->Num() > 1000) return Fail(TEXT("building list missing or oversized"));
        for (const TSharedPtr<FJsonValue>& BV : *BuildingArray)
        {
            if (!BV.IsValid() || BV->Type != EJson::Object) return Fail(TEXT("invalid building entry"));
            const FObject BO = BV->AsObject(); FBuilding B; B.Id = Str(BO, TEXT("id"));
            if (B.Id.IsEmpty() || B.Id.Len() > 80 || BuildingIds.Contains(B.Id) || !Integer(BO, TEXT("type"), 1, 100000, B.Type)) return Fail(TEXT("invalid building identity"));
            const FBuildingDef* D = Buildings.Find(B.Type); if (!D) return Fail(TEXT("building definition no longer exists"));
            int32 SavedX = 0, SavedY = 0;
            if (!Integer(BO, TEXT("level"), 0, D->Levels.Num(), B.Level) || !Integer(BO, TEXT("targetLevel"), 1, D->Levels.Num(), B.TargetLevel) || !Integer(BO, TEXT("x"), YardMinX(Y), YardMinX(Y) + YardWidth(Y), SavedX) || !Integer(BO, TEXT("y"), YardMinY(Y), YardMinY(Y) + YardHeight(Y), SavedY)) return Fail(TEXT("invalid building level or position"));
            B.X = SavedX; B.Y = SavedY;
            B.HP = Number(BO, TEXT("hp"), -1); B.ReadyAt = Number(BO, TEXT("readyAt"), -1); B.Stored = Number(BO, TEXT("stored"), -1); B.bRepairing = Flag(BO, TEXT("repairing"));
            // Missing in schema-1/2 and early schema-3 fixtures: start a fresh
            // source cycle without deleting previously accrued collector stock.
            if (BO->HasField(TEXT("productionRemaining"))
                && (!BO->TryGetNumberField(TEXT("productionRemaining"), B.ProductionRemaining)
                    || !FMath::IsFinite(B.ProductionRemaining) || B.ProductionRemaining < 0.
                    || B.ProductionRemaining > 31536000. || ((B.Type < 1 || B.Type > 4) && B.ProductionRemaining != 0.)))
                return Fail(TEXT("invalid collector production countdown"));

            const FLevel* L = Stats(B);
            if (!L || !FMath::IsFinite(B.HP) || !FMath::IsFinite(B.ReadyAt) || !FMath::IsFinite(B.Stored)
                || B.HP < 0 || B.HP > L->HP || B.ReadyAt < 0 || B.ReadyAt > Now + 31536000
                || B.Stored < 0 || B.Stored > L->Capacity || !Fits(Y, B.X, B.Y, D->Cells)) return Fail(TEXT("invalid building state or footprint overlap"));
            if (Schema < 4)
            {
                if ((B.Level == 0 && B.ReadyAt == 0)
                    || (B.ReadyAt == 0 && (B.TargetLevel != B.Level || B.bRepairing))
                    || (B.ReadyAt > 0 && (B.TargetLevel < B.Level || B.TargetLevel > B.Level + 1 || (B.bRepairing && B.TargetLevel != B.Level))))
                    return Fail(TEXT("inconsistent legacy construction or repair state"));
                if (HasWork(B))
                {
                    B.JobId = NewId();
                    B.WorkRemaining = BYMBuildingLifecycle::MigrateRemaining(B.ReadyAt, Y.LastEconomy, Now);
                }
            }
            else
            {
                if (!BO->TryGetBoolField(TEXT("repairing"), B.bRepairing)
                    || !BO->TryGetStringField(TEXT("jobId"), B.JobId)
                    || !BO->TryGetNumberField(TEXT("workRemaining"), B.WorkRemaining)
                    || !FMath::IsFinite(B.WorkRemaining) || B.WorkRemaining < 0 || B.WorkRemaining > 31536000
                    || B.TargetLevel < B.Level || B.TargetLevel > B.Level + 1
                    || (HasWork(B) ? (B.JobId.IsEmpty() || B.JobId.Len() > 64 || BuildingJobIds.Contains(B.JobId)) : (!B.JobId.IsEmpty() || B.WorkRemaining != 0.)))
                    return Fail(TEXT("invalid building work state or job identity"));
                if (HasWork(B)) BuildingJobIds.Add(B.JobId);
            }
            if (BO->HasField(TEXT("speedupRevision")) && (!Integer(BO, TEXT("speedupRevision"), 0, 1000000000, B.SpeedupRevision)
                || (!HasWork(B) && B.SpeedupRevision != 0))) return Fail(TEXT("invalid building speedup revision"));
            if (BO->HasField(TEXT("garrison")) || (Schema >= 5 && B.Type == 22))
            {
                const FObject Stock = Child(BO, TEXT("garrison"));
                if (!Stock || Stock->Values.Num() > 14 || (B.Type != 22 && !Stock->Values.IsEmpty())) return Fail(TEXT("invalid Bunker roster"));
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Stock->Values)
                {
                    int32 Count = 0;
                    if (B.Type != 22 || B.Level <= 0 || B.HP <= 0 || !BYMBunkerRules::IsAllowedSpecies(Entry.Key)
                        || !Monsters.Contains(Entry.Key) || !Integer(Stock, *Entry.Key, 1, 1024, Count)) return Fail(TEXT("invalid garrison species or count"));
                    B.Garrison.Add(Entry.Key, Count);
                }
            }
            BuildingIds.Add(B.Id); Y.Buildings.Add(MoveTemp(B));
        }
        if (CountOf(Y, 14) != 1) return Fail(TEXT("yard must contain exactly one town hall"));
        if (!O->HasField(TEXT("economy"))) InitializeEconomy(Y, true);
        for (const FYard::FMushroom& M : Y.Mushrooms) if (BuildingIds.Contains(M.Id)) return Fail(TEXT("mushroom shares a building identity"));
        if (!ReadWorkers(Y, O, Error)) return false;
        const FObject Army = Child(O, TEXT("army")); if (!Army.IsValid() || Army->Values.Num() > 100) return Fail(TEXT("invalid army"));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Army->Values)
        {
            int32 Count = 0; if (!Monsters.Contains(P.Key) || !Integer(Army, *P.Key, 0, 100000, Count)) return Fail(TEXT("unknown or invalid monster count"));
            Y.Army.Add(P.Key, Count);
        }
        if (Schema < 3)
        {
            const FArray* TrainingArray = nullptr;
            if (!O->TryGetArrayField(TEXT("training"), TrainingArray) || !TrainingArray || TrainingArray->Num() > 200) return Fail(TEXT("missing or oversized training queue"));
            for (const TSharedPtr<FJsonValue>& TV : *TrainingArray)
            {
                if (!TV.IsValid() || TV->Type != EJson::Object) return Fail(TEXT("invalid training entry"));
                FTraining T; const FObject TO = TV->AsObject(); T.Type = Str(TO, TEXT("type")); T.ReadyAt = Number(TO, TEXT("readyAt"), -1);
                if (!Monsters.Contains(T.Type) || !Integer(TO, TEXT("count"), 1, 100, T.Count) || T.ReadyAt < 0 || T.ReadyAt > Now + 31536000) return Fail(TEXT("invalid training state"));
                Y.Queue.Add(MoveTemp(T));
            }
        }
        if (Schema == 1)
        {
            // Old worlds had no unlock records. Preserve known owned/queued
            // species at level one, without discarding armies or granting levels.
            // A zero-count army key remains after the last troop is deployed, so
            // it also proves prior ownership and must not re-lock that species.
            for (const TPair<FString, FMonsterDef>& P : Monsters) if (P.Value.bInitiallyUnlocked) Y.MonsterLevels.Add(P.Key, 1);
            for (const TPair<FString, int32>& P : Y.Army) Y.MonsterLevels.Add(P.Key, 1);
            for (const FTraining& T : Y.Queue) Y.MonsterLevels.Add(T.Type, 1);
        }
        else
        {
            const FObject Levels = Child(O, TEXT("monsterLevels"));
            if (!Levels.IsValid() || Levels->Values.Num() > 100) return Fail(TEXT("missing or oversized monster progression"));
            for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Levels->Values)
            {
                const FMonsterDef* D = Monsters.Find(P.Key); int32 Level = 0;
                if (!D || !Integer(Levels, *P.Key, 1, D->Levels.Num(), Level)) return Fail(TEXT("invalid researched monster level"));
                Y.MonsterLevels.Add(P.Key, Level);
            }
            for (const TPair<FString, FMonsterDef>& P : Monsters)
                if (P.Value.bInitiallyUnlocked && MonsterLevel(Y, P.Key) < 1) return Fail(TEXT("initial unlocked monster is missing"));
            for (const TPair<FString, int32>& P : Y.Army)
                if (P.Value > 0 && MonsterLevel(Y, P.Key) < 1) return Fail(TEXT("owned monster is locked"));
            for (const FTraining& T : Y.Queue) if (MonsterLevel(Y, T.Type) < 1) return Fail(TEXT("queued monster is locked"));

            auto ReadJob = [&](const FObject& JO, FMonsterJob& Job, bool bResearch) -> bool
            {
                if (!JO.IsValid()) return false;
                Job.Type = Str(JO, TEXT("type")); const FMonsterDef* D = Monsters.Find(Job.Type);
                if (!D || !IsOperationalMonster(Job.Type) || D->bSourceFake || D->bSourceBlocked) return false;
                Job.StartedAt = Number(JO, TEXT("startedAt"), -1); Job.ReadyAt = Number(JO, TEXT("readyAt"), -1);
                int32 Paid = 0;
                if (!Integer(JO, TEXT("puttyPaid"), 0, static_cast<int32>(ResourceLimit), Paid)
                    || Job.StartedAt < 0 || Job.StartedAt > Now + 86400 || Job.ReadyAt <= Job.StartedAt
                    || Job.ReadyAt - Job.StartedAt > 31536000 || Job.ReadyAt > Now + 31536000) return false;
                Job.PuttyPaid = Paid;
                if (!bResearch) return MonsterLevel(Y, Job.Type) == 0 && Job.PuttyPaid == D->UnlockPutty;
                Job.Building = Str(JO, TEXT("building")); const FBuilding* Academy = FindBuilding(Y, Job.Building);
                if (!Integer(JO, TEXT("fromLevel"), 1, D->Levels.Num(), Job.FromLevel)
                    || !Integer(JO, TEXT("toLevel"), 2, D->Levels.Num(), Job.ToLevel)
                    || Job.ToLevel != Job.FromLevel + 1 || MonsterLevel(Y, Job.Type) != Job.FromLevel
                    || !D->Research.IsValidIndex(Job.FromLevel - 1) || !Academy || Academy->Type != 26) return false;
                const FMonsterResearchDef& R = D->Research[Job.FromLevel - 1];
                return Job.PuttyPaid == R.Putty && Academy->Level >= R.AcademyLevel;
            };
            const FObject Unlock = Child(O, TEXT("monsterUnlock"));
            if (!Unlock.IsValid() || (!Unlock->Values.IsEmpty() && !ReadJob(Unlock, Y.MonsterUnlock, false)))
                return Fail(TEXT("invalid monster unlock job"));
            const FArray* Research = nullptr;
            if (!O->TryGetArrayField(TEXT("monsterResearch"), Research) || !Research || Research->Num() > 1000)
                return Fail(TEXT("missing or oversized monster research jobs"));
            TSet<FString> ResearchTypes, ResearchBuildings;
            for (const TSharedPtr<FJsonValue>& VJob : *Research)
            {
                FMonsterJob Job;
                if (!VJob.IsValid() || VJob->Type != EJson::Object || !ReadJob(VJob->AsObject(), Job, true)
                    || ResearchTypes.Contains(Job.Type) || ResearchBuildings.Contains(Job.Building))
                    return Fail(TEXT("invalid or duplicate monster research job"));
                ResearchTypes.Add(Job.Type); ResearchBuildings.Add(Job.Building); Y.MonsterResearch.Add(MoveTemp(Job));
            }
        }
        if (Schema < 3)
        {
            // Preserve every paid legacy creature. Old previews used one global
            // completion schedule; attach it to the first existing Hatchery and
            // retain the intervals between those deadlines. Excess old entries
            // are grandfathered, never truncated to the new group capacity.
            const FBuilding* Hatchery = Y.Buildings.FindByPredicate([](const FBuilding& B) { return B.Type == 13; });
            if (!Y.Queue.IsEmpty() && !Hatchery) return Fail(TEXT("legacy paid queue has no Hatchery"));
            Y.Queue.StableSort([](const FTraining& A, const FTraining& B) { return A.ReadyAt < B.ReadyAt; });
            // Advance bounds offline catch-up to one year. Use the same anchor
            // here so older paid deadlines are not delayed by the elapsed time
            // that Advance deliberately skips. No paid entries are discarded.
            TArray<FTraining> Migrated;
            double Previous = FMath::Max(0.0, FMath::Min(Now, FMath::Max(Y.LastEconomy, Now - 31536000.0)));
            for (const FTraining& Old : Y.Queue)
            {
                for (int32 Unit = 0; Unit < Old.Count; ++Unit)
                {
                    FTraining Entry; Entry.Id = NewId(); Entry.Building = Hatchery->Id; Entry.Type = Old.Type;
                    Entry.Count = 1; Entry.bLegacy = true; Entry.Remaining = FMath::Max(0.0, Old.ReadyAt - Previous);
                    Previous = FMath::Max(Previous, Old.ReadyAt); Migrated.Add(MoveTemp(Entry));
                }
            }
            Y.Queue = MoveTemp(Migrated);
        }
        else
        {
            const FArray* Hatcheries = nullptr;
            if (!O->TryGetArrayField(TEXT("hatcheries"), Hatcheries) || !Hatcheries || Hatcheries->Num() > 1000
                || Hatcheries->Num() != CountOf(Y, 13)) return Fail(TEXT("missing or inconsistent Hatchery state"));
            TSet<FString> HatcheryIds, EntryIds; int32 TotalCount = 0;
            for (const TSharedPtr<FJsonValue>& HatcheryValue : *Hatcheries)
            {
                if (!HatcheryValue.IsValid() || HatcheryValue->Type != EJson::Object) return Fail(TEXT("invalid Hatchery entry"));
                const FObject HO = HatcheryValue->AsObject(); const FString BuildingId = Str(HO, TEXT("building"));
                const FBuilding* Hatchery = FindBuilding(Y, BuildingId);
                if (!Hatchery || Hatchery->Type != 13 || HatcheryIds.Contains(BuildingId)) return Fail(TEXT("duplicate or foreign Hatchery"));
                HatcheryIds.Add(BuildingId); bool bLegacyWaiting = false;
                auto ReadEntry = [&](const FObject& EntryObject, bool bActive) -> bool
                {
                    if (!EntryObject.IsValid()) return false;
                    FTraining Entry; Entry.Id = Str(EntryObject, TEXT("id")); Entry.Type = Str(EntryObject, TEXT("type"));
                    Entry.Building = BuildingId; Entry.bActive = bActive;
                    if (Entry.Id.IsEmpty() || Entry.Id.Len() > 80 || EntryIds.Contains(Entry.Id)
                        || !Monsters.Contains(Entry.Type) || MonsterLevel(Y, Entry.Type) < 1
                        || !Integer(EntryObject, TEXT("count"), 1, bActive ? 1 : 20, Entry.Count)
                        || !EntryObject->TryGetBoolField(TEXT("legacy"), Entry.bLegacy)) return false;
                    if (EntryObject->HasField(TEXT("priceScale")) && (!Integer(EntryObject, TEXT("priceScale"), 1, 3, Entry.PriceScale) || Entry.PriceScale == 2)) return false;
                    Entry.Remaining = Number(EntryObject, TEXT("remaining"), -1);
                    if (Entry.Remaining < 0 || Entry.Remaining > (Entry.bLegacy ? Now + 31536000.0 : 86400.0)
                        || (!bActive && !Entry.bLegacy && Entry.Remaining != 0) || (Entry.bLegacy && Entry.Count != 1)) return false;
                    TotalCount += Entry.Count; if (TotalCount > 100000) return false;
                    if (!bActive && Entry.bLegacy) bLegacyWaiting = true;
                    EntryIds.Add(Entry.Id); Y.Queue.Add(MoveTemp(Entry)); return true;
                };
                const FObject Active = Child(HO, TEXT("active"));
                if (!Active.IsValid() || (!Active->Values.IsEmpty() && !ReadEntry(Active, true))) return Fail(TEXT("invalid active Hatchery creature"));
                const FArray* Waiting = nullptr;
                if (!HO->TryGetArrayField(TEXT("queue"), Waiting) || !Waiting || Waiting->Num() > 20000) return Fail(TEXT("missing or oversized Hatchery waiting queue"));
                for (const TSharedPtr<FJsonValue>& WaitingValue : *Waiting)
                    if (!WaitingValue.IsValid() || WaitingValue->Type != EJson::Object || !ReadEntry(WaitingValue->AsObject(), false)) return Fail(TEXT("invalid Hatchery waiting group"));
                if (!bLegacyWaiting && Waiting->Num() > 1 + Hatchery->Level) return Fail(TEXT("Hatchery waiting groups exceed its level capacity"));
            }
        }
        for (const TSharedPtr<FJsonValue>& LV : Array(O, TEXT("log")))
            if (LV.IsValid() && LV->Type == EJson::Object && Y.Log.Num() < 20) Y.Log.Add(LV->AsObject());
        int32 GarrisonCount = 0;
        for (const FBuilding& Building : Y.Buildings) if (Building.Type == 22)
        {
            for (const TPair<FString, int32>& Entry : Building.Garrison)
            {
                if (Schema == 1) Y.MonsterLevels.FindOrAdd(Entry.Key) = 1;
                if (MonsterLevel(Y, Entry.Key) <= 0) return Fail(TEXT("garrison contains a locked monster"));
                GarrisonCount += Entry.Value;
            }
            if (BunkerUsed(Y, Building) > BYMBunkerRules::Capacity(Building.Level)) return Fail(TEXT("garrison exceeds ordinary Bunker capacity"));
        }
        if (GarrisonCount > 1024) return Fail(TEXT("garrison count exceeds the defensive simulation bound"));
        // Older worlds have no recorded action history. Retain any valid
        // counters, derive only known completed value, and award no past actions.
        RefreshBaseValue(Y);
        YardIds.Add(Y.Id); if (Y.bBot) BotIds.Add(Y.Id); { const FString YardId = Y.Id; Yards.Add(YardId, MoveTemp(Y)); }
    }
    if (!LoadMap(Saved, Error)) return false;
    const FArray* BattleArray = nullptr;
    if (!Saved->TryGetArrayField(TEXT("battles"), BattleArray) || !BattleArray || BattleArray->Num() > 100) return Fail(TEXT("battle list missing or oversized"));
    TSet<FString> Engaged;
    for (const TSharedPtr<FJsonValue>& V : *BattleArray)
    {
        if (!V.IsValid() || V->Type != EJson::Object) return Fail(TEXT("invalid battle entry"));
        const FObject O = V->AsObject(); FBattle B; B.Id = Str(O, TEXT("id")); B.Attacker = Str(O, TEXT("attacker")); B.Defender = Str(O, TEXT("defender"));
        B.MapCellId = Str(O, TEXT("mapCellId"));
        if (!B.MapCellId.IsEmpty()) { const FMapCell* Cell = MapCells.Find(B.MapCellId); if (!Cell || Cell->YardId != B.Defender || Cell->Owner == B.Attacker || Cell->DestroyedUntil > Now) return Fail(TEXT("invalid map battle ownership")); }
        else if (IsMapSite(B.Defender)) return Fail(TEXT("territory battle is missing its map identity"));
        B.StartedAt = Number(O, TEXT("startedAt"), -1); B.EndsAt = Number(O, TEXT("endsAt"), -1); B.SimulatedAt = Now;
        B.InitialBuildingHP = Number(O, TEXT("initialBuildingHP"), -1); B.InitialBuildings = static_cast<int32>(Number(O, TEXT("initialBuildings")));
        if (B.Id.IsEmpty() || !Yards.Contains(B.Attacker) || !Yards.Contains(B.Defender) || B.Attacker == B.Defender || Engaged.Contains(B.Attacker) || Engaged.Contains(B.Defender) || B.StartedAt < 0 || B.EndsAt < B.StartedAt || B.EndsAt > Now + 3600.0 || B.InitialBuildingHP <= 0 || B.InitialBuildings <= 0 || !Integer(O, TEXT("deployed"), 0, 500, B.Deployed)) return Fail(TEXT("invalid battle ownership or timestamps"));
        const FYard& BattleYard = Yards.FindChecked(B.Defender);
        const FArray* TroopArray = nullptr;
        if (!O->TryGetArrayField(TEXT("troops"), TroopArray) || !TroopArray || TroopArray->Num() > 500) return Fail(TEXT("missing or oversized battle army"));
        // Dead troops remain in the battle record, so every committed deployment must have exactly one saved troop.
        if (TroopArray->Num() != B.Deployed) return Fail(TEXT("deployed count does not match the saved battle army"));
        TSet<FString> TroopIds;
        for (const TSharedPtr<FJsonValue>& TV : *TroopArray)
        {
            if (!TV.IsValid() || TV->Type != EJson::Object) return Fail(TEXT("invalid troop entry"));
            const FObject TO = TV->AsObject(); FTroop T; T.Id = Str(TO, TEXT("id")); T.Type = Str(TO, TEXT("type")); T.Target = Str(TO, TEXT("target")); T.TargetCreep = Str(TO, TEXT("targetCreep"));
            const FMonsterDef* D = Monsters.Find(T.Type); T.HP = Number(TO, TEXT("hp"), -1); T.Position = FVector2D(Number(TO, TEXT("x"), -1), Number(TO, TEXT("y"), -1));
            if (!D || (Schema >= 2 && !Integer(TO, TEXT("level"), 1, D->Levels.Num(), T.Level))) return Fail(TEXT("invalid deployed monster level"));
            if (Schema == 1) Yards.FindChecked(B.Attacker).MonsterLevels.FindOrAdd(T.Type) = 1;
            if (T.Id.IsEmpty() || T.Id.Len() > 80 || TroopIds.Contains(T.Id) || T.HP < 0 || T.HP > MonsterStats(*D, T.Level).HP
                || T.Level > MonsterLevel(Yards.FindChecked(B.Attacker), T.Type)
                || T.Position.X < YardMinX(BattleYard) || T.Position.Y < YardMinY(BattleYard) || T.Position.X > YardMinX(BattleYard) + YardWidth(BattleYard) || T.Position.Y > YardMinY(BattleYard) + YardHeight(BattleYard)) return Fail(TEXT("invalid or duplicate troop state"));
            if (!T.Target.IsEmpty() && !FindBuilding(Yards.FindChecked(B.Defender), T.Target)) return Fail(TEXT("invalid troop target"));
            // Optional schema-2 addition: older saves resume ready to attack.
            if (TO->HasField(TEXT("attackCooldown")))
            {
                T.AttackCooldown = Number(TO, TEXT("attackCooldown"), -1);
                if (T.AttackCooldown < 0 || T.AttackCooldown > 60) return Fail(TEXT("invalid troop attack cooldown"));
            }
            T.TargetPosition = T.Position;
            TroopIds.Add(T.Id); B.Troops.Add(MoveTemp(T));
        }
        const FYard& DefendingYard = Yards.FindChecked(B.Defender);
        const FArray* DefenderArray = nullptr;
        if (Schema >= 5 || O->HasField(TEXT("defenders")))
        {
            if (!O->TryGetArrayField(TEXT("defenders"), DefenderArray) || !DefenderArray || DefenderArray->Num() > 1024)
                return Fail(TEXT("missing or oversized Bunker defenders"));
            for (const TSharedPtr<FJsonValue>& DefenderValue : *DefenderArray)
            {
                if (!DefenderValue.IsValid() || DefenderValue->Type != EJson::Object) return Fail(TEXT("invalid defensive monster"));
                const FObject UnitObject = DefenderValue->AsObject(); FBunkerDefender Unit;
                Unit.Id = Str(UnitObject, TEXT("id")); Unit.Type = Str(UnitObject, TEXT("type")); Unit.Bunker = Str(UnitObject, TEXT("bunker"));
                Unit.Target = Str(UnitObject, TEXT("target"));
                const FBuilding* HomeBunker = FindBuilding(DefendingYard, Unit.Bunker); const FMonsterDef* Definition = Monsters.Find(Unit.Type);
                Unit.HP = Number(UnitObject, TEXT("hp"), -1); Unit.AttackCooldown = Number(UnitObject, TEXT("attackCooldown"), -1);
                Unit.RetargetRemaining = Number(UnitObject, TEXT("retargetRemaining"), Schema >= 5 ? -1 : 0);
                Unit.Position = FVector2D(Number(UnitObject, TEXT("x"), -1), Number(UnitObject, TEXT("y"), -1));
                if (!HomeBunker || HomeBunker->Type != 22 || HomeBunker->Level <= 0 || !Definition || !BYMBunkerRules::IsAllowedSpecies(Unit.Type)
                    || Unit.Id.IsEmpty() || Unit.Id.Len() > 80 || TroopIds.Contains(Unit.Id)
                    || !Integer(UnitObject, TEXT("level"), 1, Definition->Levels.Num(), Unit.Level)
                    || Unit.Level > MonsterLevel(DefendingYard, Unit.Type)
                    || !UnitObject->TryGetBoolField(TEXT("returned"), Unit.bReturned) || !UnitObject->TryGetBoolField(TEXT("deathHandled"), Unit.bDeathHandled)
                    || Unit.HP < 0 || Unit.HP > MonsterStats(*Definition, Unit.Level).HP || Unit.AttackCooldown < 0 || Unit.AttackCooldown > 60
                    || Unit.RetargetRemaining < 0 || Unit.RetargetRemaining > 150.0 / 40.0
                    || !Str(UnitObject, TEXT("targetCreep")).IsEmpty()
                    || Unit.Position.X < YardMinX(BattleYard) || Unit.Position.Y < YardMinY(BattleYard) || Unit.Position.X > YardMinX(BattleYard) + YardWidth(BattleYard) || Unit.Position.Y > YardMinY(BattleYard) + YardHeight(BattleYard)
                    || (Unit.bDeathHandled != (Unit.HP <= 0)) || (Unit.bReturned && (Unit.HP <= 0 || !Unit.Target.IsEmpty() || HomeBunker->HP <= 0))
                    || (!Unit.Target.IsEmpty() && !B.Troops.ContainsByPredicate([&](const FTroop& Attacker)
                        { const FMonsterDef* TargetDefinition = Monsters.Find(Attacker.Type); return Attacker.Id == Unit.Target && TargetDefinition && !TargetDefinition->bFlying; })))
                    return Fail(TEXT("invalid defensive monster ownership or state"));
                Unit.TargetPosition = Unit.Position; Unit.State = Unit.bReturned ? TEXT("returned") : (Unit.HP <= 0 ? TEXT("dead") : TEXT("idle"));
                TroopIds.Add(Unit.Id); B.Defenders.Add(MoveTemp(Unit));
            }
        }
        for (const FTroop& Attacker : B.Troops) if (!Attacker.TargetCreep.IsEmpty()
            && !B.Defenders.ContainsByPredicate([&](const FBunkerDefender& Unit) { return Unit.Id == Attacker.TargetCreep; })) return Fail(TEXT("invalid attacker defender target"));
        for (const FBuilding& HomeBunker : DefendingYard.Buildings) if (HomeBunker.Type == 22 && HomeBunker.HP > 0)
        {
            TMap<FString, int32> Alive;
            for (const FBunkerDefender& Unit : B.Defenders) if (Unit.Bunker == HomeBunker.Id && Unit.HP > 0) ++Alive.FindOrAdd(Unit.Type);
            for (const TPair<FString, int32>& Entry : Alive) if (Entry.Value > HomeBunker.Garrison.FindRef(Entry.Key)) return Fail(TEXT("defenders exceed their owned Bunker stock"));
        }
        const FArray* StateArray = nullptr;
        if (Schema >= 5 || O->HasField(TEXT("bunkers")))
        {
            if (!O->TryGetArrayField(TEXT("bunkers"), StateArray) || !StateArray || StateArray->Num() > 1000) return Fail(TEXT("missing or oversized Bunker release states"));
            for (const TSharedPtr<FJsonValue>& StateValue : *StateArray)
            {
                if (!StateValue.IsValid() || StateValue->Type != EJson::Object) return Fail(TEXT("invalid Bunker release entry"));
                const FObject StateObject = StateValue->AsObject(); const FString BuildingId = Str(StateObject, TEXT("id"));
                const FBuilding* HomeBunker = FindBuilding(DefendingYard, BuildingId); FBunkerState State;
                State.DoorFrame = Number(StateObject, TEXT("doorFrame"), -1); State.ReleaseRemaining = Number(StateObject, TEXT("releaseRemaining"), -1);
                State.State = Str(StateObject, TEXT("state")); State.StateStartedAt = Number(StateObject, TEXT("stateStartedAt"), -1);
                State.TargetRefreshRemaining = Number(StateObject, TEXT("targetRefreshRemaining"), Schema >= 5 ? -1 : 0);
                if (!HomeBunker || HomeBunker->Type != 22 || B.Bunkers.Contains(BuildingId)
                    || State.DoorFrame < 0 || State.DoorFrame > BYMBunkerRules::DoorFrames
                    || State.ReleaseRemaining < 0 || State.ReleaseRemaining > BYMBunkerRules::ReleaseIntervalSeconds + .000001
                    || State.StateStartedAt < 0 || State.StateStartedAt > Now + 86400
                    || State.TargetRefreshRemaining < 0 || State.TargetRefreshRemaining > 60.0 / 40.0
                    || (State.State != TEXT("closed") && State.State != TEXT("opening") && State.State != TEXT("open") && State.State != TEXT("closing"))
                    || (State.State == TEXT("closed") && State.DoorFrame != 0) || (State.State == TEXT("open") && State.DoorFrame != BYMBunkerRules::DoorFrames))
                    return Fail(TEXT("invalid Bunker door or release countdown"));
                const FArray* CachedTargets = nullptr;
                if (Schema >= 5 || StateObject->HasField(TEXT("targets")))
                {
                    if (!StateObject->TryGetArrayField(TEXT("targets"), CachedTargets) || !CachedTargets || CachedTargets->Num() > 3)
                        return Fail(TEXT("invalid Bunker cached targets"));
                    for (const TSharedPtr<FJsonValue>& TargetValue : *CachedTargets)
                    {
                        if (!TargetValue.IsValid() || TargetValue->Type != EJson::String) return Fail(TEXT("invalid Bunker cached target"));
                        const FString TargetId = TargetValue->AsString();
                        if (TargetId.IsEmpty() || State.Targets.Contains(TargetId)
                            || !B.Troops.ContainsByPredicate([&](const FTroop& Attacker)
                                { const FMonsterDef* Definition = Monsters.Find(Attacker.Type); return Attacker.Id == TargetId && Definition && !Definition->bFlying; }))
                            return Fail(TEXT("invalid Bunker cached target ownership"));
                        State.Targets.Add(TargetId);
                    }
                }
                B.Bunkers.Add(BuildingId, State);
            }
        }
        for (const FBunkerDefender& Unit : B.Defenders) if (!B.Bunkers.Contains(Unit.Bunker)) return Fail(TEXT("defender has no Bunker release state"));
        const FObject Cooldowns = Child(O, TEXT("defenseCooldown"));
        if (!Cooldowns.IsValid() || Cooldowns->Values.Num() > 1000) return Fail(TEXT("missing or invalid defense cooldowns"));
        const FYard& Defender = Yards.FindChecked(B.Defender);
        for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Cooldowns->Values)
        {
            const double Value = Number(Cooldowns, *P.Key, -1);
            if (!FindBuilding(Defender, P.Key) || Value < 0 || Value > 60) return Fail(TEXT("invalid defense cooldown"));
            B.DefenseCooldown.Add(P.Key, Value);
        }
        if (O->HasField(TEXT("defenseBursts")))
        {
            const FObject Bursts = Child(O, TEXT("defenseBursts"));
            if (!Bursts.IsValid() || Bursts->Values.Num() > 1000) return Fail(TEXT("invalid defense burst table"));
            for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Bursts->Values)
            {
                const FBuilding* Building = FindBuilding(Defender, P.Key);
                const FObject State = Child(Bursts, *P.Key); FDefenseBurst Burst;
                if (!Building || Building->Type != 25 || !State.IsValid()) return Fail(TEXT("invalid burst defense identity"));
                const FLevel* L = Stats(*Building);
                Burst.Phase = Str(State, TEXT("phase")); Burst.Remaining = Number(State, TEXT("remaining"), -1);
                Burst.PhaseSeconds = Number(State, TEXT("phaseSeconds"), -1);
                if ((Burst.Phase != TEXT("idle") && Burst.Phase != TEXT("charging") && Burst.Phase != TEXT("active") && Burst.Phase != TEXT("cooling"))
                    || Burst.Remaining < 0 || Burst.PhaseSeconds < 0 || Burst.PhaseSeconds > 5 || Burst.Remaining > Burst.PhaseSeconds
                    || !L || !Integer(State, TEXT("shotsRemaining"), 0, L->BurstShots, Burst.ShotsRemaining)
                    || !Integer(State, TEXT("coolingStartFrame"), 32, 40, Burst.CoolingStartFrame)
                    || (Burst.Phase == TEXT("active") && Burst.ShotsRemaining == 0)) return Fail(TEXT("invalid defense burst state"));
                const bool bRest = Burst.Phase == TEXT("idle") || Burst.Phase == TEXT("active");
                const double ExpectedDuration = Burst.Phase == TEXT("charging") ? 32.0 / 40.0 : (55 - Burst.CoolingStartFrame) * 2.0 / 40.0;
                if ((bRest && (Burst.PhaseSeconds != 0 || Burst.Remaining != 0))
                    || (!bRest && FMath::Abs(Burst.PhaseSeconds - ExpectedDuration) > 0.000001)
                    || (Burst.Phase == TEXT("idle") && Burst.ShotsRemaining != 0)
                    || (Burst.Phase == TEXT("charging") && Burst.ShotsRemaining != L->BurstShots)
                    || !B.DefenseCooldown.Contains(P.Key)
                    || B.DefenseCooldown.FindRef(P.Key) > (Burst.Phase == TEXT("active") ? L->AttackInterval + 0.000001 : 0.000001))
                    return Fail(TEXT("inconsistent defense burst phase"));
                Burst.StartedAt = Now - FMath::Max(0.0, Burst.PhaseSeconds - Burst.Remaining);
                B.DefenseBursts.Add(P.Key, Burst);
            }
        }
        if (!ReadMapCombat(B, O, Error)) return false;
        Engaged.Add(B.Attacker); Engaged.Add(B.Defender); const FString AttackerId = B.Attacker; Battles.Add(AttackerId, MoveTemp(B));
    }
    BotIds.Sort(); YardIds.Sort(); return true;
}

namespace BYMSim
{
    bool IsOperationalBuilding(int32 Type)
    {
        switch (Type)
        {
            case 1: case 2: case 3: case 4: case 5: case 6: case 8: case 11: case 12: case 13: case 14: case 15:
            case 17: case 18: case 20: case 21: case 22: case 23: case 25: case 26: return true;
            default: return false;
        }
    }
    bool IsOperationalMonster(const FString& Type)
    {
        if (!Type.StartsWith(TEXT("C")) || Type.Len() > 3) return false;
        const FString Digits = Type.Mid(1); if (!Digits.IsNumeric()) return false;
        const int32 N = FCString::Atoi(*Digits);
        // Healers and monsters with special attacks need separate behavior ports; do not mislabel them as fully recreated.
        return N >= 1 && N <= 19 && N != 15 && N != 16;
    }
}

bool FBYMSimulation::FImpl::HasDeployableReserves(const FBattle& B) const
{
    const FYard* Attacker = Yards.Find(B.Attacker);
    if (!Attacker || B.Deployed >= TroopLimit()) return false;
    const int64 Remaining = FlingerCapacity(*Attacker) - FlingerUsed(B);
    for (const TPair<FString, int32>& Entry : Attacker->Army)
    {
        const FMonsterDef* M = Monsters.Find(Entry.Key);
        if (Entry.Value > 0 && M && IsOperationalMonster(Entry.Key) && MonsterStats(*Attacker, *M).Housing <= Remaining) return true;
    }
    return false;
}

FString FBYMSimulation::FImpl::MissingRequirements(const FYard& Y, const FLevel& L) const
{
    for (const FRequirement& R : L.Requirements)
    {
        if (CountOf(Y, R.Type, R.Level) >= R.Count) continue;
        const FBuildingDef* Required = Buildings.Find(R.Type);
        const FString Name = Required ? Required->Name : FString::Printf(TEXT("building %d"), R.Type);
        return FString::Printf(TEXT("Requires %d completed %s at level %d."), R.Count, *Name, R.Level);
    }
    return FString();
}

FString FBYMSimulation::FImpl::MissingResources(const FYard& Y, const FLevel& L) const
{
    TArray<FString> Missing;
    for (int32 I = 0; I < 4; ++I) if (Y.Resource[I] < L.Cost[I])
        Missing.Add(FString::Printf(TEXT("%lld %s"), static_cast<long long>(L.Cost[I] - Y.Resource[I]), Resources[I]));
    return Missing.IsEmpty() ? FString() : TEXT("Need ") + FString::Join(Missing, TEXT(", ")) + TEXT(" more.");
}

FString FBYMSimulation::FImpl::BuildReason(const FYard& Y, const FBuildingDef& D) const
{
    if (!D.bBuildable || !IsOperationalBuilding(D.Id)) return TEXT("This building is not available in this yard.");
    if (Busy(Y.Id)) return TEXT("Finish the current invasion or defense before constructing buildings.");
    if (D.Levels[0].SourceSeconds > 0 && ConstructionBusy(Y)) return TEXT("All workers are busy. Wait for a task to finish or hire a worker in the General Store.");
    if (Y.Buildings.Num() >= FMath::Clamp(GetDefault<UBYMSettings>()->MaxBuildingsPerYard, 1, 1000))
        return TEXT("This yard has reached the configured building limit.");
    const int32 Owned = CountOf(Y, D.Id), Allowed = QuantityLimit(Y, D);
    if (Owned >= Allowed)
    {
        const int32 Next = NextQuantityLevel(Y, D);
        return Next > 0 ? FString::Printf(TEXT("Town Hall level %d allows more %s (%d / %d built)."), Next, *D.Name, Owned, Allowed)
            : FString::Printf(TEXT("Maximum quantity reached for %s (%d / %d built)."), *D.Name, Owned, Allowed);
    }
    const FLevel& L = D.Levels[0];
    const FString Requirements = MissingRequirements(Y, L);
    return Requirements.IsEmpty() ? MissingResources(Y, L) : Requirements;
}

FString FBYMSimulation::FImpl::UpgradeReason(const FYard& Y, const FBuilding& B) const
{
    const FBuildingDef* D = Buildings.Find(B.Type);
    const FLevel* L = Stats(B);
    if (!D || !L) return TEXT("Building data is unavailable.");
    if (B.Level == 0 || HasWork(B) || B.bRepairing) return TEXT("Finish this building\'s construction, upgrade or repair first.");
    if (!D->Levels.IsValidIndex(B.Level)) return TEXT("This building is at its maximum level.");
    if (Busy(Y.Id)) return TEXT("Finish the current invasion or defense before upgrading.");
    if (B.Type == 8 && !Y.MonsterUnlock.Type.IsEmpty()) return TEXT("Finish or cancel the Locker unlock before upgrading it.");
    if (B.Type == 26 && AcademyBusy(Y, B.Id)) return TEXT("Finish or cancel this Academy's monster research before upgrading it.");
    if (B.HP < L->HP) return TEXT("Repair the building before upgrading it.");
    if (ConstructionBusy(Y)) return TEXT("All workers are busy. Wait for a task to finish or hire a worker in the General Store.");
    const FLevel& Next = D->Levels[B.Level];
    const FString Requirements = MissingRequirements(Y, Next);
    return Requirements.IsEmpty() ? MissingResources(Y, Next) : Requirements;
}

double FBYMSimulation::FImpl::WorkReadyAt(const FYard& Y, const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    if (!L || !HasWork(B) || !BYMBuildingLifecycle::CanWork(B.HP, L->HP, B.bRepairing)) return 0.;
    double Arrival = Now;
    if (NeedsWorker(B))
    {
        const FWorker* W = Y.Workers.FindByPredicate([&](const FWorker& Entry) { return Entry.Task == B.Id && Entry.Job == B.JobId; });
        if (!W) return 0.; Arrival = FMath::Max(Now, W->ArrivalAt);
    }
    return Arrival + B.WorkRemaining;
}

double FBYMSimulation::FImpl::RepairReadyAt(const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    return L && B.bRepairing ? Now + BYMEconomy::RepairDuration(RepairState(B, *L)) : 0.;
}

FString FBYMSimulation::FImpl::RepairReason(const FYard& Y, const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    if (!L) return TEXT("Building data is unavailable.");
    if (Busy(Y.Id)) return TEXT("Repair buildings after the current battle.");
    if (B.HP >= L->HP) return TEXT("This building is already fully repaired.");
    if (B.bRepairing) return TEXT("Repair is already in progress.");
    return FString();
}

FString FBYMSimulation::FImpl::CancelWorkReason(const FYard& Y, const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    if (!L || !HasWork(B)) return TEXT("This building has no construction or upgrade to cancel.");
    if (Busy(Y.Id)) return TEXT("Cancel building work after the current battle.");
    if (B.HP < L->HP || B.bRepairing) return TEXT("Fully repair this building before cancelling its work.");
    if (B.Level == 0 && B.Type == 14) return TEXT("The Town Hall cannot be removed.");
    // A migrated legacy queue can belong to unfinished construction. Preserve
    // its paid entries rather than orphaning them when removing the Hatchery.
    if (B.Level == 0 && Y.Queue.ContainsByPredicate([&B](const FTraining& Entry) { return Entry.Building == B.Id; }))
        return TEXT("Cancel this Hatchery's training queue before removing its construction.");
    return FString();
}

FString FBYMSimulation::FImpl::MoveReason(const FYard& Y, const FBuilding& B) const
{
    const FLevel* L = Stats(B);
    if (!L) return TEXT("Building data is unavailable.");
    if (Busy(Y.Id)) return TEXT("Move buildings after the current battle.");
    if (HasWork(B)) return TEXT("Finish construction or upgrading before moving this building.");
    if (B.HP < L->HP || B.bRepairing) return TEXT("Fully repair this building before moving it.");
    return FString();
}

void FBYMSimulation::FImpl::WorkRefund(const FYard& Y, const FBuilding& B, int64 (&Refund)[4]) const
{
    for (int32 Resource = 0; Resource < 4; ++Resource) Refund[Resource] = 0;
    const FBuildingDef* D = Buildings.Find(B.Type);
    if (!D || !HasWork(B) || !D->Levels.IsValidIndex(B.Level)) return;
    // BFOUNDATION.RecycleCost (level zero) / UpgradeCost: full source cost,
    // with BASE.Fund's storage cap. This is not completed-building recycling.
    const int64 Capacity = Storage(Y);
    for (int32 Resource = 0; Resource < 4; ++Resource)
        Refund[Resource] = FMath::Min<int64>(D->Levels[B.Level].Cost[Resource], FMath::Max<int64>(0, Capacity - Y.Resource[Resource]));
}

FObject FBYMSimulation::FImpl::BuildOptions(const FYard& Y) const
{
    FObject Options = Obj();
    for (const TPair<int32, FBuildingDef>& Entry : Buildings)
    {
        const FBuildingDef& D = Entry.Value;
        if (!D.bBuildable || !IsOperationalBuilding(D.Id)) continue;
        FObject Option = Obj();
        const int32 Owned = CountOf(Y, D.Id), Allowed = QuantityLimit(Y, D);
        const FString Reason = BuildReason(Y, D);
        Option->SetNumberField(TEXT("owned"), Owned); Option->SetNumberField(TEXT("limit"), Allowed);
        Option->SetNumberField(TEXT("available"), FMath::Max(0, Allowed - Owned));
        Option->SetBoolField(TEXT("canBuild"), Reason.IsEmpty()); Option->SetStringField(TEXT("reason"), Reason);
        Option->SetObjectField(TEXT("costs"), ResourceObject(D.Levels[0].Cost));
        Option->SetNumberField(TEXT("time"), FMath::Clamp(D.Levels[0].Seconds * ConstructionMultiplier(), 1.0, 31536000.0));
        Option->SetBoolField(TEXT("requirementsMet"), RequirementsMet(Y, D.Levels[0]));
        Option->SetBoolField(TEXT("affordable"), CanAfford(Y, D.Levels[0]));
        Option->SetNumberField(TEXT("nextTownHallLevel"), NextQuantityLevel(Y, D));
        Options->SetObjectField(FString::FromInt(D.Id), Option);
    }
    return Options;
}

FString FBYMSimulation::FImpl::UnlockReason(const FYard& Y, const FMonsterDef& D) const
{
    if (!IsOperationalMonster(D.Id) || D.bSourceBlocked || D.bSourceFake) return TEXT("This monster is not available from the original Locker.");
    if (MonsterLevel(Y, D.Id) > 0) return TEXT("This monster is already unlocked.");
    if (Busy(Y.Id)) return TEXT("Finish the current invasion or defense before unlocking monsters.");
    if (!Y.MonsterUnlock.Type.IsEmpty()) return TEXT("The Monster Locker is already unlocking a monster.");
    // CREATURELOCKER.Show/Start also have a registered front-page path. Their
    // completed-level and unlock gates do not require full health or idle work.
    if (AttainedLevel(Y, 8) < D.RequiredLevel) return FString::Printf(TEXT("Requires a completed Monster Locker at level %d."), D.RequiredLevel);
    if (Y.Resource[2] < D.UnlockPutty) return FString::Printf(TEXT("Need %lld more Putty."), static_cast<long long>(D.UnlockPutty - Y.Resource[2]));
    return FString();
}

FString FBYMSimulation::FImpl::ResearchReason(const FYard& Y, const FMonsterDef& D) const
{
    if (!IsOperationalMonster(D.Id) || D.bSourceBlocked || D.bSourceFake) return TEXT("This monster is not available from the original Academy.");
    const int32 Level = MonsterLevel(Y, D.Id);
    if (Level < 1) return TEXT("Unlock this monster in the Monster Locker first.");
    if (!D.Research.IsValidIndex(Level - 1)) return TEXT("This monster has reached its maximum research level.");
    if (Busy(Y.Id)) return TEXT("Finish the current invasion or defense before starting research.");
    if (Y.MonsterResearch.ContainsByPredicate([&D](const FMonsterJob& Job) { return Job.Type == D.Id; })) return TEXT("This monster is already being researched.");
    const FMonsterResearchDef& R = D.Research[Level - 1];
    if (!ResearchAcademy(Y, R.AcademyLevel)) return FString::Printf(TEXT("Requires a free, completed Monster Academy at level %d."), R.AcademyLevel);
    if (Y.Resource[2] < R.Putty) return FString::Printf(TEXT("Need %lld more Putty."), static_cast<long long>(R.Putty - Y.Resource[2]));
    return FString();
}

int32 FBYMSimulation::FImpl::MaxTrainCount(const FYard& Y, const FMonsterDef& D, const FString& Building) const
{
    if (!IsOperationalMonster(D.Id) || D.bSourceFake || MonsterLevel(Y, D.Id) < 1 || Busy(Y.Id)) return 0;
    const FBuilding* B = SelectHatchery(Y, D, Building); if (!B) return 0;
    const FMonsterStats& S = MonsterStats(Y, D);
    const int64 GooLimit = S.Goo > 0 ? Y.Resource[3] / (S.Goo * TrainingScale(Y)) : 100;
    return static_cast<int32>(FMath::Max<int64>(0, FMath::Min<int64>(100, FMath::Min<int64>(GooLimit, HatcherySpace(Y, *B, D.Id)))));
}

FString FBYMSimulation::FImpl::TrainReason(const FYard& Y, const FMonsterDef& D, const FString& Building) const
{
    if (!IsOperationalMonster(D.Id) || D.bSourceFake) return TEXT("This monster cannot be hatched here.");
    if (MonsterLevel(Y, D.Id) < 1) return TEXT("Unlock this monster in the Monster Locker first.");
    if (Busy(Y.Id)) return TEXT("Finish the current invasion or defense before editing Hatchery queues.");
    const FBuilding* B = SelectHatchery(Y, D, Building);
    if (!B) return Building.IsEmpty() ? TEXT("No completed, healthy Hatchery has queue space.") : TEXT("That Hatchery does not belong to your yard.");
    const FString Pause = HatcheryPauseReason(*B);
    if (Pause == TEXT("construction")) return TEXT("Complete this Hatchery first.");
    if (Pause == TEXT("upgrading")) return TEXT("Wait for this Hatchery upgrade to finish.");
    const FLevel* L = Stats(*B);
    if (!L || B->HP < L->HP || B->bRepairing) return TEXT("Fully repair this Hatchery before adding new monsters to its queue.");
    if (HatcherySpace(Y, *B, D.Id) < 1) return TEXT("This Hatchery queue is full.");
    if (Y.Resource[3] < MonsterStats(Y, D).Goo * TrainingScale(Y)) return TEXT("Not enough Goo.");
    return FString();
}

FObject FBYMSimulation::FImpl::MonsterStatsJSON(const FMonsterDef& D, int32 Level, int32 PriceScale) const
{
    const FMonsterStats& S = MonsterStats(D, Level); FObject O = Obj();
    O->SetNumberField(TEXT("level"), FMath::Clamp(Level, 1, D.Levels.Num()));
    O->SetNumberField(TEXT("hp"), S.HP); O->SetNumberField(TEXT("damage"), S.Damage);
    O->SetNumberField(TEXT("speed"), S.SourceSpeed); O->SetNumberField(TEXT("range"), S.SourceRange);
    O->SetNumberField(TEXT("housing"), S.Housing); O->SetNumberField(TEXT("goo"), static_cast<double>(S.Goo * PriceScale));
    O->SetNumberField(TEXT("time"), FMath::Clamp(S.Seconds * TrainingMultiplier() * PriceScale, 1.0, 86400.0));
    O->SetNumberField(TEXT("targetGroup"), S.TargetGroup); return O;
}

FObject FBYMSimulation::FImpl::MonsterOptions(const FYard& Y) const
{
    FObject Options = Obj();
    for (const TPair<FString, FMonsterDef>& Entry : Monsters)
    {
        const FMonsterDef& D = Entry.Value; if (!IsOperationalMonster(D.Id)) continue;
        FObject O = Obj(); const int32 Level = MonsterLevel(Y, D.Id);
        O->SetBoolField(TEXT("sourceBlocked"), D.bSourceBlocked); O->SetBoolField(TEXT("sourceFake"), D.bSourceFake);
        const FString UnlockError = UnlockReason(Y, D), ResearchError = ResearchReason(Y, D), TrainError = TrainReason(Y, D);
        O->SetBoolField(TEXT("unlocked"), Level > 0); O->SetNumberField(TEXT("level"), Level);
        O->SetNumberField(TEXT("maxLevel"), D.Levels.Num()); O->SetNumberField(TEXT("available"), Y.Army.FindRef(D.Id));
        O->SetBoolField(TEXT("canUnlock"), UnlockError.IsEmpty()); O->SetStringField(TEXT("unlockReason"), UnlockError);
        O->SetNumberField(TEXT("unlockCost"), static_cast<double>(D.UnlockPutty));
        O->SetNumberField(TEXT("unlockSeconds"), FMath::Clamp(D.UnlockSeconds * ProgressionMultiplier(), 1.0, 31536000.0));
        O->SetNumberField(TEXT("unlockReadyAt"), Y.MonsterUnlock.Type == D.Id ? Y.MonsterUnlock.ReadyAt : 0);
        O->SetNumberField(TEXT("requiredLockerLevel"), D.RequiredLevel);
        O->SetBoolField(TEXT("canResearch"), ResearchError.IsEmpty()); O->SetStringField(TEXT("researchReason"), ResearchError);
        const FMonsterResearchDef* R = D.Research.IsValidIndex(Level - 1) ? &D.Research[Level - 1] : nullptr;
        O->SetNumberField(TEXT("researchCost"), R ? static_cast<double>(R->Putty) : 0);
        O->SetNumberField(TEXT("researchSeconds"), R ? FMath::Clamp(R->Seconds * ProgressionMultiplier(), 1.0, 31536000.0) : 0);
        O->SetNumberField(TEXT("requiredAcademyLevel"), R ? R->AcademyLevel : 0);
        const FMonsterJob* Job = Y.MonsterResearch.FindByPredicate([&D](const FMonsterJob& J) { return J.Type == D.Id; });
        O->SetNumberField(TEXT("researchReadyAt"), Job ? Job->ReadyAt : 0);
        const FBuilding* Academy = R ? ResearchAcademy(Y, R->AcademyLevel) : nullptr;
        O->SetStringField(TEXT("researchBuilding"), Job ? Job->Building : (Academy ? Academy->Id : FString()));
        O->SetBoolField(TEXT("canTrain"), TrainError.IsEmpty()); O->SetStringField(TEXT("trainReason"), TrainError);
        O->SetNumberField(TEXT("maxTrainCount"), MaxTrainCount(Y, D));
        FObject CurrentStats = MonsterStatsJSON(D, FMath::Max(1, Level));
        CurrentStats->SetNumberField(TEXT("goo"), static_cast<double>(MonsterStats(Y, D).Goo * TrainingScale(Y)));
        CurrentStats->SetNumberField(TEXT("time"), FMath::Clamp(MonsterStats(Y, D).Seconds * TrainingMultiplier() * TrainingScale(Y), 1., 86400.));
        O->SetObjectField(TEXT("currentStats"), CurrentStats);
        O->SetObjectField(TEXT("nextStats"), R ? MonsterStatsJSON(D, R->ToLevel, TrainingScale(Y)) : Obj());
        Options->SetObjectField(D.Id, O);
    }
    return Options;
}

int32 FBYMSimulation::FImpl::BunkerUsed(const FYard& Y, const FBuilding& Building) const
{
    int64 Used = 0;
    for (const TPair<FString, int32>& Entry : Building.Garrison)
    {
        const FMonsterDef* Monster = Monsters.Find(Entry.Key);
        if (Monster) Used += static_cast<int64>(FMath::Max(0, Entry.Value)) * MonsterStats(Y, *Monster).Housing;
    }
    return static_cast<int32>(FMath::Min<int64>(Used, MAX_int32));
}

FString FBYMSimulation::FImpl::BunkerManageReason(const FYard& Y, const FBuilding& Building) const
{
    if (Building.Type != 22) return TEXT("Choose a Monster Bunker in your own yard.");
    if (Busy(Y.Id)) return TEXT("Manage your Bunker after the battle finishes.");
    if (Building.Level <= 0) return TEXT("Finish constructing this Monster Bunker first.");
    if (HasWork(Building)) return TEXT("Finish upgrading this Monster Bunker first.");
    const FLevel* Level = Stats(Building);
    if (!Level || Building.bRepairing || Building.HP < Level->HP) return TEXT("Fully repair this Monster Bunker first.");
    return FString();
}

FArray FBYMSimulation::FImpl::BunkersJSON(const FYard& Y) const
{
    FArray Result;
    TArray<FString> Types; Monsters.GetKeys(Types); Types.Sort();
    for (const FBuilding& Building : Y.Buildings) if (Building.Type == 22)
    {
        FObject Item = Obj(), Garrison = Obj();
        const int32 Capacity = BYMBunkerRules::Capacity(Building.Level), Used = BunkerUsed(Y, Building);
        const FString Reason = BunkerManageReason(Y, Building);
        Item->SetStringField(TEXT("building"), Building.Id); Item->SetNumberField(TEXT("capacity"), Capacity);
        Item->SetNumberField(TEXT("used"), Used); Item->SetNumberField(TEXT("range"), BYMBunkerRules::TriggerRangePixels(Building.Level));
        Item->SetBoolField(TEXT("canManage"), Reason.IsEmpty()); Item->SetStringField(TEXT("reason"), Reason);
        for (const TPair<FString, int32>& Entry : Building.Garrison) if (Entry.Value > 0) Garrison->SetNumberField(Entry.Key, Entry.Value);
        Item->SetObjectField(TEXT("monsters"), Garrison);
        FArray Options;
        for (const FString& Type : Types) if (BYMBunkerRules::IsAllowedSpecies(Type))
        {
            const FMonsterDef& Definition = Monsters.FindChecked(Type); const FMonsterStats& StatsForType = MonsterStats(Y, Definition);
            const int32 Ready = Y.Army.FindRef(Type), Stored = Building.Garrison.FindRef(Type);
            int32 Maximum = FMath::Min(Ready, FMath::Max(0, Capacity - Used) / StatsForType.Housing);
            while (Maximum > 0 && BYMBunkerRules::TransferCost(StatsForType.Goo, Maximum) > Y.Resource[2]) --Maximum;
            FString TransferReason = Reason;
            if (TransferReason.IsEmpty() && MonsterLevel(Y, Type) <= 0) TransferReason = TEXT("Unlock this monster first.");
            if (TransferReason.IsEmpty() && Ready <= 0) TransferReason = TEXT("Train this monster in your Hatchery first.");
            if (TransferReason.IsEmpty() && Capacity - Used < StatsForType.Housing) TransferReason = TEXT("This Bunker does not have enough space.");
            if (TransferReason.IsEmpty() && Maximum <= 0) TransferReason = TEXT("Not enough Putty for this transfer.");
            FString DismissReason = Reason;
            if (DismissReason.IsEmpty() && Stored <= 0) DismissReason = TEXT("This monster is not in this Bunker.");
            FObject Option = Obj(); Option->SetStringField(TEXT("type"), Type);
            Option->SetNumberField(TEXT("ready"), Ready); Option->SetNumberField(TEXT("stored"), Stored);
            Option->SetNumberField(TEXT("housing"), StatsForType.Housing); Option->SetNumberField(TEXT("sourceGoo"), static_cast<double>(StatsForType.Goo));
            Option->SetNumberField(TEXT("maxTransfer"), TransferReason.IsEmpty() ? Maximum : 0);
            Option->SetBoolField(TEXT("canTransfer"), TransferReason.IsEmpty()); Option->SetStringField(TEXT("transferReason"), TransferReason);
            Option->SetBoolField(TEXT("canDismiss"), DismissReason.IsEmpty()); Option->SetStringField(TEXT("dismissReason"), DismissReason);
            Options.Add(MakeShared<FJsonValueObject>(Option));
        }
        Item->SetArrayField(TEXT("options"), Options); Result.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Result;
}

FString FBYMSimulation::FImpl::Execute(const FString& Id, const FString& Op, const FObject& P)
{
    FYard* Y = Yards.Find(Id); if (!Y) return Reply(false, TEXT("This session has no server-owned yard."));
    if (IsMapSite(Id)) return Reply(false, TEXT("Use an authenticated main-yard account."));
    Advance(*Y);
    if (Op.StartsWith(TEXT("map"))) return ExecuteMap(*Y, Op, P);
    if (Op == TEXT("inspect") && IsMapSite(Str(P, TEXT("target")))) return Reply(false, TEXT("Select territory through the World Map."));
    if (Op == TEXT("inspect")) return Yards.Contains(Str(P, TEXT("target"))) ? Reply(true, TEXT("Yard found.")) : Reply(false, TEXT("That yard does not exist."));
    if (Op == TEXT("retreat"))
    {
        if (!Battles.Contains(Id)) return Reply(false, TEXT("You are not invading a yard."));
        const bool bContested = Battles.FindChecked(Id).Deployed > 0;
        EndBattle(Id, false, bContested ? TEXT("Attacker retreated") : TEXT("Scouting cancelled"), bContested);
        return Reply(true, TEXT("Returned home. Deployed monsters are consumed by the battle."));
    }
    if (Op == TEXT("invade"))
    {
        const FString Target = Str(P, TEXT("target")); FYard* D = Yards.Find(Target);
        if (!D || Target == Id) return Reply(false, TEXT("Choose another player or rival yard."));
        if (IsMapSite(Target) || MapParticipant(Y->Id) || MapParticipant(D->Id) || MapGate(*Y).IsEmpty() || MapGate(*D).IsEmpty()) return Reply(false, TEXT("Use the World Map attack confirmation for this territory and its distance cost."));
        if (Busy(Id) || Busy(Target)) return Reply(false, TEXT("One of these yards is already in a battle."));
        if (D->ShieldUntil > Now) return Reply(false, TEXT("This yard is protected by a defense shield."));
        if (Battles.Num() >= BattleLimit()) return Reply(false, TEXT("All battle slots are busy. Try again shortly."));
        if (ArmyCount(*Y) < 1 || LevelOf(*Y, 5) < 1) return Reply(false, TEXT("Train an army and repair or complete your Flinger before invading."));
        Advance(*D);
        const FBuilding* Town = D->Buildings.FindByPredicate([](const FBuilding& B) { return B.Type == 14; });
        if (!Town || Town->HP <= 0) return Reply(false, TEXT("This yard is rebuilding its destroyed Town Hall."));
        FBattle B; B.Id = NewId(); B.Attacker = Id; B.Defender = Target; B.StartedAt = Now; B.SimulatedAt = Now; B.EndsAt = Now + BattleSeconds(); B.InitialBuildingHP = 0;
        for (const FBuilding& Building : D->Buildings) if (Building.HP > 0 && IsCombatObjective(Building)) { B.InitialBuildingHP += Building.HP; ++B.InitialBuildings; }
        Battles.Add(Id, MoveTemp(B));
        return Reply(true, TEXT("Invasion ready. Select monsters and deploy on the marked outer edge before the battle timer ends."));
    }
    if (Op == TEXT("deploy"))
    {
        FBattle* B = Battles.Find(Id); if (!B) return Reply(false, TEXT("Start an invasion first."));
        FYard* D = Yards.Find(B->Defender); if (!D) return Reply(false, TEXT("Defending yard is unavailable."));
        const FString Type = Str(P, TEXT("type")); const FMonsterDef* M = Monsters.Find(Type); int32 Count = 0, X = 0, PosY = 0;
        if (!M || !IsOperationalMonster(Type) || !Integer(P, TEXT("count"), 1, 100, Count) || !Integer(P, TEXT("x"), YardMinX(*D), YardMinX(*D) + YardWidth(*D) - 1, X) || !Integer(P, TEXT("y"), YardMinY(*D), YardMinY(*D) + YardHeight(*D) - 1, PosY)) return Reply(false, TEXT("Invalid deployment type, count, or coordinates."));
        const FMonsterStats& S = MonsterStats(*Y, *M);
        if (B->EndsAt <= Now) return Reply(false, TEXT("The invasion timer has ended. Return home or choose another rival."));
        if (X >= YardMinX(*D) + 3 && PosY >= YardMinY(*D) + 3 && X < YardMinX(*D) + YardWidth(*D) - 3 && PosY < YardMinY(*D) + YardHeight(*D) - 3) return Reply(false, TEXT("Deploy within the outer three cells of the yard."));
        if (Y->Army.FindRef(Type) < Count || B->Deployed + Count > TroopLimit()) return Reply(false, TEXT("Not enough monsters, or this battle has reached its troop limit."));
        const int64 Capacity = FlingerCapacity(*Y), Used = FlingerUsed(*B);
        if (Used + static_cast<int64>(Count) * S.Housing > Capacity) return Reply(false, TEXT("Upgrade your Flinger for a larger invasion force."));
        for (const FBuilding& F : D->Buildings) if (F.HP > 0)
            for (int32 I = 0; I < Count; ++I)
                if (InsideCombat(F, FVector2D(X + 0.25 + (I % 3) * 0.2, PosY + 0.25 + ((I / 3) % 3) * 0.2))) return Reply(false, TEXT("The deployment cell is blocked by a building."));
        Y->Army.FindChecked(Type) -= Count; Y->ShieldUntil = 0;
        for (int32 I = 0; I < Count; ++I)
        {
            FTroop T; T.Id = NewId(); T.Type = Type; T.Level = FMath::Max(1, MonsterLevel(*Y, Type)); T.HP = S.HP;
            T.Position = FVector2D(X + 0.25 + (I % 3) * 0.2, PosY + 0.25 + ((I / 3) % 3) * 0.2);
            T.TargetPosition = T.Position; T.DeployedAt = B->SimulatedAt; B->Troops.Add(MoveTemp(T));
        }
        B->Deployed += Count;
        return Reply(true, TEXT("Monsters deployed. The host is resolving movement, defenses, and damage."));
    }
    if (HandlesEconomyCommand(Op)) return ExecuteEconomy(*Y, Op, P);
    if (Busy(Id)) return Reply(false, TEXT("Your yard is in a battle. Building, training, collecting, and repairs resume when it finishes."));
    if (Op == TEXT("bunkerTransfer") || Op == TEXT("bunkerDismiss"))
    {
        FBuilding* Bunker = FindBuilding(*Y, Str(P, TEXT("building")));
        if (!Bunker || Bunker->Type != 22) return Reply(false, TEXT("Choose a Monster Bunker in your own yard."));
        const FString Reason = BunkerManageReason(*Y, *Bunker); if (!Reason.IsEmpty()) return Reply(false, Reason);
        if (Op == TEXT("bunkerDismiss"))
        {
            const FString Type = Str(P, TEXT("type")); int32 Count = 0, Expected = 0;
            if (!BYMBunkerRules::IsAllowedSpecies(Type) || !Integer(P, TEXT("count"), 1, 100000, Count)
                || !Integer(P, TEXT("expectedStored"), 1, 100000, Expected) || Bunker->Garrison.FindRef(Type) != Expected || Count > Expected)
                return Reply(false, TEXT("That Bunker roster changed. Refresh it before removing monsters."));
            Bunker->Garrison.FindChecked(Type) -= Count;
            if (Bunker->Garrison.FindRef(Type) == 0) Bunker->Garrison.Remove(Type);
            return Reply(true, TEXT("Monsters removed from the Bunker. No resources or army units are returned."));
        }
        TMap<FString, int32> Selection;
        if (P->HasField(TEXT("selection")))
        {
            const FObject Requested = Child(P, TEXT("selection"));
            if (!Requested || Requested->Values.Num() < 1 || Requested->Values.Num() > 14 || P->HasField(TEXT("type")) || P->HasField(TEXT("count")))
                return Reply(false, TEXT("Choose a valid group of monsters to transfer."));
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Requested->Values)
            {
                int32 Count = 0;
                if (!BYMBunkerRules::IsAllowedSpecies(Entry.Key) || !Integer(Requested, *Entry.Key, 1, 100000, Count))
                    return Reply(false, TEXT("This monster or transfer quantity is not allowed in an ordinary Bunker."));
                Selection.Add(Entry.Key, Count);
            }
        }
        else
        {
            const FString Type = Str(P, TEXT("type")); int32 Count = 0;
            if (!BYMBunkerRules::IsAllowedSpecies(Type) || !Integer(P, TEXT("count"), 1, 100000, Count))
                return Reply(false, TEXT("Choose a supported monster and a valid transfer quantity."));
            Selection.Add(Type, Count);
        }
        int64 Space = 0, Putty = 0;
        for (const TPair<FString, int32>& Entry : Selection)
        {
            const FMonsterDef* Definition = Monsters.Find(Entry.Key);
            if (!Definition || MonsterLevel(*Y, Entry.Key) <= 0 || Y->Army.FindRef(Entry.Key) < Entry.Value)
                return Reply(false, TEXT("The selected ready monsters are no longer available."));
            const FMonsterStats& S = MonsterStats(*Y, *Definition);
            Space += static_cast<int64>(S.Housing) * Entry.Value;
            Putty += BYMBunkerRules::TransferCost(S.Goo, Entry.Value);
        }
        if (Space + BunkerUsed(*Y, *Bunker) > BYMBunkerRules::Capacity(Bunker->Level)) return Reply(false, TEXT("The selected monsters exceed this Bunker's remaining space."));
        if (Putty > Y->Resource[2]) return Reply(false, TEXT("Not enough Putty for the selected transfer."));
        // Validate the complete mixed selection before changing any wallet or
        // roster. Each species rounds its original transfer fee once.
        Y->Resource[2] -= Putty;
        for (const TPair<FString, int32>& Entry : Selection)
        { Y->Army.FindChecked(Entry.Key) -= Entry.Value; Bunker->Garrison.FindOrAdd(Entry.Key) += Entry.Value; }
        return Reply(true, TEXT("Monsters transferred from Housing into the Bunker."));
    }
    if (Op == TEXT("unlock") || Op == TEXT("research"))
    {
        const FString Type = Str(P, TEXT("type")); const FMonsterDef* D = Monsters.Find(Type);
        if (!D) return Reply(false, TEXT("Choose a valid monster."));
        const bool bResearch = Op == TEXT("research");
        const FString Reason = bResearch ? ResearchReason(*Y, *D) : UnlockReason(*Y, *D);
        if (!Reason.IsEmpty()) return Reply(false, Reason);
        FMonsterJob Job; Job.Type = Type; Job.StartedAt = Now;
        if (bResearch)
        {
            const FMonsterResearchDef& R = D->Research[MonsterLevel(*Y, Type) - 1];
            const FBuilding* Academy = ResearchAcademy(*Y, R.AcademyLevel);
            if (!Academy) return Reply(false, TEXT("The selected Monster Academy is no longer available."));
            Job.Building = Academy->Id; Job.FromLevel = R.FromLevel; Job.ToLevel = R.ToLevel;
            Job.PuttyPaid = R.Putty; Job.ReadyAt = Now + FMath::Clamp(R.Seconds * ProgressionMultiplier(), 1.0, 31536000.0);
            Y->Resource[2] -= Job.PuttyPaid; Y->MonsterResearch.Add(MoveTemp(Job));
            return Reply(true, TEXT("Monster research started in the Academy."));
        }
        Job.PuttyPaid = D->UnlockPutty; Job.ReadyAt = Now + FMath::Clamp(D->UnlockSeconds * ProgressionMultiplier(), 1.0, 31536000.0);
        Y->Resource[2] -= Job.PuttyPaid; Y->MonsterUnlock = MoveTemp(Job);
        return Reply(true, TEXT("Monster unlocking started in the Locker."));
    }
    if (Op == TEXT("cancelUnlock") || Op == TEXT("cancelResearch"))
    {
        const FString Type = Str(P, TEXT("type")); int64 Paid = 0;
        if (Op == TEXT("cancelUnlock"))
        {
            if (Type.IsEmpty() || Y->MonsterUnlock.Type != Type) return Reply(false, TEXT("That monster is not being unlocked."));
            Paid = Y->MonsterUnlock.PuttyPaid; Y->MonsterUnlock = FMonsterJob();
        }
        else
        {
            const int32 Index = Y->MonsterResearch.IndexOfByPredicate([&Type](const FMonsterJob& Job) { return Job.Type == Type; });
            if (Index == INDEX_NONE) return Reply(false, TEXT("That monster is not being researched."));
            Paid = Y->MonsterResearch[Index].PuttyPaid; Y->MonsterResearch.RemoveAt(Index);
        }
        // CREATURELOCKER.Cancel / ACADEMY.CancelMonsterUpgrade refund Putty
        // through BASE.Fund, which only fills available storage and never lowers
        // a wallet already above capacity (for example after losing a silo).
        const int64 Refund = FMath::Min<int64>(Paid, FMath::Max<int64>(0, Storage(*Y) - Y->Resource[2]));
        Y->Resource[2] += Refund;
        return Reply(true, FString::Printf(TEXT("Cancelled. Returned %lld Putty within available storage."), static_cast<long long>(Refund)));
    }
    if (Op == TEXT("collect"))
    {
        const FString Requested = Str(P, TEXT("building"));
        if (P->HasField(TEXT("building")))
        {
            const FBuilding* Selected = FindBuilding(*Y, Requested);
            if (!Selected || Selected->Type < 1 || Selected->Type > 4 || Selected->Level == 0)
                return Reply(false, TEXT("Choose a completed resource collector in your own yard."));
            const FLevel* SelectedStats = Stats(*Selected);
            if (!SelectedStats || Selected->HP < SelectedStats->HP)
                return Reply(false, TEXT("Fully repair this collector before collecting."));
            if (HasWork(*Selected) || Selected->bRepairing) return Reply(false, TEXT("Wait for this collector's upgrade to finish."));
        }
        const int64 Capacity = Storage(*Y); int64 Total = 0;
        for (FBuilding& B : Y->Buildings) if (B.Type >= 1 && B.Type <= 4 && B.Level > 0 && (Requested.IsEmpty() || Requested == B.Id))
        {
            const FLevel* L = Stats(B); if (!L) continue;
            // BUILDINGINFO and the original Bank All path require full health
            // and no construction/upgrade. Bank itself only does the transfer.
            if (B.HP < L->HP || HasWork(B) || B.bRepairing) continue;
            BYMEconomy::FCollectorState Collector = CollectorState(B);
            const int64 Accepted = BYMEconomy::Bank(Collector, CollectorRules(*L), RepairState(B, *L), Y->Resource[B.Type - 1], Capacity);
            Total += Accepted;
            Y->EarnedExperience = BYMExperience::Add(Y->EarnedExperience, BYMExperience::CollectorReward(Accepted));
            B.Stored = Collector.Stored; B.ProductionRemaining = Collector.Remaining;
        }
        return Reply(true, FString::Printf(TEXT("Collected %lld resources."), static_cast<long long>(Total)));
    }
    if (Op == TEXT("train"))
    {
        const FString Type = Str(P, TEXT("type")), Requested = Str(P, TEXT("building"));
        const FMonsterDef* M = Monsters.Find(Type); int32 Count = 0;
        if (!M || !IsOperationalMonster(Type) || !Integer(P, TEXT("count"), 1, 100, Count)) return Reply(false, TEXT("Choose a supported monster and a count from 1 to 100."));
        const FString Reason = TrainReason(*Y, *M, Requested); if (!Reason.IsEmpty()) return Reply(false, Reason);
        const FBuilding* Hatchery = SelectHatchery(*Y, *M, Requested);
        if (!Hatchery || Count > MaxTrainCount(*Y, *M, Hatchery->Id)) return Reply(false, TEXT("This quantity exceeds available Goo or the selected Hatchery's queue space."));
        const FString BuildingId = Hatchery->Id;
        Y->Resource[3] -= MonsterStats(*Y, *M).Goo * TrainingScale(*Y) * Count;
        for (int32 Unit = 0; Unit < Count; ++Unit)
        {
            FTraining* Group = Y->Queue.FindByPredicate([&](const FTraining& T) { return T.Building == BuildingId && !T.bActive && !T.bLegacy && T.Type == Type && T.Count < 20 && T.PriceScale == TrainingScale(*Y); });
            if (Group) ++Group->Count;
            else { FTraining Added; Added.Id = NewId(); Added.Building = BuildingId; Added.Type = Type; Added.Count = 1; Added.PriceScale = TrainingScale(*Y); Y->Queue.Add(MoveTemp(Added)); }
            AdvanceHatcheries(*Y, 0);
        }
        return Reply(true, TEXT("Monsters queued in the selected Hatchery. Finished monsters wait there if housing is full."));
    }
    if (Op == TEXT("cancelTraining"))
    {
        const FString BuildingId = Str(P, TEXT("building")), EntryId = Str(P, TEXT("entry")); int32 Expected = 0;
        const FBuilding* Hatchery = FindBuilding(*Y, BuildingId);
        if (!Hatchery || Hatchery->Type != 13 || !Integer(P, TEXT("expectedCount"), 1, 20, Expected)) return Reply(false, TEXT("Choose a current Hatchery queue entry."));
        const int32 Index = Y->Queue.IndexOfByPredicate([&](const FTraining& T) { return T.Building == BuildingId && T.Id == EntryId && T.Count == Expected; });
        if (Index == INDEX_NONE) return Reply(false, TEXT("That queue entry has changed. Refresh the Hatchery and try again."));
        const FMonsterDef* M = Monsters.Find(Y->Queue[Index].Type); if (!M) return Reply(false, TEXT("This monster definition is unavailable."));
        const int64 Refund = FMath::Min<int64>(MonsterStats(*Y, *M).Goo * Y->Queue[Index].PriceScale, FMath::Max<int64>(0, Storage(*Y) - Y->Resource[3]));
        if (--Y->Queue[Index].Count <= 0) Y->Queue.RemoveAt(Index);
        Y->Resource[3] += Refund;
        AdvanceHatcheries(*Y, 0);
        return Reply(true, FString::Printf(TEXT("Cancelled one monster. Returned %lld Goo within available storage."), static_cast<long long>(Refund)));
    }
    if (Op == TEXT("cancelConstruction") || Op == TEXT("cancelUpgrade"))
    {
        FBuilding* B = FindBuilding(*Y, Str(P, TEXT("building")));
        if (!B) return Reply(false, TEXT("Building does not belong to your yard."));
        const FString Expected = Str(P, TEXT("job"));
        if (!HasWork(*B) || Expected.IsEmpty() || Expected != B->JobId
            || (Op == TEXT("cancelConstruction")) != (B->Level == 0))
            return Reply(false, TEXT("That building job has changed. Refresh the building and try again."));
        const FString Reason = CancelWorkReason(*Y, *B); if (!Reason.IsEmpty()) return Reply(false, Reason);
        int64 Refund[4]; WorkRefund(*Y, *B, Refund);
        ReleaseWorker(*Y, B->Level == 0 ? TEXT("construction") : TEXT("upgrade"), B->Id, Now);
        for (int32 Resource = 0; Resource < 4; ++Resource) Y->Resource[Resource] += Refund[Resource];
        if (B->Level == 0)
        {
            const FString BuildingId = B->Id;
            Y->Buildings.RemoveAll([&BuildingId](const FBuilding& Entry) { return Entry.Id == BuildingId; });
        }
        else { B->TargetLevel = B->Level; B->WorkRemaining = 0.; B->JobId.Reset(); B->SpeedupRevision = 0; }
        return Reply(true, TEXT("Building work cancelled. Resources returned within available storage."));
    }
    if (Op == TEXT("build"))
    {
        int32 Type = 0, X = 0, PosY = 0;
        if (!Integer(P, TEXT("type"), 1, 100000, Type) || !Integer(P, TEXT("x"), YardMinX(*Y), YardMinX(*Y) + YardWidth(*Y), X) || !Integer(P, TEXT("y"), YardMinY(*Y), YardMinY(*Y) + YardHeight(*Y), PosY)) return Reply(false, TEXT("Invalid building type or grid coordinates."));
        const FBuildingDef* D = Buildings.Find(Type);
        if (!D || !D->bBuildable || !IsOperationalBuilding(Type)) return Reply(false, TEXT("This original building system has not yet been ported and is unavailable."));
        const FString Reason = BuildReason(*Y, *D);
        if (!Reason.IsEmpty()) return Reply(false, Reason);
        const FLevel& L = D->Levels[0];
        if (!Fits(*Y, X, PosY, D->Cells)) return Reply(false, TEXT("The footprint overlaps another building or the yard boundary."));
        FBuilding B; B.Id = NewId(); B.Type = Type; B.Level = 0; B.TargetLevel = 1; B.X = X; B.Y = PosY; B.HP = L.HP; B.JobId = NewId();
        B.WorkRemaining = L.SourceSeconds <= 0 ? 0 : FMath::Clamp(L.Seconds * ConstructionMultiplier(), 1.0, 31536000.0);
        const FString AddedId = B.Id; Y->Buildings.Add(MoveTemp(B)); FBuilding& Added = Y->Buildings.Last(); double Arrival = Now;
        if (NeedsWorker(Added) && !AssignWorker(*Y, TEXT("construction"), Added.Id, Added.JobId, BuildingWorkerPoint(Added), Arrival))
        { Y->Buildings.RemoveAll([&](const FBuilding& Entry) { return Entry.Id == AddedId; }); return Reply(false, TEXT("No worker is available for this construction.")); }
        Pay(*Y, L); if (!NeedsWorker(Added)) CompleteBuildingWork(*Y, Added, Now);
        return Reply(true, TEXT("Construction started."));
    }
    if (Op == TEXT("upgrade") || Op == TEXT("move") || Op == TEXT("repair"))
    {
        FBuilding* B = FindBuilding(*Y, Str(P, TEXT("building"))); if (!B) return Reply(false, TEXT("Building does not belong to your yard."));
        const FBuildingDef* D = Buildings.Find(B->Type); const FLevel* L = Stats(*B);
        if (!D || !L) return Reply(false, TEXT("Building data is unavailable."));
        if (Op == TEXT("move"))
        {
            const FString Reason = MoveReason(*Y, *B); if (!Reason.IsEmpty()) return Reply(false, Reason);
            int32 X = 0, PosY = 0;
            if (!Integer(P, TEXT("x"), YardMinX(*Y), YardMinX(*Y) + YardWidth(*Y), X) || !Integer(P, TEXT("y"), YardMinY(*Y), YardMinY(*Y) + YardHeight(*Y), PosY) || !Fits(*Y, X, PosY, D->Cells, B->Id)) return Reply(false, TEXT("The new footprint overlaps another building or the yard boundary."));
            B->X = X; B->Y = PosY; return Reply(true, TEXT("Building moved."));
        }
        if (Op == TEXT("repair"))
        {
            const FString Reason = RepairReason(*Y, *B); if (!Reason.IsEmpty()) return Reply(false, Reason);
            B->bRepairing = true;
            return Reply(true, TEXT("Repair started."));
        }
        const FString Reason = UpgradeReason(*Y, *B);
        if (!Reason.IsEmpty()) return Reply(false, Reason);
        const FLevel& Next = D->Levels[B->Level];
        B->TargetLevel = B->Level + 1; B->JobId = NewId(); B->SpeedupRevision = 0; B->WorkRemaining = FMath::Clamp(Next.Seconds * ConstructionMultiplier(), 1.0, 31536000.0);
        double Arrival = Now;
        if (!AssignWorker(*Y, TEXT("upgrade"), B->Id, B->JobId, BuildingWorkerPoint(*B), Arrival))
        { B->TargetLevel = B->Level; B->JobId.Reset(); B->WorkRemaining = 0; return Reply(false, TEXT("No worker is available for this upgrade.")); }
        Pay(*Y, Next); return Reply(true, TEXT("Upgrade started."));
    }
    return Reply(false, TEXT("Unknown game command."));
}

void FBYMSimulation::FImpl::BuildPath(FTroop& T, const FYard& Defender, const FBuilding& Target, double Range)
{
    T.Path.Reset(); T.PathIndex = 0; T.RepathAt = Now + 1.0;
    const int32 Width = YardWidth(Defender), Height = YardHeight(Defender), MinX = YardMinX(Defender), MinY = YardMinY(Defender), Count = Width * Height;
    TArray<int32> BlockedBy; BlockedBy.Init(-1, Count);
    TArray<uint8> BlockedEdges; BlockedEdges.Init(0, Count);
    static const int32 DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    // Occupancy, attack contact and line-of-sight share the original collision
    // rectangles. The larger construction/UI size must never stop attackers.
    for (int32 I = 0; I < Defender.Buildings.Num(); ++I)
    {
        const FBuilding& B = Defender.Buildings[I]; if (B.HP <= 0) continue;
        const FBuildingDef* D = Buildings.Find(B.Type); if (!D || D->Type == TEXT("decoration")) continue;
        for (const FCombatRect& R : D->CombatRects)
            for (int32 Y = FMath::Max(MinY, FMath::FloorToInt(B.Y + R.Min.Y) - 1); Y < FMath::Min(MinY + Height, FMath::CeilToInt(B.Y + R.Max.Y) + 1); ++Y)
                for (int32 X = FMath::Max(MinX, FMath::FloorToInt(B.X + R.Min.X) - 1); X < FMath::Min(MinX + Width, FMath::CeilToInt(B.X + R.Max.X) + 1); ++X)
                {
                    const FVector2D Point(X + 0.5, Y + 0.5);
                    if (InsideCombat(B, Point)) BlockedBy[(Y - MinY) * Width + X - MinX] = I;
                    // Cache local edge obstructions once. A BFS expansion must
                    // not rescan every yard building for every neighbor.
                    for (int32 Direction = 0; Direction < 4; ++Direction)
                        if (SegmentHitsRect(Point, Point + FVector2D(DX[Direction], DY[Direction]), FVector2D(B.X, B.Y) + R.Min, FVector2D(B.X, B.Y) + R.Max))
                            BlockedEdges[(Y - MinY) * Width + X - MinX] |= static_cast<uint8>(1 << Direction);
                }
    }
    const int32 StartX = FMath::Clamp(FMath::FloorToInt(T.Position.X), MinX, MinX + Width - 1);
    const int32 StartY = FMath::Clamp(FMath::FloorToInt(T.Position.Y), MinY, MinY + Height - 1);
    const int32 Start = (StartY - MinY) * Width + StartX - MinX;
    TArray<int32> Parents; Parents.Init(-1, Count); Parents[Start] = Start;
    TArray<int32> Queue; Queue.Reserve(Count); Queue.Add(Start);
    int32 Found = -1, OuterCell = -1, OuterBuilding = -1;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const int32 Current = Queue[Index], X = Current % Width + MinX, Y = Current / Width + MinY;
        const FVector2D Point(X + 0.5, Y + 0.5);
        if (BlockedBy[Current] == -1 && CanAttack(Defender, Target, Point, Range)) { Found = Current; break; }
        for (int32 Direction = 0; Direction < 4; ++Direction)
        {
            const int32 NX = X + DX[Direction], NY = Y + DY[Direction]; if (NX < MinX || NY < MinY || NX >= MinX + Width || NY >= MinY + Height) continue;
            const int32 Next = (NY - MinY) * Width + NX - MinX;
            if (BlockedBy[Next] != -1)
            {
                // Record a reachable wall/outer building while searching. Only
                // use it if the preferred objective is actually unreachable.
                const FBuilding& Obstacle = Defender.Buildings[BlockedBy[Next]];
                if (OuterCell == -1 && Obstacle.Id != Target.Id && CanAttack(Defender, Obstacle, Point, Range))
                { OuterCell = Current; OuterBuilding = BlockedBy[Next]; }
                continue;
            }
            if (Parents[Next] != -1 || (BlockedEdges[Current] & (1 << Direction)) != 0) continue;
            Parents[Next] = Current; Queue.Add(Next);
        }
    }
    if (Found == -1 && OuterCell != -1) { Found = OuterCell; T.Target = Defender.Buildings[OuterBuilding].Id; }
    if (Found == -1) return;
    TArray<FIntPoint> Reverse;
    for (int32 P = Found; P != Start; P = Parents[P]) Reverse.Add(FIntPoint(P % Width + MinX, P / Width + MinY));
    // Center the initial deployment cell before following orthogonal steps;
    // diagonal corner cutting would otherwise pass through narrow obstacles.
    const FVector2D Center(StartX + 0.5, StartY + 0.5);
    if ((Center - T.Position).Size() > 0.0001 && ClearSegment(Defender, T.Position, Center)) T.Path.Add(FIntPoint(StartX, StartY));
    for (int32 I = Reverse.Num() - 1; I >= 0; --I) T.Path.Add(Reverse[I]);
}

void FBYMSimulation::FImpl::BuildUnitPath(FTroop& Unit, const FYard& Yard, const FVector2D& Destination, double Range, const FString& Ignore)
{
    Unit.Path.Reset(); Unit.PathIndex = 0; Unit.RepathAt = Now + .5;
    const int32 Width = YardWidth(Yard), Height = YardHeight(Yard), MinX = YardMinX(Yard), MinY = YardMinY(Yard), Count = Width * Height;
    TArray<uint8> Blocked; Blocked.Init(0, Count); TArray<uint8> Edges; Edges.Init(0, Count);
    static const int32 DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (const FBuilding& Building : Yard.Buildings) if (Building.HP > 0 && Building.Id != Ignore)
    {
        const FBuildingDef* Definition = Buildings.Find(Building.Type); if (!Definition || Definition->Type == TEXT("decoration")) continue;
        for (const FCombatRect& Rect : Definition->CombatRects)
            for (int32 CellY = FMath::Max(MinY, FMath::FloorToInt(Building.Y + Rect.Min.Y) - 1); CellY < FMath::Min(MinY + Height, FMath::CeilToInt(Building.Y + Rect.Max.Y) + 1); ++CellY)
                for (int32 CellX = FMath::Max(MinX, FMath::FloorToInt(Building.X + Rect.Min.X) - 1); CellX < FMath::Min(MinX + Width, FMath::CeilToInt(Building.X + Rect.Max.X) + 1); ++CellX)
                {
                    const FVector2D Point(CellX + .5, CellY + .5);
                    if (InsideCombat(Building, Point)) Blocked[(CellY - MinY) * Width + CellX - MinX] = 1;
                    for (int32 Direction = 0; Direction < 4; ++Direction)
                        if (SegmentHitsRect(Point, Point + FVector2D(DX[Direction], DY[Direction]), FVector2D(Building.X, Building.Y) + Rect.Min, FVector2D(Building.X, Building.Y) + Rect.Max))
                            Edges[(CellY - MinY) * Width + CellX - MinX] |= static_cast<uint8>(1 << Direction);
                }
    }
    const int32 Start = (FMath::Clamp(FMath::FloorToInt(Unit.Position.Y), MinY, MinY + Height - 1) - MinY) * Width + FMath::Clamp(FMath::FloorToInt(Unit.Position.X), MinX, MinX + Width - 1) - MinX;
    TArray<int32> Parents; Parents.Init(-1, Count); Parents[Start] = Start;
    TArray<int32> Queue; Queue.Reserve(Count); Queue.Add(Start); int32 Found = -1;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const int32 Current = Queue[Index], CellX = Current % Width + MinX, CellY = Current / Width + MinY;
        const FVector2D Point(CellX + .5, CellY + .5);
        if (!Blocked[Current] && ProjectedLength(Destination - Point) <= Range && ClearSegment(Yard, Point, Destination, Ignore)) { Found = Current; break; }
        for (int32 Direction = 0; Direction < 4; ++Direction)
        {
            const int32 NX = CellX + DX[Direction], NY = CellY + DY[Direction];
            if (NX < MinX || NY < MinY || NX >= MinX + Width || NY >= MinY + Height || (Edges[Current] & (1 << Direction))) continue;
            const int32 Next = (NY - MinY) * Width + NX - MinX;
            if (Blocked[Next] || Parents[Next] != -1) continue;
            Parents[Next] = Current; Queue.Add(Next);
        }
    }
    if (Found == -1) return;
    TArray<FIntPoint> Reverse;
    for (int32 Cursor = Found; Cursor != Start; Cursor = Parents[Cursor]) Reverse.Add(FIntPoint(Cursor % Width + MinX, Cursor / Width + MinY));
    const FVector2D Center(Start % Width + MinX + .5, Start / Width + MinY + .5);
    if ((Center - Unit.Position).Size() > .0001 && ClearSegment(Yard, Unit.Position, Center, Ignore)) Unit.Path.Add(FIntPoint(Start % Width + MinX, Start / Width + MinY));
    for (int32 Index = Reverse.Num() - 1; Index >= 0; --Index) Unit.Path.Add(Reverse[Index]);
}

bool FBYMSimulation::FImpl::MoveToUnit(FTroop& Unit, const FYard& Yard, const FVector2D& Destination, double Range, double Speed, double Step, const FString& Ignore)
{
    const FVector2D Before = Unit.Position; const FVector2D Delta = Destination - Unit.Position;
    const double Length = ProjectedLength(Delta);
    if (Length <= Range && ClearSegment(Yard, Unit.Position, Destination, Ignore)) return true;
    const FMonsterDef* Definition = Monsters.Find(Unit.Type);
    double Budget = Speed * MovementMultiplier() * Step;
    if ((Definition && Definition->bFlying) || ClearSegment(Yard, Unit.Position, Destination, Ignore))
    {
        if (Length > .0001) Unit.Position += Delta / Length * FMath::Min(Budget, FMath::Max(0.0, Length - Range * .95));
    }
    else
    {
        // A failed route has an empty path. Respect its retry clock so an
        // enclosed target cannot force one full grid search per defender/tick.
        if (Unit.RepathAt <= Now || (!Unit.Path.IsEmpty() && Unit.PathIndex >= Unit.Path.Num()))
            BuildUnitPath(Unit, Yard, Destination, Range, Ignore);
        while (Budget > 0 && Unit.PathIndex < Unit.Path.Num())
        {
            const FIntPoint Cell = Unit.Path[Unit.PathIndex]; const FVector2D Next(Cell.X + .5, Cell.Y + .5);
            if (!ClearSegment(Yard, Unit.Position, Next, Ignore)) { Unit.Path.Reset(); Unit.PathIndex = 0; break; }
            const FVector2D Segment = Next - Unit.Position; const double Distance = ProjectedLength(Segment);
            if (Distance <= Budget) { Unit.Position = Next; Budget -= Distance; ++Unit.PathIndex; }
            else { Unit.Position += Segment / Distance * Budget; Budget = 0; }
            if (ProjectedLength(Destination - Unit.Position) <= Range && ClearSegment(Yard, Unit.Position, Destination, Ignore)) break;
        }
    }
    const FVector2D Movement = Unit.Position - Before;
    if (Movement.Size() > .0001) { Unit.State = TEXT("moving"); Unit.Facing = Movement.GetSafeNormal(); }
    Unit.TargetPosition = Destination;
    return ProjectedLength(Destination - Unit.Position) <= Range && ClearSegment(Yard, Unit.Position, Destination, Ignore);
}

void FBYMSimulation::FImpl::RemoveBunkerDefender(FBattle& B, FYard& Yard, FBunkerDefender& Unit)
{
    if (Unit.bDeathHandled || Unit.HP > 0 || Unit.bReturned) return;
    Unit.bDeathHandled = true; Unit.State = TEXT("dead"); Unit.Target.Reset(); Unit.Path.Reset();
    FBuilding* HomeBunker = FindBuilding(Yard, Unit.Bunker);
    FBuilding EventBuilding; if (HomeBunker) EventBuilding = *HomeBunker;
    if (HomeBunker)
    {
        const int32 Remaining = FMath::Max(0, HomeBunker->Garrison.FindRef(Unit.Type) - 1);
        if (Remaining > 0) HomeBunker->Garrison.Add(Unit.Type, Remaining); else HomeBunker->Garrison.Remove(Unit.Type);
    }
    CombatEvent(B, TEXT("troopDied"), Unit.Id, Unit.Id, Unit.Position, Unit.Position, Unit.Type, EventBuilding, false, 0);
    // Defensive Eye-Ra explodes once on death as well as on its suicide strike.
    // Mark the death first so the blast cannot replay after a save/reload.
    if (Unit.Type == TEXT("C5"))
    {
        const FMonsterDef* Definition = Monsters.Find(Unit.Type); if (!Definition) return;
        const double BaseDamage = FMath::Max(0.0, MonsterStats(*Definition, Unit.Level).Damage) * DamageMultiplier();
        double TotalDamage = 0;
        for (FTroop& Victim : B.Troops) if (Victim.HP > 0)
        {
            const FMonsterDef* VictimDefinition = Monsters.Find(Victim.Type); if (!VictimDefinition || VictimDefinition->bFlying) continue;
            const double Damage = FMath::Min(Victim.HP, BYMBunkerRules::DefensiveEyeRaDamage(BaseDamage, SourceGroundDistancePixels(Victim.Position, Unit.Position)));
            if (Damage <= 0) continue;
            Victim.HP = FMath::Max(0.0, Victim.HP - Damage); TotalDamage += Damage;
            CombatEvent(B, TEXT("explosionHit"), Unit.Id, Victim.Id, Unit.Position, Victim.Position, Unit.Type, EventBuilding, false, Damage);
            if (Victim.HP == 0) { Victim.State = TEXT("dead"); CombatEvent(B, TEXT("troopDied"), Unit.Id, Victim.Id, Unit.Position, Victim.Position, Victim.Type, EventBuilding, false, Damage); }
        }
        CombatEvent(B, TEXT("monsterExplode"), Unit.Id, Unit.Id, Unit.Position, Unit.Position, Unit.Type, EventBuilding, false, TotalDamage);
    }
}

void FBYMSimulation::FImpl::DestroyBattleBuilding(FBattle& B, FYard& Yard, FBuilding& Victim, const FTroop& Source, double AppliedDamage)
{
        CombatEvent(B, TEXT("buildingDestroyed"), Source.Id, Victim.Id, Source.Position, CombatCenter(Victim), Source.Type, Victim, false, AppliedDamage);
        if (Victim.Type == 13) DestroyHatcheryQueue(Yard, Victim.Id);
        if (Victim.Type == 22)
        {
            // Source Destroyed discards undispatched stock; Export omits all
            // stock while destroyed. Outside defenders may finish this fight,
            // but can never return into a destroyed Bunker or recreate stock.
            Victim.Garrison.Empty();
            for (FTroop& Attacker : B.Troops)
                if (B.Defenders.ContainsByPredicate([&](const FBunkerDefender& Unit) { return Unit.Bunker == Victim.Id && Unit.bReturned && Unit.Id == Attacker.TargetCreep; }))
                    Attacker.TargetCreep.Reset();
            B.Defenders.RemoveAll([&](const FBunkerDefender& Unit) { return Unit.Bunker == Victim.Id && Unit.bReturned; });
            FBunkerState& BunkerState = B.Bunkers.FindOrAdd(Victim.Id);
            BunkerState.DoorFrame = 0; BunkerState.State = TEXT("closed"); BunkerState.StateStartedAt = B.SimulatedAt; BunkerState.Targets.Reset(); BunkerState.TargetRefreshRemaining = 0;
        }
        Victim.Stored = 0; Victim.ProductionRemaining = 0;
        // BFOUNDATION.modifyHealth cancels repair before Destroyed applies the
        // upgrade setback. Construction remains level zero with its paid work.
        Victim.bRepairing = false;
        if (HasWork(Victim) && Victim.Level > 0)
        {
            const FBuildingDef* Definition = Buildings.Find(Victim.Type);
            if (Definition && Definition->Levels.IsValidIndex(Victim.Level))
                Victim.WorkRemaining = BYMBuildingLifecycle::AfterDestruction(Victim.WorkRemaining,
                    FMath::Clamp(Definition->Levels[Victim.Level].Seconds * ConstructionMultiplier(), 1.0, 31536000.0));
        }
}

void FBYMSimulation::FImpl::ExplodeAttacker(FBattle& B, FYard& Yard, FTroop& Attacker, const FString& PrimaryDefender)
{
    const FMonsterDef* Definition = Monsters.Find(Attacker.Type); if (!Definition || Attacker.HP <= 0) return;
    const double BaseDamage = FMath::Max(0.0, MonsterStats(*Definition, Attacker.Level).Damage) * DamageMultiplier() * MapAttackBonus(B);
    FBuilding EventBuilding;
    if (const FBuilding* Target = FindBuilding(Yard, Attacker.Target)) EventBuilding = *Target;
    const double RemainingHealth = Attacker.HP; Attacker.HP = 0; Attacker.State = TEXT("dead");
    double TotalDamage = 0;
    // CreepBase.explode always strikes nearby buildings, even when its primary
    // target is a defending creep. All lethal paths share lifecycle side effects.
    for (FBuilding& Victim : Yard.Buildings) if (Victim.HP > 0)
    {
        const FBuildingDef* BuildingDefinition = Buildings.Find(Victim.Type);
        if (!BuildingDefinition || Victim.Type == 139 || BuildingDefinition->Type == TEXT("decoration") || BuildingDefinition->Type == TEXT("enemy")) continue;
        const double Damage = FMath::Min(Victim.HP, BYMCombatRules::EyeRaBuildingDamage(BaseDamage * (MapAttackBonus(B) > 0 ? MapBuildingDamageMultiplier(B) / MapAttackBonus(B) : 1.) * (Victim.Fortification > 0 ? 1. - (Victim.Fortification * 10. + 10.) * .01 : 1.), Attacker.Position, FVector2D(Victim.X, Victim.Y)));
        if (Damage <= 0) continue;
        Victim.HP = FMath::Max(0.0, Victim.HP - Damage); TotalDamage += Damage;
        CombatEvent(B, TEXT("explosionHit"), Attacker.Id, Victim.Id, Attacker.Position, CombatCenter(Victim), Attacker.Type, Victim, false, Damage);
        if (Victim.HP == 0) DestroyBattleBuilding(B, Yard, Victim, Attacker, Damage);
    }
    for (FBunkerDefender& Victim : B.Defenders) if (Victim.HP > 0 && !Victim.bReturned)
    {
        const double Damage = FMath::Min(Victim.HP, BYMBunkerRules::OffensiveEyeRaDamageToDefender(BaseDamage,
            ProjectedLength(Victim.Position - Attacker.Position) * SourcePixelsPerCell, Victim.Id == PrimaryDefender));
        if (Damage <= 0) continue;
        Victim.HP = FMath::Max(0.0, Victim.HP - Damage); TotalDamage += Damage;
        CombatEvent(B, TEXT("explosionHit"), Attacker.Id, Victim.Id, Attacker.Position, Victim.Position, Attacker.Type, EventBuilding, false, Damage);
        if (Victim.HP == 0) RemoveBunkerDefender(B, Yard, Victim);
    }
    Attacker.Target.Reset(); Attacker.TargetCreep.Reset(); Attacker.Path.Reset();
    CombatEvent(B, TEXT("monsterExplode"), Attacker.Id, EventBuilding.Id, Attacker.Position, Attacker.Position, Attacker.Type, EventBuilding, false, TotalDamage);
    CombatEvent(B, TEXT("troopDied"), Attacker.Id, Attacker.Id, Attacker.Position, Attacker.Position, Attacker.Type, EventBuilding, false, RemainingHealth);
}

bool FBYMSimulation::FImpl::FightDefender(FBattle& B, FYard& Yard, FTroop& Attacker, double Step)
{
    const FMonsterDef* Definition = Monsters.Find(Attacker.Type); if (!Definition || Attacker.HP <= 0) return false;
    const FMonsterStats& S = MonsterStats(*Definition, Attacker.Level);
    FBunkerDefender* Target = B.Defenders.FindByPredicate([&](const FBunkerDefender& Unit) { return Unit.Id == Attacker.TargetCreep && Unit.HP > 0 && !Unit.bReturned; });
    if (!Target && S.TargetGroup == 6)
    {
        double Closest = TNumericLimits<double>::Max();
        for (FBunkerDefender& Candidate : B.Defenders) if (Candidate.HP > 0 && !Candidate.bReturned)
        {
            const double Distance = (Candidate.Position - Attacker.Position).Size();
            if (Distance < Closest) { Closest = Distance; Target = &Candidate; }
        }
        if (Target) Attacker.TargetCreep = Target->Id;
    }
    if (!Target) { Attacker.TargetCreep.Reset(); return false; }
    Attacker.Target.Reset(); const double Range = S.SourceRange > 0 ? S.Range : BYMBunkerRules::DefensiveMeleeRangePixels / SourcePixelsPerCell;
    const bool InRange = MoveToUnit(Attacker, Yard, Target->Position, Range, S.Speed, Step, FString());
    if (!InRange)
    {
        if (Attacker.State != TEXT("moving") && Attacker.Path.IsEmpty()) { Attacker.TargetCreep.Reset(); return false; }
        return true;
    }
    Attacker.State = TEXT("attacking"); const FVector2D Aim = Target->Position - Attacker.Position;
    if (Aim.Size() > .0001) Attacker.Facing = Aim.GetSafeNormal();
    if (Attacker.AttackCooldown > 0) Attacker.AttackCooldown -= Step;
    if (Attacker.AttackCooldown > 0) return true;
    Attacker.AttackCooldown += S.AttackInterval; Attacker.LastAttackAt = B.SimulatedAt; ++Attacker.AttackSequence;
    FBuilding EventBuilding; if (const FBuilding* HomeBunker = FindBuilding(Yard, Target->Bunker)) EventBuilding = *HomeBunker;
    if (Attacker.Type == TEXT("C5"))
    {
        const FString Primary = Target->Id; ExplodeAttacker(B, Yard, Attacker, Primary); return true;
    }
    const double Damage = FMath::Min(Target->HP, FMath::Max(0.0, S.Damage) * DamageMultiplier() * MapAttackBonus(B) * (S.TargetGroup == 6 && S.SourceRange <= 0 ? 3.0 : 1.0));
    Target->HP = FMath::Max(0.0, Target->HP - Damage);
    CombatEvent(B, TEXT("monsterAttack"), Attacker.Id, Target->Id, Attacker.Position, Target->Position, Attacker.Type, EventBuilding, S.SourceRange > 0, Damage);
    if (Target->HP == 0) { RemoveBunkerDefender(B, Yard, *Target); Attacker.TargetCreep.Reset(); }
    return true;
}

void FBYMSimulation::FImpl::TickBunkers(FBattle& B, FYard& Yard, double Step)
{
    // Ordinary pre-Map-Room-3 stock persists as counts, not per-monster health.
    // Returned records are reused: sortie cycles never grow a battle save.
    for (FBuilding& HomeBunker : Yard.Buildings) if (HomeBunker.Type == 22)
    {
        FBunkerState& State = B.Bunkers.FindOrAdd(HomeBunker.Id);
        State.ReleaseRemaining -= Step; bool bRelease = false;
        if (State.ReleaseRemaining <= .000001) { State.ReleaseRemaining += BYMBunkerRules::ReleaseIntervalSeconds; bRelease = true; }
        TArray<FTroop*> Threats;
        State.TargetRefreshRemaining = FMath::Max(0.0, State.TargetRefreshRemaining - Step);
        if (HomeBunker.Level > 0 && HomeBunker.HP > 0 && !HasWork(HomeBunker))
        {
            bool bInvalidTarget = State.Targets.IsEmpty();
            for (const FString& Id : State.Targets)
            {
                FTroop* Target = B.Troops.FindByPredicate([&](const FTroop& Attacker) { return Attacker.Id == Id && Attacker.HP > 0; });
                if (Target) Threats.Add(Target); else bInvalidTarget = true;
            }
            // BUILDING22 refreshes empty/invalid targets every 10 display frames,
            // and refreshes its nearest-three cache every 60 at 40Hz. Persist
            // these clocks rather than picking a new nearest target every tick.
            if (bInvalidTarget)
            {
                Threats.Reset(); State.Targets.Reset();
                State.TargetRefreshRemaining = FMath::Min(State.TargetRefreshRemaining, BYMBunkerRules::RetargetIntervalSeconds);
            }
            if (State.TargetRefreshRemaining <= .000001)
            {
                Threats.Reset(); const double Range = BYMBunkerRules::TriggerRangePixels(HomeBunker.Level);
                const FVector2D Origin = CombatCenter(HomeBunker);
                for (FTroop& Attacker : B.Troops) if (Attacker.HP > 0)
                {
                    const FMonsterDef* Definition = Monsters.Find(Attacker.Type);
                    if (Definition && !Definition->bFlying && SourceGroundDistancePixels(Attacker.Position, Origin) < Range) Threats.Add(&Attacker);
                }
                Threats.Sort([&](const FTroop& A, const FTroop& C) { return SourceGroundDistancePixels(A.Position, Origin) < SourceGroundDistancePixels(C.Position, Origin); });
                if (Threats.Num() > 3) Threats.SetNum(3);
                State.Targets.Reset(); for (const FTroop* Target : Threats) State.Targets.Add(Target->Id);
                State.TargetRefreshRemaining = Threats.IsEmpty() ? BYMBunkerRules::RetargetIntervalSeconds : 60.0 / 40.0;
            }
        }
        else { State.Targets.Reset(); State.TargetRefreshRemaining = 0; }
        int32 Dispatched = 0;
        for (const FBunkerDefender& Unit : B.Defenders) if (Unit.Bunker == HomeBunker.Id && Unit.HP > 0 && !Unit.bReturned) ++Dispatched;
        const bool bOpen = HomeBunker.HP > 0 && BunkerUsed(Yard, HomeBunker) > 0 && (!Threats.IsEmpty() || Dispatched > 0);
        const FString Before = State.State;
        const double PreviousFrame = State.DoorFrame;
        State.DoorFrame = FMath::Clamp(State.DoorFrame + (bOpen ? Step : -Step) * BYMBunkerRules::DoorFrameRate, 0.0, static_cast<double>(BYMBunkerRules::DoorFrames));
        State.State = bOpen ? (State.DoorFrame >= BYMBunkerRules::DoorFrames ? TEXT("open") : TEXT("opening")) : (State.DoorFrame <= 0 ? TEXT("closed") : TEXT("closing"));
        if (State.State != Before) State.StateStartedAt = B.SimulatedAt;
        if ((bOpen && PreviousFrame == 0) || (!bOpen && PreviousFrame == BYMBunkerRules::DoorFrames))
            CombatEvent(B, TEXT("bunkerDoor"), HomeBunker.Id, HomeBunker.Id, CombatCenter(HomeBunker), CombatCenter(HomeBunker), FString(), HomeBunker, false, 0);
        if (!bRelease || !bOpen || Threats.IsEmpty() || State.DoorFrame < BYMBunkerRules::DoorFrames) continue;
        TArray<FString> Species; HomeBunker.Garrison.GetKeys(Species); Species.Sort();
        for (const FString& Type : Species)
        {
            int32 Active = 0;
            for (const FBunkerDefender& Unit : B.Defenders) if (Unit.Bunker == HomeBunker.Id && Unit.Type == Type && Unit.HP > 0 && !Unit.bReturned) ++Active;
            if (HomeBunker.Garrison.FindRef(Type) <= Active) continue;
            const FMonsterDef* Definition = Monsters.Find(Type); if (!Definition) continue;
            FBunkerDefender* Unit = B.Defenders.FindByPredicate([&](const FBunkerDefender& Candidate) { return Candidate.Bunker == HomeBunker.Id && Candidate.Type == Type && Candidate.HP > 0 && Candidate.bReturned; });
            if (!Unit)
            {
                if (B.Defenders.Num() >= 1024) break;
                FBunkerDefender Added; Added.Id = NewId(); Added.Bunker = HomeBunker.Id; Added.Type = Type; B.Defenders.Add(MoveTemp(Added)); Unit = &B.Defenders.Last();
            }
            // The original Dictionary iteration/random target order is not a
            // stable network contract. Cycle the nearest three deterministically.
            FTroop* Target = Threats[(GetTypeHash(Unit->Id) + static_cast<uint32>(Unit->AttackSequence)) % Threats.Num()];
            Unit->Level = FMath::Max(1, MonsterLevel(Yard, Type)); Unit->HP = MonsterStats(*Definition, Unit->Level).HP;
            Unit->bReturned = false; Unit->bDeathHandled = false; Unit->State = TEXT("moving"); Unit->Target = Target->Id;
            Unit->AttackCooldown = 0; Unit->RetargetRemaining = 150.0 / 40.0; Unit->Path.Reset(); Unit->PathIndex = 0; Unit->RepathAt = 0;
            Unit->Position = BunkerDoorPoint(HomeBunker, Target->Position); Unit->TargetPosition = Target->Position;
            Unit->Facing = (Target->Position - Unit->Position).GetSafeNormal(); Unit->DeployedAt = B.SimulatedAt;
            break;
        }
    }
    for (FBunkerDefender& Unit : B.Defenders)
    {
        if (Unit.HP <= 0) { RemoveBunkerDefender(B, Yard, Unit); continue; }
        if (Unit.bReturned) continue;
        Unit.State = TEXT("idle"); const FMonsterDef* Definition = Monsters.Find(Unit.Type); if (!Definition) continue;
        const FMonsterStats& S = MonsterStats(*Definition, Unit.Level);
        FBuilding* HomeBunker = FindBuilding(Yard, Unit.Bunker);
        FTroop* Target = B.Troops.FindByPredicate([&](const FTroop& Attacker) { return Attacker.Id == Unit.Target && Attacker.HP > 0; });
        const double Range = S.SourceRange > 0 ? S.Range : BYMBunkerRules::DefensiveMeleeRangePixels / SourcePixelsPerCell;
        const bool bHoldingTarget = Target && ProjectedLength(Target->Position - Unit.Position) <= Range
            && ClearSegment(Yard, Unit.Position, Target->Position, Unit.Bunker);
        Unit.RetargetRemaining = FMath::Max(0.0, Unit.RetargetRemaining - Step);
        const bool bRefresh = Unit.RetargetRemaining <= .000001;
        if (bRefresh) Unit.RetargetRemaining = 150.0 / 40.0;
        // CreepBase.tickBDefend retains a live opponent during contact. Moving
        // defenders search locally every 150 display frames; death/return can
        // reacquire immediately. A current living target may be pursued beyond
        // the Bunker's acquisition range if no closer local target is found.
        if (!Target || (bRefresh && !bHoldingTarget))
        {
            FTroop* Nearest = nullptr; double Closest = BYMBunkerRules::DefenseSearchRangePixels;
            for (FTroop& Attacker : B.Troops) if (Attacker.HP > 0)
            {
                const FMonsterDef* AttackerDefinition = Monsters.Find(Attacker.Type); if (!AttackerDefinition || AttackerDefinition->bFlying) continue;
                const double Distance = SourceGroundDistancePixels(Attacker.Position, Unit.Position); if (Distance < Closest) { Closest = Distance; Nearest = &Attacker; }
            }
            if (Nearest) Target = Nearest;
        }
        if (!Target && HomeBunker && HomeBunker->HP > 0 && !HasWork(*HomeBunker))
        {
            if (const FBunkerState* State = B.Bunkers.Find(HomeBunker->Id))
                for (const FString& TargetId : State->Targets)
                {
                    FTroop* Candidate = B.Troops.FindByPredicate([&](const FTroop& Attacker) { return Attacker.Id == TargetId && Attacker.HP > 0; });
                    if (Candidate) { Target = Candidate; break; }
                }
        }
        if (!Target)
        {
            Unit.Target.Reset();
            if (HomeBunker && HomeBunker->HP > 0)
            {
                if (MoveToUnit(Unit, Yard, BunkerDoorPoint(*HomeBunker, Unit.Position), .5, S.Speed * BYMBunkerRules::DefenderSpeedMultiplier, Step, Unit.Bunker))
                { Unit.bReturned = true; Unit.State = TEXT("returned"); Unit.Path.Reset(); }
            }
            continue;
        }
        if (Unit.Target != Target->Id) { Unit.Target = Target->Id; Unit.Path.Reset(); Unit.RepathAt = 0; }
        if (!MoveToUnit(Unit, Yard, Target->Position, Range, S.Speed * BYMBunkerRules::DefenderSpeedMultiplier, Step, Unit.Bunker)) continue;
        Unit.State = TEXT("attacking"); const FVector2D Aim = Target->Position - Unit.Position;
        if (Aim.Size() > .0001) Unit.Facing = Aim.GetSafeNormal();
        if (Target->Type != TEXT("C5") && Unit.Type != TEXT("C5") && Target->TargetCreep.IsEmpty()) { Target->TargetCreep = Unit.Id; Target->Path.Reset(); Target->RepathAt = 0; }
        if (Unit.AttackCooldown > 0) Unit.AttackCooldown -= Step;
        if (Unit.AttackCooldown > 0) continue;
        Unit.AttackCooldown += S.AttackInterval; Unit.LastAttackAt = B.SimulatedAt; ++Unit.AttackSequence;
        if (Unit.Type == TEXT("C5")) { Unit.HP = 0; RemoveBunkerDefender(B, Yard, Unit); continue; }
        FBuilding EventBuilding; if (HomeBunker) EventBuilding = *HomeBunker;
        const double Damage = FMath::Min(Target->HP, FMath::Max(0.0, S.Damage) * DamageMultiplier() * (S.TargetGroup == 6 && S.SourceRange <= 0 ? 3.0 : 1.0));
        Target->HP = FMath::Max(0.0, Target->HP - Damage);
        CombatEvent(B, TEXT("monsterAttack"), Unit.Id, Target->Id, Unit.Position, Target->Position, Unit.Type, EventBuilding, S.SourceRange > 0, Damage);
        if (Target->HP <= 0) { Target->State = TEXT("dead"); CombatEvent(B, TEXT("troopDied"), Unit.Id, Target->Id, Unit.Position, Target->Position, Target->Type, EventBuilding, false, Damage); Unit.Target.Reset(); }
        int32 Aggro = 0;
        for (FTroop& Attacker : B.Troops) if (Attacker.HP > 0 && Attacker.Type != TEXT("C5") && Aggro < 5)
        {
            const FMonsterDef* AttackerDefinition = Monsters.Find(Attacker.Type);
            if (AttackerDefinition && !AttackerDefinition->bFlying && SourceGroundDistancePixels(Attacker.Position, Unit.Position) < BYMBunkerRules::RetaliationRangePixels)
            { Attacker.TargetCreep = Unit.Id; Attacker.Path.Reset(); Attacker.RepathAt = 0; ++Aggro; }
        }
    }
}

void FBYMSimulation::FImpl::TickBattle(FBattle& B, double Step)
{
    FYard* Defender = Yards.Find(B.Defender); if (!Defender) return;
    B.SimulatedAt = FMath::Max(B.SimulatedAt + Step, Now - CombatAccumulator + Step);
    // Accrue to this damage timestamp before changing production eligibility.
    Advance(*Defender, B.SimulatedAt);
    B.Events.RemoveAll([&B](const FObject& Event) { return Number(Event, TEXT("at")) < B.SimulatedAt - 3.0; });
    TickBunkers(B, *Defender, Step);
    for (FTroop& T : B.Troops)
    {
        T.State = T.HP <= 0 ? TEXT("dead") : TEXT("idle");
        if (T.HP <= 0) continue;
        const FMonsterDef* M = Monsters.Find(T.Type); if (!M) continue;
        const FMonsterStats& S = MonsterStats(*M, T.Level);
        if (FightDefender(B, *Defender, T, Step)) continue;
        FBuilding* Target = FindBuilding(*Defender, T.Target);
        if (!Target || Target->HP <= 0)
        {
            double Best = TNumericLimits<double>::Max(); int32 BestTier = 2; Target = nullptr;
            for (FBuilding& Candidate : Defender->Buildings) if (Candidate.HP > 0)
            {
                const FBuildingDef* BD = Buildings.Find(Candidate.Type);
                if (!BD || Candidate.Type == 139 || BD->Type == TEXT("decoration") || BD->Type == TEXT("immovable")
                    || BD->Type == TEXT("enemy") || BD->Type == TEXT("trap") || BD->Type == TEXT("cage")) continue;
                // MonsterBase.findTarget searches the complete preferred set
                // first. A nearby nonpreferred building must not outweigh a
                // distant preferred target through a distance multiplier.
                const bool Lootable = (Candidate.Type >= 1 && Candidate.Type <= 4) || Candidate.Type == 6 || Candidate.Type == 14;
                const bool PreferredDefense = BD->Type == TEXT("tower") && (Candidate.Type != 22 || BunkerUsed(*Defender, Candidate) > 0);
                const int32 Tier = S.TargetGroup == 6 && Candidate.Type == 22 && BunkerUsed(*Defender, Candidate) > 0 ? 0
                    : (S.TargetGroup == 6 ? (BD->Type == TEXT("wall") ? -1 : 1) : BYMCombatRules::TargetTier(S.TargetGroup, BD->Type == TEXT("wall"), Lootable, PreferredDefense));
                if (Tier < 0 || Tier > BestTier) continue;
                const double HalfHeight = BD->CombatRects.IsEmpty() ? .5 : (BD->CombatRects[0].Max.Y - BD->CombatRects[0].Min.Y) * .5;
                const double Score = BYMCombatRules::TargetDistance(T.Position, FVector2D(Candidate.X, Candidate.Y), HalfHeight);
                if (Tier < BestTier || Score < Best) { BestTier = Tier; Best = Score; Target = &Candidate; }
            }
            T.Target = Target ? Target->Id : FString(); T.Path.Reset(); T.RepathAt = 0;
        }
        if (!Target) continue;
        const FVector2D Before = T.Position;
        const double Range = S.Range;
        if (!CanAttack(*Defender, *Target, T.Position, Range))
        {
            if (M->bFlying)
            {
                const FVector2D Destination = CombatPoint(*Target, T.Position);
                const FVector2D Delta = Destination - T.Position;
                const double Length = ProjectedLength(Delta);
                if (Length > 0.0001)
                {
                    // Ground structures can obstruct a ranged shot, but cannot
                    // strand a flyer at nominal range. Continue over the
                    // obstruction until the target is visible.
                    const double Remaining = ClearSegment(*Defender, T.Position, Destination, Target->Id) ? FMath::Max(0.0, Length - Range + 0.0001) : Length;
                    T.Position += Delta / Length * FMath::Min(S.Speed * MovementMultiplier() * Step, Remaining);
                }
            }
            else
            {
                // Empty failed routes wait for BuildPath's retry clock. A
                // consumed successful path can still be refreshed immediately.
                if (T.RepathAt <= Now || (!T.Path.IsEmpty() && T.PathIndex >= T.Path.Num()))
                    BuildPath(T, *Defender, *Target, Range);
                Target = FindBuilding(*Defender, T.Target); if (!Target) continue;
                double Budget = S.Speed * MovementMultiplier() * Step;
                while (Budget > 0 && T.PathIndex < T.Path.Num())
                {
                    const FIntPoint Cell = T.Path[T.PathIndex]; const FVector2D Destination(Cell.X + 0.5, Cell.Y + 0.5);
                    const FVector2D Delta = Destination - T.Position; const double Length = ProjectedLength(Delta);
                    if (!ClearSegment(*Defender, T.Position, Destination)) { T.Path.Reset(); T.PathIndex = 0; break; }
                    if (Length <= Budget) { T.Position = Destination; Budget -= Length; ++T.PathIndex; }
                    else { T.Position += Delta / Length * Budget; Budget = 0; }
                    if (CanAttack(*Defender, *Target, T.Position, Range)) break;
                }
            }
        }
        const FVector2D Movement = T.Position - Before;
        if (Movement.Size() > 0.0001) { T.State = TEXT("moving"); T.Facing = Movement.GetSafeNormal(); }
        T.TargetPosition = CombatPoint(*Target, T.Position);
        if (CanAttack(*Defender, *Target, T.Position, Range))
        {
            T.State = TEXT("attacking");
            const FVector2D Aim = CombatCenter(*Target) - T.Position;
            if (Aim.Size() > 0.0001) T.Facing = Aim.GetSafeNormal();
            // Source CreepBase.tickBAttack advances the cooldown only while at
            // the target. Preserve fractional debt instead of rounding every
            // strike to a fresh host tick (or damaging continuously).
            if (T.AttackCooldown > 0) T.AttackCooldown -= Step;
            if (T.AttackCooldown <= 0)
            {
                T.AttackCooldown += S.AttackInterval; T.LastAttackAt = B.SimulatedAt; ++T.AttackSequence;
                if (T.Type == TEXT("C5"))
                { ExplodeAttacker(B, *Defender, T); continue; }
                const FBuildingDef* BD = Buildings.Find(Target->Type);
                const double PreferredDamage = BD && ((S.TargetGroup == 2 && BD->Type == TEXT("wall")) || (S.TargetGroup == 4 && BD->Type == TEXT("tower"))) ? 2.0 : 1.0;
                const double Damage = FMath::Min(Target->HP, FMath::Max(0.0, S.Damage) * DamageMultiplier() * MapBuildingDamageMultiplier(B) * (Target->Fortification > 0 ? 1. - (Target->Fortification * 10. + 10.) * .01 : 1.) * PreferredDamage);
                Target->HP = FMath::Max(0.0, Target->HP - Damage);
                CombatEvent(B, TEXT("monsterAttack"), T.Id, Target->Id, T.Position, T.TargetPosition, T.Type, *Target, S.SourceRange > 0, Damage);
                if (Target->HP == 0)
                {
                    DestroyBattleBuilding(B, *Defender, *Target, T, Damage);
                    T.Target.Reset(); T.Path.Reset();
                }
            }
        }
    }
    // Defenses select targets and resolve every strike on the host. Visual
    // events cannot submit damage, create a projectile hit or decide victory.
    for (FBuilding& Defense : Defender->Buildings)
    {
        // BTOWER.canAttack blocks construction/upgrades, not ongoing repairs.
        if (TickMapStronghold(B, *Defender, Defense, Step)) continue;
        const FLevel* L = Stats(Defense); if (!L || Defense.HP <= 0 || Defense.Level == 0 || HasWork(Defense) || L->Damage <= 0 || L->Range <= 0) continue;
        double& Cooldown = B.DefenseCooldown.FindOrAdd(Defense.Id);
        FTroop* Target = nullptr; double Closest = L->Range;
        const FVector2D Origin = CombatCenter(Defense);
        const FBuildingDef* Definition = Buildings.Find(Defense.Type);
        const int32 TargetMode = Definition ? Definition->TargetFlyerMode : 1;
        auto CanTarget = [&](const FTroop& T)
        {
            const FMonsterDef* Monster = Monsters.Find(T.Type);
            return Monster && (TargetMode == 1 || (TargetMode == 2 ? Monster->bFlying : !Monster->bFlying));
        };
        for (FTroop& T : B.Troops) if (T.HP > 0 && CanTarget(T))
        {
            // Tower Targeting.getCreepsInRange converts FromISO before testing
            // its radius; unlike creep locomotion, this is a ground-space metric.
            const double Dist = (T.Position - Origin).Size(); if (Dist <= Closest) { Closest = Dist; Target = &T; }
        }
        FDefenseBurst* Burst = nullptr;
        auto BeginCooling = [&]()
        {
            if (!Burst) return;
            const int32 Fired = L->BurstShots - Burst->ShotsRemaining;
            Burst->CoolingStartFrame = 32 + ((Fired * 4) % 9);
            Burst->Phase = TEXT("cooling"); Burst->PhaseSeconds = (55 - Burst->CoolingStartFrame) * 2.0 / 40.0;
            Burst->Remaining = Burst->PhaseSeconds; Burst->StartedAt = B.SimulatedAt; Cooldown = 0;
        };
        if (Defense.Type == 25)
        {
            // BUILDING25 has a charge, finite pulse train and a cooling
            // animation. Its 4/40-second pulse interval is not an endless DPS
            // interval; keep these phases authoritative and saveable.
            Burst = &B.DefenseBursts.FindOrAdd(Defense.Id);
            if (Burst->Phase == TEXT("idle"))
            {
                if (Target)
                {
                    Burst->Phase = TEXT("charging"); Burst->PhaseSeconds = 32.0 / 40.0;
                    Burst->Remaining = Burst->PhaseSeconds; Burst->StartedAt = B.SimulatedAt;
                    Burst->ShotsRemaining = L->BurstShots; Cooldown = 0;
                    FDefenseVisual& Visual = B.DefenseVisuals.FindOrAdd(Defense.Id);
                    Visual.Target = Target->Id; Visual.Facing = (Target->Position - Origin).GetSafeNormal();
                }
                continue;
            }
            if (Burst->Phase == TEXT("charging") || Burst->Phase == TEXT("cooling"))
            {
                Burst->Remaining = FMath::Max(0.0, Burst->Remaining - Step);
                if (Burst->Remaining > 0.000001) continue;
                Burst->PhaseSeconds = 0; Burst->Remaining = 0; Burst->StartedAt = B.SimulatedAt;
                if (Burst->Phase == TEXT("cooling")) { Burst->Phase = TEXT("idle"); Burst->ShotsRemaining = 0; }
                else { Burst->Phase = TEXT("active"); Cooldown = L->AttackInterval; }
                continue;
            }
            if (!Target) { BeginCooling(); continue; }
        }
        if (Cooldown > 0) Cooldown -= Step;
        if (Cooldown > 0) continue;
        if (Target)
        {
            const FVector2D Impact = Target->Position;
            // Source Fire methods truncate the per-shot amount after their
            // health multiplier; a damaged tower does not keep full firepower.
            const double ShotDamage = BYMCombatRules::TowerShotDamage(L->Damage, Defense.HP, L->HP, DamageMultiplier() * MapTowerBonus(B));
            FDefenseVisual& Visual = B.DefenseVisuals.FindOrAdd(Defense.Id);
            Visual.Target = Target->Id; Visual.Facing = (Impact - Origin).GetSafeNormal(); Visual.LastAttackAt = B.SimulatedAt; ++Visual.AttackSequence;
            CombatEvent(B, TEXT("defenseAttack"), Defense.Id, Target->Id, Origin, Impact, Target->Type, Defense, true, FMath::Min(Target->HP, ShotDamage));
            for (FTroop& T : B.Troops) if (T.HP > 0 && CanTarget(T) && (&T == Target || (L->Splash > 0 && (T.Position - Impact).Size() <= L->Splash)))
            {
                const double Damage = FMath::Min(T.HP, ShotDamage);
                T.HP = FMath::Max(0.0, T.HP - Damage);
                if (T.HP == 0)
                { T.State = TEXT("dead"); CombatEvent(B, TEXT("troopDied"), Defense.Id, T.Id, Origin, T.Position, T.Type, Defense, true, Damage); }
            }
            Cooldown += L->AttackInterval;
            if (Burst && --Burst->ShotsRemaining <= 0) BeginCooling();
        }
        else Cooldown = 0;
    }
}

void FBYMSimulation::FImpl::EndBattle(const FString& AttackerId, bool bAttackerWon, const FString& Reason, bool bContested)
{
    FBattle* B = Battles.Find(AttackerId); if (!B) return;
    FYard* Attacker = Yards.Find(B->Attacker); FYard* Defender = Yards.Find(B->Defender);
    // Settle repair/hatching through the actual end time while UnderAttack
    // still blocks collector production. This also covers expiry and scouting
    // cancellation without a final combat step; never discard elapsed work.
    if (Defender) Advance(*Defender);
    const FMapCell* BattleCell = MapCells.Find(B->MapCellId);
    const bool bMapSite = BattleCell && BattleCell->Kind != 101;
    if (BattleCell && Defender && bContested) bAttackerWon = MapDamagePercent(*Defender) >= 90.;
    FYard* DefendingOwner = bMapSite && BattleCell ? Yards.Find(BattleCell->Owner) : Defender;
    if (Attacker && Defender && bContested)
    {
        const UBYMSettings* Settings = GetDefault<UBYMSettings>();
        Defender->ShieldUntil = bMapSite ? 0. : Now + FMath::Clamp(static_cast<double>(Settings->ShieldSeconds), 0.0, 31536000.0);
        FYard& Winner = bAttackerWon ? *Attacker : (DefendingOwner ? *DefendingOwner : *Defender); FYard& Loser = bAttackerWon ? (DefendingOwner ? *DefendingOwner : *Defender) : *Attacker;
        if (!bMapSite || DefendingOwner)
        {
        Winner.RP = FMath::Min<int64>(static_cast<int64>(ResourceLimit), Winner.RP + FMath::Clamp(Settings->RPWin, 0, 1000000));
        const int64 Minimum = Settings->bClampRPAtZero ? 0 : -static_cast<int64>(ResourceLimit);
        Loser.RP = FMath::Max<int64>(Minimum, Loser.RP - FMath::Clamp(Settings->RPLoss, 0, 1000000));
        Winner.Wins = FMath::Min(Winner.Wins + 1, 100000000); Loser.Losses = FMath::Min(Loser.Losses + 1, 100000000);
        }
        FYard* LootOwner = DefendingOwner ? DefendingOwner : Defender;
        int64 Loot[4] = {0, 0, 0, 0};
        if (bAttackerWon)
        {
            const int64 Cap = Storage(*Attacker);
            for (int32 I = 0; I < 4; ++I)
            {
                Loot[I] = FMath::Min<int64>(static_cast<int64>(LootOwner->Resource[I] * FMath::Clamp(static_cast<double>(Settings->InvasionLootFraction), 0.0, 1.0)), FMath::Max<int64>(0, Cap - Attacker->Resource[I]));
                LootOwner->Resource[I] -= Loot[I]; Attacker->Resource[I] += Loot[I];
                Attacker->EarnedExperience = BYMExperience::Add(Attacker->EarnedExperience, BYMExperience::LootReward(Loot[I]));
            }
        }
        for (FYard* Y : {Attacker, LootOwner})
        {
            FObject L = Obj(); L->SetStringField(TEXT("battle"), B->Id); L->SetStringField(TEXT("opponent"), Y == Attacker ? Defender->Name : Attacker->Name);
            L->SetStringField(TEXT("role"), Y == Attacker ? TEXT("attacker") : TEXT("defender"));
            L->SetBoolField(TEXT("won"), Y == &Winner); L->SetBoolField(TEXT("contested"), true);
            L->SetStringField(TEXT("reason"), Reason); L->SetNumberField(TEXT("at"), Now);
            L->SetObjectField(TEXT("loot"), ResourceObject(Loot)); Y->Log.Insert(L, 0); if (Y->Log.Num() > 20) Y->Log.SetNum(20);
        }
    }
    else if (Attacker)
    {
        // Scouting expiry/cancellation has a visible outcome, but never awards RP,
        // removes reserves, or shields a defender who was not actually attacked.
        FObject L = Obj(); L->SetStringField(TEXT("battle"), B->Id);
        L->SetStringField(TEXT("opponent"), Defender ? Defender->Name : TEXT("Unavailable yard"));
        L->SetStringField(TEXT("role"), TEXT("attacker")); L->SetBoolField(TEXT("won"), false);
        L->SetBoolField(TEXT("contested"), false); L->SetStringField(TEXT("reason"), Reason); L->SetNumberField(TEXT("at"), Now);
        const int64 Loot[4] = {0, 0, 0, 0}; L->SetObjectField(TEXT("loot"), ResourceObject(Loot));
        Attacker->Log.Insert(L, 0); if (Attacker->Log.Num() > 20) Attacker->Log.SetNum(20);
    }
    FinishMapBattle(*B, bAttackerWon, bContested);
    if (Attacker && !Attacker->Log.IsEmpty() && BattleCell)
    {
        FObject L = Attacker->Log[0]; L->SetStringField(TEXT("mapCellId"), B->MapCellId);
        L->SetNumberField(TEXT("territoryKind"), BattleCell->Kind); L->SetStringField(TEXT("territoryName"), Defender ? Defender->Name : FString());
        L->SetBoolField(TEXT("captured"), bContested && bAttackerWon && BattleCell->Kind >= 102 && BattleCell->Kind <= 104);
    }
    // The terminal strike and death/destruction events must survive removal
    // of battle authority long enough for the next client snapshot to see them.
    PruneCompletedBattles();
    FObject Presentation = BattleJSON(*B, false);
    Presentation->SetStringField(TEXT("phase"), TEXT("completed")); Presentation->SetBoolField(TEXT("canDeploy"), false);
    Presentation->SetNumberField(TEXT("remainingSeconds"), 0); Presentation->SetNumberField(TEXT("finishedAt"), Now);
    CompletedBattles.Add(B->Defender, Presentation);
    Battles.Remove(AttackerId);
}

void FBYMSimulation::FImpl::BotAction(FYard& Y)
{
    Advance(Y);
    auto Command = [&](const TCHAR* Operation, const FObject& Params) { FObject Response; Decode(Execute(Y.Id, Operation, Params), Response); return Flag(Response, TEXT("ok")); };
    auto DeployArmy = [&]()
    {
        FBattle* Battle = Battles.Find(Y.Id); if (!Battle) return;
        const FYard* TargetYard = Yards.Find(Battle->Defender); if (!TargetYard) return;
        int64 Capacity = 0;
        for (const FBuilding& F : Y.Buildings) if (F.Type == 5 && F.HP > 0 && !HasWork(F) && !F.bRepairing) { const FLevel* S = Stats(F); if (S) Capacity = FMath::Max(Capacity, static_cast<int64>(S->Capacity)); }
        int64 Used = FlingerUsed(*Battle);
        TArray<FString> Types; Y.Army.GetKeys(Types); Types.Sort();
        for (const FString& Type : Types)
        {
            const FMonsterDef* M = Monsters.Find(Type); if (!M || !IsOperationalMonster(Type)) continue;
            const FMonsterStats& S = MonsterStats(Y, *M);
            const int32 Count = FMath::Min3(FMath::Min(100, TroopLimit() - Battle->Deployed), Y.Army.FindRef(Type), static_cast<int32>(FMath::Max<int64>(0, Capacity - Used) / S.Housing));
            if (Count < 1) continue;
            FObject P = Obj(); P->SetStringField(TEXT("type"), Type); P->SetNumberField(TEXT("count"), Count);
            // Alternating edges provide different approaches without granting bots privileged deployment positions.
            const bool bNorth = (GetTypeHash(Y.Id) + Y.Decisions) % 2 == 0;
            P->SetNumberField(TEXT("x"), YardMinX(*TargetYard) + (bNorth ? YardWidth(*TargetYard) / 2 : 1)); P->SetNumberField(TEXT("y"), YardMinY(*TargetYard) + (bNorth ? 1 : YardHeight(*TargetYard) / 2));
            if (Command(TEXT("deploy"), P)) Used += static_cast<int64>(Count) * S.Housing;
        }
    };
    if (FBattle* B = Battles.Find(Y.Id))
    {
        if (B->Deployed == 0) DeployArmy();
        return;
    }
    if (UnderAttack(Y.Id)) return;
    MigrateBotLayout(Y);
    ++Y.Decisions;
    if (BotMapAction(Y)) return;
    if (Y.Decisions % 9 == 0 && BotLayoutMove(Y)) return;
    // Collection uses the same capped server command as players, never an AI income bonus.
    Command(TEXT("collect"), Obj());
    for (const FBuilding& B : Y.Buildings)
    {
        const FLevel* L = Stats(B); if (L && B.HP < L->HP && !B.bRepairing)
        { FObject P = Obj(); P->SetStringField(TEXT("building"), B.Id); Command(TEXT("repair"), P); return; }
    }
    // Rivals use the same priced, capacity-checked worker/shop commands. One
    // bounded action at a time preserves their construction and combat budget.
    if (Y.Decisions % 12 == 0 && StoreReason(Y).IsEmpty())
    {
        const bool Expand = Y.ExpansionLevel < 6 && Y.Decisions % 36 == 0;
        const bool Pack = Y.PackingLevel < 10 && Y.Decisions % 24 == 0;
        FObject P = Obj();
        if (Expand) { P->SetStringField(TEXT("code"), TEXT("ENL")); P->SetNumberField(TEXT("expectedLevel"), Y.ExpansionLevel); }
        else if (Pack) { P->SetStringField(TEXT("code"), TEXT("BIP")); P->SetNumberField(TEXT("expectedLevel"), Y.PackingLevel); }
        else if (Y.PurchasedWorkers < 4) { P->SetStringField(TEXT("code"), TEXT("BEW")); P->SetNumberField(TEXT("expectedLevel"), Y.PurchasedWorkers); }
        if (P->HasField(TEXT("code")) && Command(TEXT("shopBuy"), P)) return;
    }
    if (Y.Decisions % 10 == 0 && AvailableWorkers(Y) > 0)
    {
        for (const FYard::FMushroom& M : Y.Mushrooms) if (!M.bPicking)
        { FObject P = Obj(); P->SetStringField(TEXT("id"), M.Id); if (Command(TEXT("clearmushroom"), P)) return; }
    }
    int64 ReadyArmyHousing = 0;
    for (const TPair<FString, int32>& P : Y.Army) { const FMonsterDef* M = Monsters.Find(P.Key); if (M) ReadyArmyHousing += static_cast<int64>(P.Value) * MonsterStats(Y, *M).Housing; }
    if (Y.Decisions % 4 == 0)
    {
        for (const FBuilding& Bunker : Y.Buildings) if (Bunker.Type == 22 && BunkerManageReason(Y, Bunker).IsEmpty())
        {
            TArray<FString> Types; Y.Army.GetKeys(Types); Types.Sort();
            for (const FString& Type : Types) if (BYMBunkerRules::IsAllowedSpecies(Type) && Y.Army.FindRef(Type) > 1)
            {
                const FMonsterDef* Definition = Monsters.Find(Type); if (!Definition) continue;
                const int32 Space = BYMBunkerRules::Capacity(Bunker.Level) - BunkerUsed(Y, Bunker);
                const int32 Count = FMath::Min3(5, Y.Army.FindRef(Type) / 2, Space / MonsterStats(Y, *Definition).Housing);
                if (Count <= 0) continue;
                FObject Params = Obj(); Params->SetStringField(TEXT("building"), Bunker.Id); Params->SetStringField(TEXT("type"), Type); Params->SetNumberField(TEXT("count"), Count);
                if (Command(TEXT("bunkerTransfer"), Params)) return;
            }
        }
    }
    if (ReadyArmyHousing >= 100 && Y.Decisions % 3 == 0 && Battles.Num() < BattleLimit())
    {
        const int32 Start = YardIds.Num() > 0 ? static_cast<int32>((GetTypeHash(Y.Id) + static_cast<uint32>(Y.Decisions) * 104729U) % static_cast<uint32>(YardIds.Num())) : 0;
        for (int32 I = 0; I < FMath::Min(24, YardIds.Num()); ++I)
        {
            const FString Target = YardIds[(Start + I * 37) % YardIds.Num()]; const FYard* D = Yards.Find(Target);
            if (!D || D->Id == Y.Id || Busy(D->Id) || D->ShieldUntil > Now) continue;
            FObject P = Obj(); P->SetStringField(TEXT("target"), D->Id);
            if (Command(TEXT("invade"), P))
            {
                // Deploy immediately rather than spend the visible battle countdown
                // waiting for this bot's next round-robin decision.
                DeployArmy(); return;
            }
        }
    }
    if ((Y.Decisions % 3) == 1)
    {
        // Rivals buy discovery and research through the same Putty, level and
        // per-Academy occupancy checks as human players. No free AI unlocks.
        TArray<FString> Types; Monsters.GetKeys(Types);
        Types.Sort([this](const FString& A, const FString& B)
        { const FMonsterDef& DA = Monsters.FindChecked(A); const FMonsterDef& DB = Monsters.FindChecked(B); return DA.UnlockPutty == DB.UnlockPutty ? A < B : DA.UnlockPutty < DB.UnlockPutty; });
        for (const FString& Type : Types)
        {
            const FMonsterDef& M = Monsters.FindChecked(Type);
            if (!ResearchReason(Y, M).IsEmpty()) continue;
            FObject P = Obj(); P->SetStringField(TEXT("type"), Type); if (Command(TEXT("research"), P)) return;
        }
        for (const FString& Type : Types)
        {
            const FMonsterDef& M = Monsters.FindChecked(Type);
            if (!UnlockReason(Y, M).IsEmpty()) continue;
            FObject P = Obj(); P->SetStringField(TEXT("type"), Type); if (Command(TEXT("unlock"), P)) return;
        }
    }
    if (UsedHousing(Y) < Housing(Y) && QueuedMonsters(Y) < 12)
    {
        TArray<FString> Types; Monsters.GetKeys(Types); Types.Sort();
        const int32 Offset = Types.Num() > 0 ? static_cast<int32>((GetTypeHash(Y.Id) + Y.Decisions) % Types.Num()) : 0;
        for (int32 I = 0; I < Types.Num(); ++I)
        {
            const FMonsterDef& M = Monsters.FindChecked(Types[(Offset + I) % Types.Num()]);
            const int32 Count = FMath::Min(5, MaxTrainCount(Y, M));
            if (Count > 0) { FObject P = Obj(); P->SetStringField(TEXT("type"), M.Id); P->SetNumberField(TEXT("count"), Count); if (Command(TEXT("train"), P)) return; }
        }
    }
    // Upgrade production, storage, capacity and defenses. The same prerequisites and resource checks apply.
    const int32 Priorities[] = {14, 4, 1, 2, 3, 6, 15, 20, 13, 8, 26, 5, 22, 12, 11};
    const int32 Offset = Y.Decisions % UE_ARRAY_COUNT(Priorities);
    for (int32 I = 0; I < UE_ARRAY_COUNT(Priorities); ++I)
    {
        const int32 Type = Priorities[(Offset + I) % UE_ARRAY_COUNT(Priorities)];
        for (const FBuilding& B : Y.Buildings) if (B.Type == Type)
        { FObject P = Obj(); P->SetStringField(TEXT("building"), B.Id); if (Command(TEXT("upgrade"), P)) return; }
    }
    // Map Room is an original prerequisite for Flinger level 2. Rivals must be
    // able to construct it through the same cost/quantity/requirement checks.
    for (const int32 Type : {12, 11, 26, 20, 22, 21, 1, 2, 3, 4, 6, 15, 23, 25})
    {
        const FBuildingDef* D = Buildings.Find(Type); int32 X = 0, PosY = 0;
        if (!D || !FreeSpot(Y, D->Cells, X, PosY, Type)) continue;
        FObject P = Obj(); P->SetNumberField(TEXT("type"), Type); P->SetNumberField(TEXT("x"), X); P->SetNumberField(TEXT("y"), PosY);
        if (Command(TEXT("build"), P)) return;
    }
}

FBYMSimulation::FBYMSimulation() : Impl(MakeUnique<FImpl>()) {}
FBYMSimulation::~FBYMSimulation() = default;

bool FBYMSimulation::Initialize(const TSharedPtr<FJsonObject>& Saved, FString& Error)
{
    Impl = MakeUnique<FImpl>(); Impl->Now = static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp());
    if (!Impl->LoadCatalog(Error) || !Impl->LoadEconomyRules(Error) || !Impl->LoadMapRules(Error)) return false;
    if (Saved.IsValid() && !Impl->LoadSave(Saved, Error)) return false;
    const int32 DesiredBots = FMath::Clamp(GetDefault<UBYMSettings>()->RivalBotCount, 0, 10000);
    static const TCHAR* First[] = {TEXT("Bramble"), TEXT("Moss"), TEXT("Crag"), TEXT("Thorn"), TEXT("Boulder"), TEXT("Goo"), TEXT("Bog"), TEXT("Rubble"), TEXT("Feral"), TEXT("Ember"), TEXT("Hollow"), TEXT("Iron"), TEXT("Twilight"), TEXT("Mushroom"), TEXT("Grim"), TEXT("Wild")};
    static const TCHAR* Last[] = {TEXT("Fangs"), TEXT("Keepers"), TEXT("Raiders"), TEXT("Maws"), TEXT("Claws"), TEXT("Stompers"), TEXT("Horde"), TEXT("Sentinels"), TEXT("Burrow"), TEXT("Hive"), TEXT("Wardens"), TEXT("Howlers"), TEXT("Guard"), TEXT("Brawlers"), TEXT("Wanderers"), TEXT("Hunters")};
    int32 NextBot = 1;
    while (Impl->BotIds.Num() < DesiredBots)
    {
        if (Impl->YardIds.Num() >= 100000) { Error = TEXT("The world has reached the maximum of 100,000 combined player and rival yards."); return false; }
        const FString Id = FString::Printf(TEXT("bot-%06d"), NextBot); ++NextBot; if (Impl->Yards.Contains(Id)) continue;
        const int32 Index = NextBot - 2;
        const FString Name = FString::Printf(TEXT("%s %s %03d"), First[Index % UE_ARRAY_COUNT(First)], Last[(Index / UE_ARRAY_COUNT(First)) % UE_ARRAY_COUNT(Last)], Index + 1);
        FYard Y = Impl->NewYard(Id, Name, true);
        if (Y.Buildings.Num() != 11) { Error = TEXT("The configured yard is too small for the complete starter layout. Set Yard Cells to at least 40."); return false; }
        Impl->BotIds.Add(Id); Impl->YardIds.Add(Id); Impl->Yards.Add(Id, MoveTemp(Y));
    }
    Impl->BotIds.Sort(); Impl->YardIds.Sort();
    for (const FString& Owner : Impl->YardIds) if (!Impl->AddMapPlayer(Owner, Error)) return false;
    for (const FString& Bot : Impl->BotIds) if (FYard* Yard = Impl->Yards.Find(Bot)) Impl->MigrateBotLayout(*Yard);
    Impl->bInitialized = true; Error.Reset(); return true;
}

TSharedPtr<FJsonObject> FBYMSimulation::Serialize() const
{
    FObject O = Obj(); O->SetNumberField(TEXT("schema"), 5); FArray Yards, Battles;
    TArray<FString> Keys; Impl->Yards.GetKeys(Keys); Keys.Sort();
    for (const FString& K : Keys) if (!Impl->IsMapSite(K)) Yards.Add(MakeShared<FJsonValueObject>(Impl->YardJSON(Impl->Yards.FindChecked(K), true)));
    Keys.Reset(); Impl->Battles.GetKeys(Keys); Keys.Sort();
    for (const FString& K : Keys) Battles.Add(MakeShared<FJsonValueObject>(Impl->BattleJSON(Impl->Battles.FindChecked(K), true)));
    O->SetArrayField(TEXT("yards"), Yards); O->SetArrayField(TEXT("battles"), Battles); O->SetObjectField(TEXT("worldMap"), Impl->SaveMap()); return O;
}

bool FBYMSimulation::AddPlayer(const FString& Id, const FString& DisplayName, FString& Error)
{
    if (!Impl->bInitialized || Id.IsEmpty() || Id.Len() > 80 || DisplayName.IsEmpty() || DisplayName.Len() > 64 || Impl->Yards.Contains(Id) || Impl->YardIds.Num() >= 100000)
    { Error = TEXT("Player yard identity is invalid or already exists."); return false; }
    FYard Y = Impl->NewYard(Id, DisplayName, false);
    if (Y.Buildings.Num() != 11) { Error = TEXT("The configured yard is too small for the starter layout."); return false; }
    Impl->YardIds.Add(Id); Impl->Yards.Add(Id, MoveTemp(Y));
    if (!Impl->AddMapPlayer(Id, Error)) { Impl->Yards.Remove(Id); Impl->YardIds.Remove(Id); return false; }
    Error.Reset(); return true;
}

void FBYMSimulation::RemovePlayer(const FString& Id)
{
    // Used only to roll back an uncommitted registration; disconnects must never remove game state.
    const FYard* Y = Impl->Yards.Find(Id);
    if (Y && !Y->bBot && !Impl->Busy(Id)) { Impl->RemoveMapPlayer(Id); Impl->Yards.Remove(Id); Impl->YardIds.Remove(Id); }
}

void FBYMSimulation::SetPlayerOnline(const FString& Id, bool bOnline)
{ if (bOnline && Impl->Yards.Contains(Id) && !Impl->IsMapSite(Id)) Impl->OnlinePlayers.Add(Id); else Impl->OnlinePlayers.Remove(Id); }

bool FBYMSimulation::HasYard(const FString& Id) const { return Impl->Yards.Contains(Id); }
bool FBYMSimulation::HasActiveBattles() const { return !Impl->Battles.IsEmpty(); }

void FBYMSimulation::Tick(double NowSeconds, float DeltaSeconds)
{
    if (!Impl->bInitialized || !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0) return;
    // Host-clock rollback cannot rewind construction, shields, or training.
    Impl->Now = FMath::Max(Impl->Now, NowSeconds);
    Impl->PruneCompletedBattles();
    Impl->CombatAccumulator = FMath::Min(Impl->CombatAccumulator + static_cast<double>(DeltaSeconds), 2.0);
    const double CombatStep = FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->CombatSimulationStepSeconds), 0.05, 0.1);
    while (Impl->CombatAccumulator + 0.000001 >= CombatStep)
    {
        TArray<FString> Keys; Impl->Battles.GetKeys(Keys);
        for (const FString& K : Keys)
        {
            FBattle* B = Impl->Battles.Find(K); if (!B) continue;
            // The original attack uses its displayed battle countdown, not a
            // separate hidden 30-second first-deployment deadline.
            if (B->EndsAt <= Impl->Now) { Impl->EndBattle(K, false, B->Deployed > 0 ? TEXT("Defense held until the time limit") : TEXT("Invasion timer expired before deployment"), B->Deployed > 0); continue; }
            Impl->TickBattle(*B, CombatStep);
            const FYard* Defender = Impl->Yards.Find(B->Defender); if (!Defender) { Impl->EndBattle(K, false, TEXT("Defending yard unavailable"), false); continue; }
            bool bTownDestroyed = false; int32 AliveBuildings = 0, AliveTroops = 0;
            for (const FBuilding& BD : Defender->Buildings) if (Impl->IsCombatObjective(BD)) { if (BD.HP > 0) ++AliveBuildings; if (BD.Type == 14 && BD.HP <= 0) bTownDestroyed = true; }
            for (const FTroop& T : B->Troops) if (T.HP > 0) ++AliveTroops;
            const bool bHalfDestroyed = B->InitialBuildings > 0 && (B->InitialBuildings - AliveBuildings) * 2 >= B->InitialBuildings;
            const bool bMapVictory = !B->MapCellId.IsEmpty() && Impl->MapDamagePercent(*Defender) >= 90.;
            if (B->Deployed > 0 && (bMapVictory || (B->MapCellId.IsEmpty() && (bTownDestroyed || bHalfDestroyed)))) Impl->EndBattle(K, true, bMapVictory ? TEXT("World Map objective reached: at least 90% building damage") : (bTownDestroyed ? TEXT("Town Hall destroyed") : TEXT("Half the defending buildings destroyed")));
            // ATTACK.Tick also considers healthy reserves before ending an
            // attack. A defeated first wave must not discard a deployable army.
            else if (B->Deployed > 0 && AliveTroops == 0 && !Impl->HasDeployableReserves(*B))
                Impl->EndBattle(K, false, TEXT("All invading monsters defeated and no deployable reserves remain"));
        }
        Impl->CombatAccumulator = FMath::Max(0.0, Impl->CombatAccumulator - CombatStep);
    }
    // Accrual is timestamp based. A bounded batch avoids a frame spike from 1,000 rival yards.
    const int32 AccrualBatch = FMath::Min(32, Impl->YardIds.Num());
    for (int32 I = 0; I < AccrualBatch; ++I)
    {
        if (Impl->EconomyCursor >= Impl->YardIds.Num()) Impl->EconomyCursor = 0;
        if (FYard* Y = Impl->Yards.Find(Impl->YardIds[Impl->EconomyCursor++])) Impl->Advance(*Y);
    }
    Impl->BotAccumulator += DeltaSeconds;
    const double Step = FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->SimulationStepSeconds), 0.1, 60.0);
    const int32 ActiveBots = FMath::Min(Impl->BotIds.Num(), FMath::Clamp(GetDefault<UBYMSettings>()->RivalBotCount, 0, 10000));
    if (Impl->BotAccumulator >= Step && ActiveBots > 0)
    {
        Impl->BotAccumulator = FMath::Fmod(Impl->BotAccumulator, Step);
        const int32 Budget = FMath::Min(FMath::Clamp(GetDefault<UBYMSettings>()->BotActionsPerTick, 1, 100), ActiveBots);
        for (int32 I = 0; I < Budget; ++I)
        {
            if (Impl->BotCursor >= ActiveBots) Impl->BotCursor = 0;
            FYard* Y = Impl->Yards.Find(Impl->BotIds[Impl->BotCursor++]);
            if (Y && Impl->Now >= Y->NextDecision)
            { Y->NextDecision = Impl->Now + FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->BotDecisionSeconds), 1.0, 86400.0); Impl->BotAction(*Y); }
        }
    }
}

FString FBYMSimulation::Execute(const FString& Id, const FString& Operation, const FString& Payload)
{
    if (!Impl->bInitialized) return Reply(false, TEXT("World simulation is not running."));
    if (Operation.Len() > 32 || Payload.Len() > FMath::Clamp(GetDefault<UBYMSettings>()->MaxCommandBytes, 256, 65536)) return Reply(false, TEXT("Command is too large."));
    FObject Params; if (!Decode(Payload.IsEmpty() ? TEXT("{}") : Payload, Params)) return Reply(false, TEXT("Command is not a JSON object."));
    return Impl->Execute(Id, Operation, Params);
}

FString FBYMSimulation::Snapshot(const FString& Id, const FString& ViewedYardId) const
{
    const FYard* Home = Impl->Yards.Find(Id); if (!Home) return Reply(false, TEXT("No authenticated yard."));
    FString View = ViewedYardId.IsEmpty() ? Id : ViewedYardId;
    // During an invasion every click must refer to the same defending yard that
    // deployment will affect; an inspect/home request cannot change that target.
    if (const FBattle* B = Impl->Battles.Find(Id)) View = B->Defender;
    const FMapCell* ViewedCell = Impl->MapCellForYard(View);
    if (ViewedCell && ViewedCell->Kind != 101)
    {
        if (!Impl->Yards.Contains(View) && !Impl->MapGate(*Home).IsEmpty()) return Reply(false, Impl->MapGate(*Home));
        FString Error; const FString CellId = ViewedCell->Id;
        if (!Impl->EnsureMapSite(CellId, Error)) return Reply(false, Error);
        // A reconstructed lazy site can reallocate the yard map. Never retain a
        // previous home pointer or silently swap the viewed rival for own army.
        Home = Impl->Yards.Find(Id); if (!Home) return Reply(false, TEXT("No authenticated yard."));
    }
    const FYard* Yard = Impl->Yards.Find(View); if (!Yard) Yard = Home;
    FObject O = Obj(); O->SetBoolField(TEXT("ok"), true); O->SetStringField(TEXT("selfId"), Id);
    O->SetNumberField(TEXT("now"), Impl->Now); O->SetNumberField(TEXT("yardCells"), FMath::Max(Impl->YardWidth(*Yard), Impl->YardHeight(*Yard))); O->SetNumberField(TEXT("baseYardCells"), Impl->Cells());
    FObject Rules = Obj(); Rules->SetNumberField(TEXT("yardCells"), FMath::Max(Impl->YardWidth(*Yard), Impl->YardHeight(*Yard))); Rules->SetNumberField(TEXT("baseYardCells"), Impl->Cells()); Rules->SetNumberField(TEXT("cellSize"), FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->CellSize), 20.0, 1000.0));
    Rules->SetNumberField(TEXT("constructionTimeMultiplier"), ConstructionMultiplier()); Rules->SetNumberField(TEXT("trainingTimeMultiplier"), TrainingMultiplier());
    Rules->SetNumberField(TEXT("monsterProgressionTimeMultiplier"), ProgressionMultiplier());
    Rules->SetNumberField(TEXT("resourceProductionMultiplier"), ProductionMultiplier()); Rules->SetNumberField(TEXT("combatDamageMultiplier"), DamageMultiplier());
    Rules->SetNumberField(TEXT("battleSnapshotIntervalSeconds"), FMath::Clamp(static_cast<double>(GetDefault<UBYMSettings>()->BattleSnapshotIntervalSeconds), 0.1, 1.0));
    Rules->SetNumberField(TEXT("maximumBattleSeconds"), BattleSeconds()); Rules->SetNumberField(TEXT("maximumTroopsPerBattle"), TroopLimit());
    Rules->SetBoolField(TEXT("mapRoomParticipant"), Impl->MapParticipant(Id));
    Rules->SetBoolField(TEXT("mapRoomAvailable"), Impl->MapGate(*Home).IsEmpty());
    O->SetObjectField(TEXT("rules"), Rules);
    FObject Summary = Impl->YardJSON(*Home, false); Summary->RemoveField(TEXT("buildings"));
    O->SetObjectField(TEXT("home"), Summary);
    FObject ViewObject = Impl->YardJSON(*Yard, false, Yard->Id == Id);
    if (const FMapCell* Cell = Impl->MapCellForYard(Yard->Id))
    {
        ViewObject->SetObjectField(TEXT("mapCell"), Impl->MapCellJSON(*Home, *Cell, true));
        ViewObject->SetStringField(TEXT("mapRelationship"), Cell->Owner == Id ? TEXT("self") : TEXT("enemy"));
        if (Cell->Kind != 101)
        {
            const bool bManage = Cell->Owner == Id && !Impl->Busy(Id) && !Impl->Busy(Yard->Id);
            for (const auto& Value : Array(ViewObject, TEXT("buildings")))
            {
                FObject Building = Value->AsObject(); const FBuilding* B = Impl->FindBuilding(*Yard, Str(Building, TEXT("id"))); if (!B) continue;
                const FLevel* L = Impl->Stats(*B);
                const bool bMove = bManage && B->HP > 0 && !B->bRepairing && B->Type != 138 && B->Type != 139 && B->Type != 140;
                Building->SetBoolField(TEXT("canMove"), bMove); Building->SetStringField(TEXT("moveReason"), bMove ? TEXT("") : TEXT("Move a living non-core building in your own idle territory."));
                Building->SetBoolField(TEXT("canRepair"), bManage && L && B->HP < L->HP && !B->bRepairing);
                Building->SetBoolField(TEXT("canUpgrade"), false); Building->SetBoolField(TEXT("upgradeAvailable"), false);
                Building->SetStringField(TEXT("upgradeReason"), TEXT("World Map territories retain their original fixed building levels."));
            }
        }
    }
    O->SetObjectField(TEXT("yard"), ViewObject);
    O->SetObjectField(TEXT("buildOptions"), Impl->BuildOptions(*Home));
    O->SetObjectField(TEXT("monsterOptions"), Impl->MonsterOptions(*Home));
    if (!Home->Log.IsEmpty()) O->SetObjectField(TEXT("lastBattle"), Home->Log[0]);
    for (const TPair<FString, FBattle>& P : Impl->Battles)
    {
        const FMapCell* Cell = Impl->MapCells.Find(P.Value.MapCellId);
        const bool bDefender = P.Value.Defender == Id || (Cell && Cell->Owner == Id && Yard->Id == P.Value.Defender);
        if ((P.Value.Attacker == Id || bDefender) && (Yard->Id == P.Value.Defender || Yard->Id == Id))
        {
            FObject BattleView = Impl->BattleJSON(P.Value, false);
            if (P.Value.Attacker != Id)
            {
                BattleView->SetBoolField(TEXT("canDeploy"), false);
                for (const TCHAR* Key : {TEXT("flingerCapacity"), TEXT("flingerUsed"), TEXT("flingerRemaining"), TEXT("remainingTroopSlots")}) BattleView->RemoveField(Key);
            }
            ViewObject->SetArrayField(TEXT("residentRoster"), FArray());
            O->SetObjectField(TEXT("battle"), BattleView); break;
        }
    }
    const FObject* Completed = Impl->CompletedBattles.Find(Yard->Id);
    if (Completed && Number(*Completed, TEXT("finishedAt")) + 3.0 >= Impl->Now)
        O->SetObjectField(TEXT("completedBattle"), *Completed);
    FArray Buildable, Trainable; TArray<int32> BuildingKeys; Impl->Buildings.GetKeys(BuildingKeys); BuildingKeys.Sort();
    for (const int32 Key : BuildingKeys) if (IsOperationalBuilding(Key) && Impl->Buildings.FindChecked(Key).bBuildable) Buildable.Add(MakeShared<FJsonValueNumber>(Key));
    TArray<FString> MonsterKeys; Impl->Monsters.GetKeys(MonsterKeys); MonsterKeys.Sort();
    for (const FString& Key : MonsterKeys) if (IsOperationalMonster(Key)) Trainable.Add(MakeShared<FJsonValueString>(Key));
    O->SetArrayField(TEXT("buildableTypes"), Buildable); O->SetArrayField(TEXT("trainableTypes"), Trainable);
    O->SetStringField(TEXT("battleObjective"), Impl->MapParticipant(Id) ? TEXT("Deal at least 90% non-wall building damage before the timer ends. Resource outposts, strongholds and fortifications are captured; main backyards remain owned.") : TEXT("Destroy the Town Hall or at least half the defending buildings before the timer ends."));
    return Encode(O);
}

FString FBYMSimulation::Ladder(int32 Page) const
{
    TArray<const FYard*> Ranked; Ranked.Reserve(Impl->Yards.Num());
    for (const TPair<FString, FYard>& P : Impl->Yards) if (!Impl->IsMapSite(P.Key)) Ranked.Add(&P.Value);
    Ranked.Sort([](const FYard& A, const FYard& B)
    {
        if (A.RP != B.RP) return A.RP > B.RP;
        if (A.Wins != B.Wins) return A.Wins > B.Wins;
        const int32 NameOrder = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
        return NameOrder != 0 ? NameOrder < 0 : A.Id < B.Id;
    });
    const int32 TotalPages = FMath::Max(1, FMath::DivideAndRoundUp(Ranked.Num(), 50));
    Page = FMath::Clamp(Page, 0, TotalPages - 1); FArray Entries;
    for (int32 I = Page * 50; I < FMath::Min((Page + 1) * 50, Ranked.Num()); ++I)
    {
        const FYard& Y = *Ranked[I]; FObject E = Obj(); E->SetStringField(TEXT("id"), Y.Id); E->SetStringField(TEXT("name"), Y.Name);
        E->SetNumberField(TEXT("rp"), static_cast<double>(Y.RP)); E->SetNumberField(TEXT("rank"), I + 1); E->SetBoolField(TEXT("bot"), Y.bBot); E->SetBoolField(TEXT("activeBot"), Y.bBot && Impl->ActiveBot(Y.Id));
        const BYMExperience::FProgress Experience = BYMExperience::Progress(Y.EarnedExperience, Y.BaseValue);
        E->SetNumberField(TEXT("level"), Experience.Level); E->SetNumberField(TEXT("experiencePoints"), static_cast<double>(Experience.Points));
        E->SetNumberField(TEXT("shieldUntil"), Y.ShieldUntil); E->SetNumberField(TEXT("wins"), Y.Wins); E->SetNumberField(TEXT("losses"), Y.Losses);
        Entries.Add(MakeShared<FJsonValueObject>(E));
    }
    FObject O = Obj(); O->SetBoolField(TEXT("ok"), true); O->SetNumberField(TEXT("page"), Page); O->SetNumberField(TEXT("total"), Ranked.Num());
    O->SetNumberField(TEXT("totalPages"), TotalPages); O->SetArrayField(TEXT("entries"), Entries); return Encode(O);
}

#include "BYMSimulationMap.inl"
#include "BYMSimulationMapPersistence.inl"
#include "BYMSimulationMapCombat.inl"
#include "BYMSimulationBotLayouts.inl"

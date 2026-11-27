#include "BYMBotLayout.h"

namespace BYMBotLayout
{
    namespace
    {
        uint32 Mix(uint32 Value)
        {
            Value ^= Value >> 16; Value *= 0x7feb352dU;
            Value ^= Value >> 15; Value *= 0x846ca68bU;
            return Value ^ (Value >> 16);
        }

        uint32 IdentitySeed(const FString& Identity)
        {
            // Bot IDs are ASCII. Explicit FNV-1a avoids engine/platform hash
            // differences and needs no random-stream state in the world save.
            uint32 Seed = 2166136261U;
            for (TCHAR Character : Identity) { Seed ^= static_cast<uint32>(Character); Seed *= 16777619U; }
            return Mix(Seed);
        }

        int32 Role(int32 Type)
        {
            if (Type == 14) return 0;                         // town centre
            if (Type >= 1 && Type <= 4) return 1;             // collectors
            if (Type == 15) return 2;                         // housing
            if (Type == 13) return 3;                         // hatchery
            if (Type == 20 || Type == 21 || Type == 22 || Type == 23 || Type == 25) return 4;
            if (Type == 6) return 5;                          // storage
            if (Type == 5) return 7;                          // flinger
            return 6;                                        // progression/shop/map support
        }
    }

    FPlan MakePlan(const FString& BotId, int32 BuildingType, int32 Occurrence,
        const FIntRect& Bounds, int32 Footprint)
    {
        FPlan Result;
        if (Footprint < 1 || Bounds.Width() > 256 || Bounds.Height() > 256
            || Bounds.Width() < Footprint + 8 || Bounds.Height() < Footprint + 8) return Result;
        const uint32 Identity = IdentitySeed(BotId);
        Result.Style = static_cast<int32>(Identity % 6U);
        Result.Seed = Mix(Identity ^ static_cast<uint32>(BuildingType) * 0x9e3779b9U
            ^ static_cast<uint32>(FMath::Max(0, Occurrence)) * 0x85ebca6bU);
        // Coordinates are thousandths of the available placement rectangle.
        // Core, split flanks, four districts, diagonal, outer ring and offset
        // strongpoint styles deliberately change relative building positions.
        static const int32 Anchors[6][8][2] = {
            {{500,500},{250,250},{650,250},{250,650},{550,700},{700,500},{750,750},{450,100}},
            {{250,500},{750,300},{200,150},{250,800},{550,500},{600,800},{800,700},{650,100}},
            {{450,400},{150,150},{800,150},{150,800},{800,700},{500,750},{750,450},{450,100}},
            {{300,250},{700,650},{750,200},{200,700},{450,650},{700,450},{450,850},{150,100}},
            {{500,500},{200,500},{700,800},{200,800},{850,450},{750,150},{350,100},{100,200}},
            {{700,650},{250,700},{750,100},{150,200},{500,400},{500,800},{850,400},{450,100}}
        };
        const int32 BuildingRole = Role(BuildingType);
        int32 U = Anchors[Result.Style][BuildingRole][0];
        int32 V = Anchors[Result.Style][BuildingRole][1];
        if (BuildingRole == 1)
        {
            const int32 Collector = BuildingType - 1;
            U += (Collector % 2) * 170 - 85;
            V += (Collector / 2) * 170 - 85;
        }
        if (BuildingRole == 4)
        {
            U += (BuildingType % 3) * 100 - 100;
            V += ((BuildingType / 3) % 3) * 100 - 100;
        }
        U += static_cast<int32>(Result.Seed % 171U) - 85;
        V += static_cast<int32>((Result.Seed >> 8) % 171U) - 85;
        if (Identity & 8U) U = 1000 - U;
        if (Identity & 16U) V = 1000 - V;
        U = FMath::Clamp(U, 0, 1000); V = FMath::Clamp(V, 0, 1000);
        Result.Anchor = FIntPoint(Bounds.Min.X + 4 + (Bounds.Width() - Footprint - 8) * U / 1000,
            Bounds.Min.Y + 4 + (Bounds.Height() - Footprint - 8) * V / 1000);
        Result.WeightX = 2 + static_cast<int32>((Identity >> 5) % 3U);
        Result.WeightY = 2 + static_cast<int32>((Identity >> 7) % 3U);
        Result.bValid = true;
        return Result;
    }

    uint64 RankCandidate(const FPlan& Plan, int32 X, int32 Y)
    {
        if (!Plan.bValid) return TNumericLimits<uint64>::Max();
        // The simulation limits candidates to a 256-cell yard. Bound arbitrary
        // callers too so corrupted coordinates cannot overflow score arithmetic.
        const int64 DX = FMath::Clamp<int64>(static_cast<int64>(X) - Plan.Anchor.X, -65535, 65535);
        const int64 DY = FMath::Clamp<int64>(static_cast<int64>(Y) - Plan.Anchor.Y, -65535, 65535);
        const uint64 Distance = static_cast<uint64>(DX * DX * Plan.WeightX + DY * DY * Plan.WeightY);
        const uint32 Tie = Mix(Plan.Seed ^ static_cast<uint32>(X) * 0x9e3779b9U ^ static_cast<uint32>(Y) * 0x85ebca6bU);
        return Distance * 65536ULL + static_cast<uint64>(Tie & 65535U);
    }
}

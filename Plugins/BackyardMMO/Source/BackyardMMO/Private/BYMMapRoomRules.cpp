#include "BYMMapRoomRules.h"

#include <cmath>

namespace BYMMapRoomRules
{
    bool IsValidCell(FIntPoint Cell)
    {
        return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Width && Cell.Y < Height;
    }

    int32 HexDistance(FIntPoint A, FIntPoint B)
    {
        // getHexNeighborOffsets.ts: odd-r offset -> cube coordinates.
        const int64 AQ = static_cast<int64>(A.X) - (static_cast<int64>(A.Y) - (A.Y & 1)) / 2;
        const int64 BQ = static_cast<int64>(B.X) - (static_cast<int64>(B.Y) - (B.Y & 1)) / 2;
        const int64 DQ = AQ - BQ, DR = static_cast<int64>(A.Y) - B.Y, DS = -DQ - DR;
        const int64 Distance = FMath::Max(FMath::Abs(DQ), FMath::Max(FMath::Abs(DR), FMath::Abs(DS)));
        return static_cast<int32>(FMath::Min<int64>(Distance, MAX_int32));
    }

    TArray<FIntPoint> Neighbors(FIntPoint Cell)
    {
        const int32 Shift = Cell.Y & 1;
        const int32 DX[6] = {1, -1, Shift, Shift - 1, Shift, Shift - 1};
        const int32 DY[6] = {0, 0, -1, -1, 1, 1};
        TArray<FIntPoint> Result;
        Result.Reserve(6);
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const int64 X = static_cast<int64>(Cell.X) + DX[Index];
            const int64 Y = static_cast<int64>(Cell.Y) + DY[Index];
            if (X >= MIN_int32 && X <= MAX_int32 && Y >= MIN_int32 && Y <= MAX_int32)
                Result.Add(FIntPoint(static_cast<int32>(X), static_cast<int32>(Y)));
        }
        return Result;
    }

    int32 Range(int32 Kind, int32 Level)
    {
        if (Kind == Player) return 10;
        if (Kind == Resource && Level >= 10 && Level <= 50 && Level % 10 == 0) return Level / 10 + 1;
        if (Kind == Stronghold && Level >= 30 && Level <= 50 && Level % 10 == 0) return Level / 2 - 5;
        return 0;
    }

    int64 ResourceRate(int32 Level)
    {
        constexpr int64 Values[] = {1, 2, 5, 11, 23};
        return Level >= 10 && Level <= 50 && Level % 10 == 0 ? Values[Level / 10 - 1] : 0;
    }

    int64 ResourceCapacity(int32 Level)
    {
        constexpr int64 Values[] = {175000, 350000, 750000, 2000000, 4000000};
        return Level >= 10 && Level <= 50 && Level % 10 == 0 ? Values[Level / 10 - 1] : 0;
    }

    int32 StrongholdBonus(int32 Level)
    {
        return Level >= 30 && Level <= 50 && Level % 10 == 0 ? Level - 20 : 0;
    }

    int32 DamageReduction(int32 DefenderCount)
    {
        constexpr int32 Values[] = {0, 7, 13, 23, 35, 50, 75};
        return Values[FMath::Clamp(DefenderCount, 0, 6)];
    }

    int64 AttackCost(int32 DistanceBeyondOwnedRange, int32 TownHallLevel)
    {
        constexpr int64 Values[] = {0, 100, 200, 400, 800, 1600, 4000, 10000, 25000, 75000, 225000};
        return static_cast<int64>(FMath::Max(0, DistanceBeyondOwnedRange)) * Values[FMath::Clamp(TownHallLevel, 0, 10)];
    }

    double CapturePercent(double RemainingHP, double MaximumHP)
    {
        if (!FMath::IsFinite(RemainingHP) || !FMath::IsFinite(MaximumHP) || MaximumHP <= 0) return 0;
        const double Ratio = FMath::Clamp(RemainingHP / MaximumHP, 0.0, 1.0);
        return std::floor(100.0 - 100.0 * Ratio);
    }
}

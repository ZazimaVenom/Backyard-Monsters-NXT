#pragma once

#include "CoreMinimal.h"

/** Source Map Room 3 arithmetic; ownership and admission remain host decisions. */
namespace BYMMapRoomRules
{
    constexpr int32 Width = 500;
    constexpr int32 Height = 500;
    constexpr int32 Player = 101;
    constexpr int32 Resource = 102;
    constexpr int32 Stronghold = 103;
    constexpr int32 Fortification = 104;
    constexpr int32 Tribe = 1;
    constexpr int32 CaptureThreshold = 90;

    BACKYARDMMO_API bool IsValidCell(FIntPoint Cell);
    /** Odd-row horizontal offset grid. Extreme input distances saturate int32. */
    BACKYARDMMO_API int32 HexDistance(FIntPoint A, FIntPoint B);
    /** E,W,NE,NW,SE,SW; includes off-map neighbors, excluding integer overflow. */
    BACKYARDMMO_API TArray<FIntPoint> Neighbors(FIntPoint Cell);
    BACKYARDMMO_API int32 Range(int32 Kind, int32 Level);
    BACKYARDMMO_API int64 ResourceRate(int32 Level);
    BACKYARDMMO_API int64 ResourceCapacity(int32 Level);
    BACKYARDMMO_API int32 StrongholdBonus(int32 Level);
    BACKYARDMMO_API int32 DamageReduction(int32 DefenderCount);
    /** Price for EACH of Twigs/Pebbles/Putty; Goo is zero. Source A/B factor 1. */
    BACKYARDMMO_API int64 AttackCost(int32 DistanceBeyondOwnedRange, int32 TownHallLevel);
    /** Integer-truncated source damage percentage. Caller excludes walls/new work. */
    BACKYARDMMO_API double CapturePercent(double RemainingHP, double MaximumHP);
}

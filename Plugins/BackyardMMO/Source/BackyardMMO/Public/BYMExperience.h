#pragma once

#include "CoreMinimal.h"

/** Active Flash player experience rules. Ranking points are a separate system. */
namespace BYMExperience
{
    // Every persisted integer remains exactly representable in the existing
    // JSON world snapshot. Arithmetic saturates rather than wrapping uint32.
    constexpr int64 MaximumPoints = 9007199254740991LL;

    struct FProgress
    {
        int32 Level = 1;
        int32 MaxLevel = 99;
        int64 Earned = 0, BaseValue = 0, Points = 0;
        int64 Lower = 0, Upper = 900, Needed = 900;
        double Fraction = 0.;
        bool bAtMaxLevel = false;
    };

    BACKYARDMMO_API bool IsValidSavedPoints(double Value);
    BACKYARDMMO_API int64 Add(int64 Current, int64 Award);
    BACKYARDMMO_API int32 ThresholdCount();
    BACKYARDMMO_API int64 Threshold(int32 Index);
    BACKYARDMMO_API FProgress Progress(int64 Earned, int64 BaseValue);

    /** ceil(sum of CURRENT completed-level resource costs and source times /10).
     * Aggregate first; do not ceil each building independently. Damage and an
     * ongoing upgrade do not remove a completed building's contribution.
     */
    BACKYARDMMO_API int64 RetainBaseValue(int64 PreviousHighWater, int64 CompletedCostTotal);
    /** Source costs/times only: configured speed modifiers do not change XP. */
    BACKYARDMMO_API int64 ConstructionReward(int64 SourceSeconds, int64 ResourceCost, bool bTownHall = false);
    /** The original Upgraded function reads the PREVIOUS completed-level row. */
    BACKYARDMMO_API int64 UpgradeReward(int64 PreviousSourceSeconds, int64 PreviousResourceCost);
    /** Call once per collector with the accepted bank transfer. */
    BACKYARDMMO_API int64 CollectorReward(int64 Accepted, bool bTutorial = false);
    BACKYARDMMO_API int64 LootReward(int64 AwardedLoot);
}

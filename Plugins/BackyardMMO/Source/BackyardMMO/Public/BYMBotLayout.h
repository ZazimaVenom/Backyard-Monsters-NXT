#pragma once

#include "CoreMinimal.h"
#include "Math/IntRect.h"

/** Deterministic rival preferences, not privileged placement. The simulation
 * must still call its ordinary footprint/obstacle and move/build validators.
 * No state, inventory, timers or building coordinates are mutated here.
 */
namespace BYMBotLayout
{
    struct FPlan
    {
        FIntPoint Anchor = FIntPoint(0, 0);
        uint32 Seed = 0;
        int32 Style = 0;
        int32 WeightX = 2;
        int32 WeightY = 2;
        bool bValid = false;
    };

    /** Six distinct district/core/flank arrangements, stable per bot identity.
     * Building role and occurrence choose a district; identity-specific offsets
     * and facing keep rivals distinct without granting extra buildings.
     */
    BACKYARDMMO_API FPlan MakePlan(const FString& BotId, int32 BuildingType,
        int32 Occurrence, const FIntRect& Bounds, int32 Footprint);

    /** Lowest score wins among positions already accepted by the host.
     * Score is independent of iteration order, time, platform hash and saves.
     */
    BACKYARDMMO_API uint64 RankCandidate(const FPlan& Plan, int32 X, int32 Y);
}

#pragma once

#include "CoreMinimal.h"

// Ordinary overworld, count-based bunkers. Map Room 3's individually injured
// garrisons are a separate system and must not be mixed into these rules.
namespace BYMBunkerRules
{
    constexpr double DoorFrameRate = 40.;
    constexpr int32 DoorFrames = 15;
    constexpr double ReleaseIntervalSeconds = 30. / 72.;
    constexpr double RetargetIntervalSeconds = 10. / 40.;
    constexpr double DefenseSearchRangePixels = 200.;
    constexpr double RetaliationRangePixels = 50.;
    constexpr double DefenderSpeedMultiplier = 1.5;
    constexpr double DefensiveEyeRaRadiusPixels = 90.;
    constexpr double DefensiveMeleeRangePixels = 30.;

    BACKYARDMMO_API bool IsAllowedSpecies(const FString& Species);
    // GLOBAL.changeNotMaproom3SpecificBuildings overrides the raw catalog.
    BACKYARDMMO_API int32 Capacity(int32 CompletedLevel);
    BACKYARDMMO_API double TriggerRangePixels(int32 CompletedLevel);
    // Floor once per selected species batch, then sum the batches. Saturates
    // safely for extreme helper inputs; the host also validates wallet limits.
    BACKYARDMMO_API int64 TransferCost(int64 CurrentGooCost, int32 Count);
    // Logical/cartesian distance, as returned by Targeting.getCreepsInRange.
    BACKYARDMMO_API double DefensiveEyeRaDamage(double BaseDamage, double DistancePixels);
    // Offensive Eye-Ra keeps its existing building blast. CreepBase.explode
    // uses a different, projected-distance rule against defensive creatures.
    BACKYARDMMO_API double OffensiveEyeRaDamageToDefender(double BaseDamage, double ProjectedDistancePixels, bool bPrimaryTarget);
}

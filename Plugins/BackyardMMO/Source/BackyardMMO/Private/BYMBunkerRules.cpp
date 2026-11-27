#include "BYMBunkerRules.h"

#include <cmath>
#include <limits>

namespace BYMBunkerRules
{
    bool IsAllowedSpecies(const FString& Species)
    {
        // MONSTERBUNKERPOPUP.BUNKERABLE_MONSTERS, ordinary overworld subset.
        // Existing C17 inventories may be garrisoned even though normal Locker
        // purchases of that source-blocked species remain unavailable.
        const TCHAR* Allowed[] = {TEXT("C1"), TEXT("C2"), TEXT("C3"), TEXT("C4"), TEXT("C5"),
            TEXT("C6"), TEXT("C7"), TEXT("C8"), TEXT("C9"), TEXT("C10"), TEXT("C11"),
            TEXT("C12"), TEXT("C13"), TEXT("C17")};
        for (const TCHAR* Id : Allowed) if (Species == Id) return true;
        return false;
    }

    int32 Capacity(int32 CompletedLevel)
    {
        // The raw YARD_PROPS values are the MR3 branch. GLOBAL.as:692 doubles
        // them for the ordinary main-yard branch selected at :734-748.
        constexpr int32 Values[] = {380, 450, 540, 660, 800};
        return CompletedLevel >= 1 && CompletedLevel <= 5 ? Values[CompletedLevel - 1] : 0;
    }

    double TriggerRangePixels(int32 CompletedLevel)
    {
        constexpr double Values[] = {300., 350., 400., 450., 500.};
        return CompletedLevel >= 1 && CompletedLevel <= 5 ? Values[CompletedLevel - 1] : 0.;
    }

    int64 TransferCost(int64 CurrentGooCost, int32 Count)
    {
        if (CurrentGooCost <= 0 || Count <= 0) return 0;
        // Equivalent to floor(Goo * Count / 2), without overflowing the
        // intermediate product or losing the odd-cost batch rounding rule.
        const int64 Limit = std::numeric_limits<int64>::max();
        const int64 Half = CurrentGooCost / 2;
        const int64 Odd = (CurrentGooCost % 2) * static_cast<int64>(Count) / 2;
        if (Half > (Limit - Odd) / Count) return Limit;
        return Half * Count + Odd;
    }

    double DefensiveEyeRaDamage(double BaseDamage, double DistancePixels)
    {
        if (!std::isfinite(BaseDamage) || !std::isfinite(DistancePixels)
            || BaseDamage <= 0. || DistancePixels < 0. || DistancePixels >= DefensiveEyeRaRadiusPixels) return 0.;
        // Targeting.DealLinearAEDamage truncates its distance and damage locals
        // before applying the 20-percent minimum. Range selection is strict.
        const double Distance = std::trunc(DistancePixels);
        const double Linear = std::trunc(BaseDamage / DefensiveEyeRaRadiusPixels * (DefensiveEyeRaRadiusPixels - Distance));
        const double Minimum = std::trunc(BaseDamage / 5.);
        return Linear > Minimum ? Linear : Minimum;
    }

    double OffensiveEyeRaDamageToDefender(double BaseDamage, double ProjectedDistancePixels, bool bPrimaryTarget)
    {
        if (!std::isfinite(BaseDamage) || !std::isfinite(ProjectedDistancePixels)
            || BaseDamage <= 0. || ProjectedDistancePixels < 0.) return 0.;
        if (bPrimaryTarget) return ProjectedDistancePixels < 30. ? BaseDamage : 0.;
        if (ProjectedDistancePixels >= 90.) return 0.;
        const double DistanceSquared = ProjectedDistancePixels * ProjectedDistancePixels;
        return std::trunc(BaseDamage * ((8100. - DistanceSquared) / 8100.));
    }
}

#include "BYMCombatRules.h"

#include <cmath>

namespace BYMCombatRules
{
    namespace
    {
        constexpr double SourcePixelsPerCell = 20.;
        bool ValidPoint(const FVector2D& Point)
        { return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y); }
        FVector2D SourceGroundPoint(const FVector2D& Point)
        {
            // PATHING.FromISO returns int-valued coordinates, truncating
            // toward zero, unlike floor for a negative source coordinate.
            return FVector2D(std::trunc(Point.X * SourcePixelsPerCell),
                std::trunc(Point.Y * SourcePixelsPerCell));
        }
    }

    int32 TargetTier(int32 TargetGroup, bool bWall, bool bLootable, bool bTower)
    {
        if ((TargetGroup == 2 && bWall) || (TargetGroup == 3 && bLootable)
            || (TargetGroup == 4 && bTower)) return 0;
        if (bWall) return -1;
        return TargetGroup == 1 ? 0 : 1;
    }

    double TargetDistance(const FVector2D& Monster, const FVector2D& BuildingAnchor, double BuildingHalfHeight)
    {
        if (!ValidPoint(Monster) || !ValidPoint(BuildingAnchor) || !FMath::IsFinite(BuildingHalfHeight)
            || BuildingHalfHeight < 0) return TNumericLimits<double>::Max();
        const double Middle = BuildingHalfHeight * SourcePixelsPerCell;
        const FVector2D Target = BuildingAnchor * SourcePixelsPerCell + FVector2D(Middle, Middle);
        return ((SourceGroundPoint(Monster) - Target).Size() - Middle) / SourcePixelsPerCell;
    }

    double EyeRaBuildingDamage(double Damage, const FVector2D& Monster, const FVector2D& BuildingAnchor)
    {
        if (!FMath::IsFinite(Damage) || Damage <= 0 || !ValidPoint(Monster) || !ValidPoint(BuildingAnchor)) return 0;
        const FVector2D Delta = SourceGroundPoint(Monster) - FVector2D(5, 5) - SourceGroundPoint(BuildingAnchor);
        const double DistanceSquared = Delta.X * Delta.X + Delta.Y * Delta.Y;
        constexpr double RadiusSquared = 60. * 60.;
        if (!FMath::IsFinite(DistanceSquared) || DistanceSquared >= RadiusSquared) return 0;
        return std::floor(Damage * ((RadiusSquared - DistanceSquared) / RadiusSquared) + .5);
    }

    double TowerShotDamage(double Damage, double Health, double MaxHealth, double GlobalMultiplier)
    {
        if (!FMath::IsFinite(Damage) || !FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth)
            || !FMath::IsFinite(GlobalMultiplier) || Damage <= 0 || Health <= 0 || MaxHealth <= 0 || GlobalMultiplier <= 0) return 0;
        const double HealthFraction = FMath::Clamp(Health / MaxHealth, 0., 1.);
        return std::floor(Damage * (.5 + .5 * HealthFraction)) * GlobalMultiplier;
    }
}

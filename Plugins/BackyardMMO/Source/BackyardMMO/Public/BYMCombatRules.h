#pragma once

#include "CoreMinimal.h"

/** Active original overworld combat rules, independent of actors and rendering.
 * Coordinates are logical yard cells (20 original source pixels per cell).
 */
namespace BYMCombatRules
{
    /** Preferred categories are searched before any fallback category. Walls
     * are excluded from general fallback, but may still be attacked to clear
     * a blocked path. Negative means not a direct target in this category.
     */
    BACKYARDMMO_API int32 TargetTier(int32 TargetGroup, bool bWall, bool bLootable, bool bTower);

    /** MonsterBase.findTarget uses ground-space distance to the building's
     * source middle, minus its half-height; it does not use an arbitrary
     * multiplier to make preferred targets merely appear closer.
     */
    BACKYARDMMO_API double TargetDistance(const FVector2D& Monster, const FVector2D& BuildingAnchor,
        double BuildingHalfHeight);

    /** Unpowered C5: 60px blast, squared-distance falloff and Math.round.
     * The active source compares the (-5,-5)-offset monster position to the
     * building anchor. Its discarded Point.add result is intentionally not
     * replaced with a different, centred explosion rule.
     */
    BACKYARDMMO_API double EyeRaBuildingDamage(double Damage, const FVector2D& Monster,
        const FVector2D& BuildingAnchor);

    /** BUILDING20/21/23/25 truncate each shot after the health multiplier.
     * Requested NXT global damage scaling is applied to that source shot.
     */
    BACKYARDMMO_API double TowerShotDamage(double Damage, double Health, double MaxHealth,
        double GlobalMultiplier = 1.0);
}

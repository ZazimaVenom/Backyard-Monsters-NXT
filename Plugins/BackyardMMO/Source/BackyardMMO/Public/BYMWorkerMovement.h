#pragma once
#include "CoreMinimal.h"
#include "Math/IntRect.h"

/** Shared host/presentation route clock. Original workers use facing poses, not
 * a walking frame loop. Travel must never depend on a client's camera or FPS. */
namespace BYMWorkerMovement
{
    struct FRoutePoint
    {
        FVector2D Position = FVector2D::ZeroVector;
        double AtSeconds = 0.;
        bool bHop = false;
    };
    struct FSample
    {
        FVector2D Position = FVector2D::ZeroVector;
        FVector2D Facing = FVector2D(0,1);
        double HopHeightPixels = 0.;
        bool bArrived = false;
    };
    /** Absolute logical cells (20 original Cartesian pixels). Bounds are
     * inclusive Min/exclusive Max. Occupied cells cost twice open ground and
     * remain traversable, matching WORKER.Target(ignoreWalls=true). */
    BACKYARDMMO_API bool PlanRoute(const FVector2D& Start, const FVector2D& Target,
        const FIntRect& Bounds, const TArray<FIntRect>& Occupied,
        TArray<FRoutePoint>& OutRoute, double& OutDuration);
    /** AtSeconds is relative to the host's startedAt. bHop begins at that point.
     * The original hop is 40 source pixels, .4s Sine-out then .4s Bounce-out. */
    BACKYARDMMO_API FSample SampleRoute(const TArray<FRoutePoint>& Route, double Elapsed);
}

#pragma once
#include "CoreMinimal.h"

/** Original TickFast display cadence, independent of render frame rate. */
namespace BYMBuildingAnimation
{
    /** Source Bunker door phase is 0..15 at forty display ticks/second, but
     * its supplied image has only fifteen cells. Preserve phase independently
     * and clamp the visible endpoint to the final complete source cell. */
    BACKYARDMMO_API double BunkerDoorPhase(double Phase, double Elapsed, bool bOpening, bool bMoving = true);
    BACKYARDMMO_API int32 BunkerDoorFrame(double Phase, int32 AvailableFrames = 15);

    struct FClock
    {
        int64 DisplayTick = -1;
        int32 DisplayedOrdinal = 0;
        int32 NextOrdinal = 0;
        bool bInitialized = false;
    };

    /** Advance a source frame ordinal. A paused machine retains its frame while
     * display cadence continues; Hatcheries request reset-to-zero instead.
     * AnimFrame copies the current source cell before advancing its counter.
     * Fast mode uses (tick % Fast == 0 || tick % Slow == 0), counting shared
     * multiples once, just like the original if/else-if TickFast branches.
     */
    BACKYARDMMO_API int32 SelectFrame(FClock& Clock, double Seconds, double DisplayHz,
        int32 FastDivisor, int32 SlowDivisor, int32 CycleLength, bool bWorking,
        bool bSlow, bool bResetWhenPaused, int32 InitialOrdinal = 0, int32 Phase = 0);
}

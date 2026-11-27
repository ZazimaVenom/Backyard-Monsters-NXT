#include "BYMBuildingAnimation.h"
#include <cmath>

namespace BYMBuildingAnimation
{
    double BunkerDoorPhase(double Phase, double Elapsed, bool bOpening, bool bMoving)
    {
        if (!FMath::IsFinite(Phase)) Phase = 0.;
        Phase = FMath::Clamp(Phase, 0., 15.);
        if (!bMoving || !FMath::IsFinite(Elapsed) || Elapsed <= 0.) return Phase;
        const double Steps = FMath::Min(Elapsed, 1.) * 40.;
        return FMath::Clamp(Phase + (bOpening ? Steps : -Steps), 0., 15.);
    }

    int32 BunkerDoorFrame(double Phase, int32 AvailableFrames)
    {
        if (!FMath::IsFinite(Phase) || AvailableFrames < 1) return 0;
        // DeltaSeconds arrives as a float. Tolerate its sub-microtick boundary
        // error so closing at exactly eight source ticks cannot skip a cell.
        return FMath::Clamp(FMath::FloorToInt(FMath::Clamp(Phase, 0., 15.) + 1.e-6), 0, AvailableFrames - 1);
    }

    int32 SelectFrame(FClock& Clock, double Seconds, double DisplayHz, int32 FastDivisor,
        int32 SlowDivisor, int32 CycleLength, bool bWorking, bool bSlow,
        bool bResetWhenPaused, int32 InitialOrdinal, int32 Phase)
    {
        if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(DisplayHz) || Seconds < 0.
            || DisplayHz <= 0. || DisplayHz > 120. || Seconds * DisplayHz > 1.e12
            || FastDivisor < 1 || FastDivisor > 1000 || SlowDivisor < 1 || SlowDivisor > 1000
            || CycleLength < 1 || CycleLength > 65536 || Phase < 0 || Phase > 1000) return Clock.DisplayedOrdinal;
        const int64 Tick = static_cast<int64>(std::floor(Seconds * DisplayHz)) + Phase;
        if (!Clock.bInitialized)
        {
            Clock.DisplayedOrdinal = FMath::Clamp(InitialOrdinal, 0, CycleLength - 1);
            Clock.NextOrdinal = Clock.DisplayedOrdinal;
            Clock.DisplayTick = Tick - 1;
            Clock.bInitialized = true;
        }
        if (Tick < Clock.DisplayTick) return Clock.DisplayedOrdinal; // no clock rewind
        Clock.DisplayedOrdinal = FMath::Clamp(Clock.DisplayedOrdinal, 0, CycleLength - 1);
        Clock.NextOrdinal = FMath::Clamp(Clock.NextOrdinal, 0, CycleLength - 1);
        if (bWorking)
        {
            const auto Multiples = [](int64 Last, int64 Current, int32 Divisor)
            {
                // Tick zero is the first source TickFast call and qualifies.
                return Current / Divisor - (Last < 0 ? -1 : Last / Divisor);
            };
            int64 Steps = Multiples(Clock.DisplayTick, Tick, SlowDivisor);
            if (!bSlow)
            {
                int32 A = FastDivisor, B = SlowDivisor;
                while (B != 0) { const int32 Remainder = A % B; A = B; B = Remainder; }
                const int32 Common = FastDivisor / A * SlowDivisor;
                Steps += Multiples(Clock.DisplayTick, Tick, FastDivisor)
                    - Multiples(Clock.DisplayTick, Tick, Common);
            }
            if (Steps > 0)
            {
                Clock.DisplayedOrdinal = static_cast<int32>((Clock.NextOrdinal + Steps - 1) % CycleLength);
                Clock.NextOrdinal = static_cast<int32>((Clock.NextOrdinal + Steps) % CycleLength);
            }
        }
        else if (bResetWhenPaused) { Clock.DisplayedOrdinal = 0; Clock.NextOrdinal = 0; }
        Clock.DisplayTick = Tick;
        return Clock.DisplayedOrdinal;
    }
}

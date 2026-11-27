#include "BYMBuildingLifecycle.h"
#include <cmath>

namespace BYMBuildingLifecycle
{
    namespace { constexpr double Year = 31536000.; }
    bool CanWork(double Health, double MaxHealth, bool bRepairing)
    {
        return FMath::IsFinite(Health) && FMath::IsFinite(MaxHealth) && MaxHealth >= 1.
            && Health == MaxHealth && !bRepairing;
    }
    double AdvanceWork(double Remaining, double Seconds, double Health, double MaxHealth, bool bRepairing)
    {
        if (!FMath::IsFinite(Remaining) || Remaining < 0. || Remaining > Year
            || !FMath::IsFinite(Seconds) || Seconds < 0. || !CanWork(Health, MaxHealth, bRepairing)) return Remaining;
        return FMath::Max(0., Remaining - Seconds);
    }
    double AfterDestruction(double Remaining, double FullDuration)
    {
        if (!FMath::IsFinite(Remaining) || !FMath::IsFinite(FullDuration)
            || Remaining < 0. || Remaining > Year || FullDuration < 1. || FullDuration > Year) return Remaining;
        // BFOUNDATION uses integer locals for duration, half elapsed work and
        // the resulting countdown. Preserve that source truncation order.
        const double Duration = std::trunc(FullDuration);
        const double Setback = FMath::Min(std::trunc((Duration - Remaining) * .5), 28800.);
        return FMath::Clamp(std::trunc(Remaining + Setback), 0., Year);
    }
    double MigrateRemaining(double Deadline, double LastEconomy, double Now)
    {
        if (!FMath::IsFinite(Deadline) || !FMath::IsFinite(LastEconomy) || !FMath::IsFinite(Now)
            || Deadline < 0. || LastEconomy < 0. || Now < 0.) return 0.;
        const double Origin = FMath::Max(0., FMath::Min(Now, FMath::Max(LastEconomy, Now - Year)));
        return FMath::Clamp(Deadline - Origin, 0., Year);
    }
}

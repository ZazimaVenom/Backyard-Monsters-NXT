#pragma once
#include "CoreMinimal.h"

/** Original main-yard building work rules; account and world storage are unrelated. */
namespace BYMBuildingLifecycle
{
    BACKYARDMMO_API bool CanWork(double Health, double MaxHealth, bool bRepairing);
    BACKYARDMMO_API double AdvanceWork(double Remaining, double Seconds, double Health, double MaxHealth, bool bRepairing);
    /** Normal lethal damage clears repair before applying this upgrade setback. */
    BACKYARDMMO_API double AfterDestruction(double Remaining, double FullDuration);
    /** Convert schema-1/2/3 absolute job deadlines at the same bounded accrual origin. */
    BACKYARDMMO_API double MigrateRemaining(double Deadline, double LastEconomy, double Now);
}

#pragma once

#include "CoreMinimal.h"

/** Main-yard collector and free-repair rules from BRESOURCE/BFOUNDATION.
 * No clock, wallet owner, networking, or file access is hidden in these helpers.
 */
namespace BYMEconomy
{
    struct FCollectorState
    {
        double Stored = 0.;
        // Real seconds remaining in the current cycle; damage never rescales
        // a cycle already in flight. Zero means stopped or not yet started.
        double Remaining = 0.;
    };

    struct FCollectorRules
    {
        double BaseCycle = 10.;
        double Amount = 0.;
        double Capacity = 0.;
        // NXT override: scales time, preserving intact source-sized batches.
        // One uses the original rules; zero pauses the existing countdown.
        double RateMultiplier = 1.;
    };

    struct FRepairState
    {
        double Health = 0.;
        double MaxHealth = 1.;
        double Rate = 0.;
        bool bRepairing = false;
    };

    BACKYARDMMO_API double RepairRate(double MaxHealth, double SourceRepairSeconds, double TimeMultiplier = 1.);
    BACKYARDMMO_API double RepairDuration(const FRepairState& State);
    /** Returns zero when the threshold is already met; -1 if it cannot be reached. */
    BACKYARDMMO_API double SecondsToHealth(const FRepairState& State, double HealthFraction);
    BACKYARDMMO_API bool AdvanceRepair(FRepairState& State, double Seconds);
    BACKYARDMMO_API double ProductionTimeout(double BaseCycle, double Health, double MaxHealth);
    BACKYARDMMO_API double CycleSeconds(const FCollectorRules& Rules, const FRepairState& State);
    BACKYARDMMO_API bool CanProduce(const FCollectorState& Collector, const FCollectorRules& Rules,
        const FRepairState& State, bool bProductionBlocked);

    /** Advances repairs and discrete production together. Preserves countdowns
     * across damage/construction pauses, skips bulk offline cycles, and splits
     * repair at health/cycle boundaries rather than running a per-second loop.
     * Invalid input returns false without modifying either state.
     */
    BACKYARDMMO_API bool Advance(FCollectorState& Collector, FRepairState& State,
        const FCollectorRules& Rules, bool bProductionBlocked, double Seconds);

    /** Transfer only wallet room. Remaining stock and over-cap wallets survive;
     * taking from a full collector starts a fresh cycle. Returns amount accepted.
     */
    BACKYARDMMO_API int64 Bank(FCollectorState& Collector, const FCollectorRules& Rules,
        const FRepairState& State, int64& Wallet, int64 WalletCapacity);

    /** BASE.CalcResources counts a completed silo's level while damaged,
     * repairing or upgrading. A level-zero construction has no capacity yet.
     */
    BACKYARDMMO_API double StorageContribution(int32 CompletedLevel, double LevelCapacity);
}

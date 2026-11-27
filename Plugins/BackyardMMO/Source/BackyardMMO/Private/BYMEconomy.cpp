#include "BYMEconomy.h"

#include <cmath>
#include <limits>

namespace BYMEconomy
{
    namespace
    {
        constexpr double MaximumExactInteger = 9007199254740991.;
        bool ValidRepair(const FRepairState& State)
        {
            return FMath::IsFinite(State.Health) && FMath::IsFinite(State.MaxHealth)
                && FMath::IsFinite(State.Rate) && State.MaxHealth >= 1. && State.MaxHealth <= MaximumExactInteger
                && State.Health >= 0. && State.Health <= State.MaxHealth && State.Rate >= 0.
                && State.Rate <= std::ceil(State.MaxHealth) * 100.
                && (!State.bRepairing || State.Rate > 0.);
        }
        bool ValidCollector(const FCollectorState& Collector, const FCollectorRules& Rules)
        {
            return FMath::IsFinite(Collector.Stored) && FMath::IsFinite(Collector.Remaining)
                && FMath::IsFinite(Rules.BaseCycle) && FMath::IsFinite(Rules.Amount)
                && FMath::IsFinite(Rules.Capacity) && FMath::IsFinite(Rules.RateMultiplier)
                && Rules.BaseCycle >= .1 && Rules.BaseCycle <= 86400.
                && Rules.Amount >= 0. && Rules.Amount <= MaximumExactInteger
                && Rules.Capacity >= 0. && Rules.Capacity <= MaximumExactInteger
                && Rules.RateMultiplier >= 0. && Rules.RateMultiplier <= 100.
                && Collector.Stored >= 0. && Collector.Stored <= Rules.Capacity
                && Collector.Remaining >= 0. && Collector.Remaining <= 31536000.;
        }
        void Heal(FRepairState& State, double Seconds)
        {
            if (!State.bRepairing) return;
            State.Health = FMath::Min(State.MaxHealth, State.Health + State.Rate * Seconds);
            if (State.Health >= State.MaxHealth) State.bRepairing = false;
        }
        // Inside a fixed-health penalty band every newly started cycle has the
        // same length. Finish the existing cycle first, then skip full batches.
        void ProduceSegment(FCollectorState& Collector, const FCollectorRules& Rules,
            double Cycle, double Seconds)
        {
            if (Seconds <= 0.) return;
            if (Collector.Remaining <= 0.) Collector.Remaining = Cycle;
            if (Seconds < Collector.Remaining)
            {
                Collector.Remaining -= Seconds;
                return;
            }
            Seconds -= Collector.Remaining;
            Collector.Stored = FMath::Min(Rules.Capacity, Collector.Stored + Rules.Amount);
            Collector.Remaining = 0.;
            if (Collector.Stored >= Rules.Capacity) return;
            const double Complete = std::floor(Seconds / Cycle);
            const double Needed = std::ceil((Rules.Capacity - Collector.Stored) / Rules.Amount);
            if (Complete >= Needed)
            {
                Collector.Stored = Rules.Capacity;
                return;
            }
            Collector.Stored = FMath::Min(Rules.Capacity, Collector.Stored + Complete * Rules.Amount);
            Seconds -= Complete * Cycle;
            // A completion at the segment boundary is restarted using the new
            // health by the caller, not with the previous penalty band's cycle.
            Collector.Remaining = Seconds > 0. ? Cycle - Seconds : 0.;
        }
    }

    double RepairRate(double MaxHealth, double SourceRepairSeconds, double TimeMultiplier)
    {
        if (!FMath::IsFinite(MaxHealth) || !FMath::IsFinite(SourceRepairSeconds)
            || !FMath::IsFinite(TimeMultiplier) || MaxHealth < 1. || MaxHealth > MaximumExactInteger || SourceRepairSeconds < 1.
            || TimeMultiplier < .01 || TimeMultiplier > 100.) return 0.;
        const double Rate = std::ceil(MaxHealth / FMath::Min(3600., SourceRepairSeconds)) / TimeMultiplier;
        return FMath::IsFinite(Rate) ? Rate : 0.;
    }

    double RepairDuration(const FRepairState& State)
    {
        if (!ValidRepair(State) || !State.bRepairing || State.Health >= State.MaxHealth) return 0.;
        return (State.MaxHealth - State.Health) / State.Rate;
    }

    double SecondsToHealth(const FRepairState& State, double HealthFraction)
    {
        if (!ValidRepair(State) || !FMath::IsFinite(HealthFraction) || HealthFraction < 0. || HealthFraction > 1.) return -1.;
        const double Missing = State.MaxHealth * HealthFraction - State.Health;
        if (Missing <= 0.) return 0.;
        return State.bRepairing ? Missing / State.Rate : -1.;
    }

    bool AdvanceRepair(FRepairState& State, double Seconds)
    {
        if (!ValidRepair(State) || !FMath::IsFinite(Seconds) || Seconds < 0. || Seconds > 31536000.) return false;
        Heal(State, Seconds);
        return true;
    }

    double ProductionTimeout(double BaseCycle, double Health, double MaxHealth)
    {
        if (!FMath::IsFinite(BaseCycle) || !FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth)
            || BaseCycle < .1 || BaseCycle > 86400. || MaxHealth < 1. || MaxHealth > MaximumExactInteger || Health < 0. || Health > MaxHealth) return 0.;
        // Algebraically the source BRESOURCE.productionTimeout getter. The
        // division form avoids unnecessary overflow for large health values.
        return BaseCycle + std::ceil(BaseCycle * (4. - 4. * (Health / MaxHealth)));
    }

    double CycleSeconds(const FCollectorRules& Rules, const FRepairState& State)
    {
        if (!FMath::IsFinite(Rules.RateMultiplier) || Rules.RateMultiplier <= 0. || Rules.RateMultiplier > 100.) return 0.;
        const double SourceCycle = ProductionTimeout(Rules.BaseCycle, State.Health, State.MaxHealth);
        if (SourceCycle <= 0.) return 0.;
        const double Cycle = SourceCycle / Rules.RateMultiplier;
        // Tiny positive custom rates must not manufacture a saved countdown
        // that exceeds the simulation's one-year time horizon or is infinite.
        return FMath::IsFinite(Cycle) ? FMath::Min(31536000., Cycle) : 31536000.;
    }

    bool CanProduce(const FCollectorState& Collector, const FCollectorRules& Rules,
        const FRepairState& State, bool bProductionBlocked)
    {
        return ValidCollector(Collector, Rules) && ValidRepair(State) && !bProductionBlocked
            && State.Health >= State.MaxHealth * .5 && Rules.Amount > 0.
            && Rules.RateMultiplier > 0. && Collector.Stored < Rules.Capacity;
    }

    bool Advance(FCollectorState& Collector, FRepairState& State,
        const FCollectorRules& Rules, bool bProductionBlocked, double Seconds)
    {
        if (!ValidCollector(Collector, Rules) || !ValidRepair(State)
            || !FMath::IsFinite(Seconds) || Seconds < 0. || Seconds > 31536000.) return false;
        if (Collector.Stored >= Rules.Capacity) Collector.Remaining = 0.;
        if (bProductionBlocked || Rules.Amount <= 0. || Rules.RateMultiplier <= 0. || Collector.Stored >= Rules.Capacity)
        {
            Heal(State, Seconds);
            return true;
        }
        // Health below half pauses the same countdown. The source free repair
        // rate is constant, so crossing this threshold has a direct solution.
        if (State.Health < State.MaxHealth * .5)
        {
            const double ToHalf = SecondsToHealth(State, .5);
            if (ToHalf < 0. || Seconds < ToHalf)
            {
                Heal(State, Seconds);
                return true;
            }
            Heal(State, ToHalf);
            State.Health = FMath::Max(State.Health, State.MaxHealth * .5);
            Seconds -= ToHalf;
        }
        while (Seconds > 0. && Collector.Stored < Rules.Capacity)
        {
            const double Cycle = CycleSeconds(Rules, State);
            if (Collector.Remaining <= 0.) Collector.Remaining = Cycle;
            // No completion before the end: penalty changes cannot alter the
            // currently running countdown. Skip directly to the final health.
            if (Collector.Remaining > Seconds)
            {
                Collector.Remaining -= Seconds;
                Heal(State, Seconds);
                Seconds = 0.;
                break;
            }
            if (!State.bRepairing)
            {
                ProduceSegment(Collector, Rules, Cycle, Seconds);
                Seconds = 0.;
                break;
            }
            // End this segment at the next integer penalty change. This is
            // bounded by health bands / actual completions, never offline time.
            const double Penalty = std::ceil(Rules.BaseCycle * (4. - 4. * (State.Health / State.MaxHealth)));
            const double NextHealth = Penalty > 0.
                ? State.MaxHealth * (1. - (Penalty - 1.) / (4. * Rules.BaseCycle)) : State.MaxHealth;
            double Segment = FMath::Min(Seconds, FMath::Max(0., (NextHealth - State.Health) / State.Rate));
            if (Segment <= 0.)
            {
                // Floating-point cancellation at a boundary must not stall.
                State.Health = FMath::Min(State.MaxHealth, std::nextafter(State.Health, State.MaxHealth));
                if (State.Health >= State.MaxHealth) State.bRepairing = false;
                continue;
            }
            ProduceSegment(Collector, Rules, Cycle, Segment);
            Heal(State, Segment);
            Seconds -= Segment;
        }
        Heal(State, Seconds);
        if (Collector.Stored >= Rules.Capacity) Collector.Remaining = 0.;
        else if (Collector.Remaining <= 0. && CanProduce(Collector, Rules, State, bProductionBlocked))
            Collector.Remaining = CycleSeconds(Rules, State);
        return true;
    }

    int64 Bank(FCollectorState& Collector, const FCollectorRules& Rules,
        const FRepairState& State, int64& Wallet, int64 WalletCapacity)
    {
        if (!ValidCollector(Collector, Rules) || !ValidRepair(State) || Wallet < 0 || WalletCapacity < 0
            || static_cast<double>(Wallet) > MaximumExactInteger || static_cast<double>(WalletCapacity) > MaximumExactInteger) return 0;
        const int64 Room = WalletCapacity > Wallet ? WalletCapacity - Wallet : 0;
        const int64 Accepted = FMath::Min(static_cast<int64>(std::floor(Collector.Stored)), Room);
        if (Accepted <= 0) return 0;
        const bool bWasFull = Collector.Stored >= Rules.Capacity;
        Wallet += Accepted;
        Collector.Stored -= static_cast<double>(Accepted);
        if (bWasFull || Collector.Remaining <= 0.)
            Collector.Remaining = State.Health > 0. ? CycleSeconds(Rules, State) : 0.;
        return Accepted;
    }

    double StorageContribution(int32 CompletedLevel, double LevelCapacity)
    {
        return CompletedLevel > 0 && FMath::IsFinite(LevelCapacity) && LevelCapacity > 0. && LevelCapacity <= MaximumExactInteger
            ? LevelCapacity : 0.;
    }
}

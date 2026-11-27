#include "BYMExperience.h"

#include <cmath>

namespace BYMExperience
{
    namespace
    {
        constexpr int64 Thresholds[] = {
#include "BYMExperienceThresholds.inl"
        };
        static_assert(sizeof(Thresholds) / sizeof(Thresholds[0]) == 100, "Preserve the complete active Flash threshold table");
        int64 Bounded(int64 Value) { return Value < 0 ? 0 : (Value > MaximumPoints ? MaximumPoints : Value); }
    }

    bool IsValidSavedPoints(double Value)
    {
        return std::isfinite(Value) && Value >= 0. && Value <= static_cast<double>(MaximumPoints) && std::floor(Value) == Value;
    }

    int64 Add(int64 Current, int64 Award)
    {
        Current = Bounded(Current); Award = Bounded(Award);
        return Award > MaximumPoints - Current ? MaximumPoints : Current + Award;
    }

    int32 ThresholdCount() { return static_cast<int32>(sizeof(Thresholds) / sizeof(Thresholds[0])); }

    int64 Threshold(int32 Index)
    {
        return Thresholds[Index < 0 ? 0 : (Index >= ThresholdCount() ? ThresholdCount() - 1 : Index)];
    }

    FProgress Progress(int64 Earned, int64 BaseValue)
    {
        FProgress Result;
        Result.Earned = Bounded(Earned); Result.BaseValue = Bounded(BaseValue);
        Result.Points = Add(Result.Earned, Result.BaseValue);
        Result.MaxLevel = ThresholdCount() - 1;
        // BASE.BaseLevel deliberately leaves the final table entry as an upper
        // boundary. Preserve its level-99 cap instead of adopting the supplied
        // server's separately truncated 56-entry table.
        for (int32 Index = 0; Index < Result.MaxLevel; ++Index)
        {
            if (Result.Points < Thresholds[Index]) break;
            Result.Level = Index + 1; Result.Lower = Thresholds[Index]; Result.Upper = Thresholds[Index + 1];
        }
        Result.Needed = Result.Points < Result.Upper ? Result.Upper - Result.Points : 0;
        const double Fraction = static_cast<double>(Result.Points - Result.Lower) / static_cast<double>(Result.Upper - Result.Lower);
        Result.Fraction = Fraction < 0. ? 0. : (Fraction > 1. ? 1. : Fraction);
        Result.bAtMaxLevel = Result.Level == Result.MaxLevel;
        return Result;
    }

    int64 RetainBaseValue(int64 PreviousHighWater, int64 CompletedCostTotal)
    {
        const int64 Total = Bounded(CompletedCostTotal);
        const int64 Current = Total / 10 + (Total % 10 != 0 ? 1 : 0);
        const int64 Previous = Bounded(PreviousHighWater);
        return Previous > Current ? Previous : Current;
    }

    int64 ConstructionReward(int64 SourceSeconds, int64 ResourceCost, bool bTownHall)
    {
        // The bounded operands keep this exact integer numerator below int64
        // capacity even for malformed/extreme helper inputs.
        const int64 Award = (Bounded(SourceSeconds) * 5 + Bounded(ResourceCost)) / 10;
        return Add(Award, bTownHall ? 100 : 0);
    }

    int64 UpgradeReward(int64 PreviousSourceSeconds, int64 PreviousResourceCost)
    {
        return (Bounded(PreviousSourceSeconds) + Bounded(PreviousResourceCost)) / 3;
    }

    int64 CollectorReward(int64 Accepted, bool bTutorial)
    {
        Accepted = Bounded(Accepted);
        return bTutorial ? Accepted : Accepted / 2 + Accepted % 2;
    }

    int64 LootReward(int64 AwardedLoot) { return Bounded(AwardedLoot); }
}

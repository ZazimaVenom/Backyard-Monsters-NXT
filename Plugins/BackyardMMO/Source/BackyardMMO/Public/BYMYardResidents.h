#pragma once

#include "CoreMinimal.h"
#include "Math/IntRect.h"
#include "Math/RandomStream.h"

/** Cosmetic residents represent a bounded sample of an existing ready army.
 * None of these helpers create units, train monsters or affect combat/save state.
 */
namespace BYMYardResidents
{
    constexpr int32 MaximumResidents = 50;

    struct FResident
    {
        FString Id;
        FString Type;
        int32 Ordinal = 0;
    };

    /** Deterministic round-robin allocation keeps every owned species represented
     * before allocating additional residents, without exceeding its ready count.
     */
    BACKYARDMMO_API TArray<FResident> SelectRoster(const TMap<FString, int32>& ReadyArmy, int32 Limit);

    /** Cached occupancy for one displayed yard. Rectangles use inclusive Min,
     * exclusive Max; callers round fractional collision bounds outwards.
     * The outermost cell is reserved so residents remain inside the yard.
     */
    class BACKYARDMMO_API FGrid
    {
    public:
        void Reset(int32 InCells, const TArray<FIntRect>& Obstacles);
        void Reset(const FIntRect& InBounds, const TArray<FIntRect>& Obstacles);
        int32 GetCells() const { return Cells; }
        bool HasFreeCells() const { return !FreeCells.IsEmpty(); }
        bool IsWalkable(const FVector2D& Position) const;
        bool IsCellWalkable(int32 X, int32 Y) const;
        bool FindPath(const FVector2D& Start, const FVector2D& Goal, TArray<FVector2D>& OutPath) const;
        bool ChooseWanderPath(const FVector2D& Start, FRandomStream& Random, TArray<FVector2D>& OutPath) const;
        bool PickFreePosition(FRandomStream& Random, FVector2D& OutPosition) const;
    private:
        int32 Cells = 0;
        FIntRect Bounds;
        int32 Width = 0;
        int32 Height = 0;
        TArray<uint8> Blocked;
        TArray<int32> FreeCells;
        void Search(int32 Start, int32 MaximumSteps, TArray<int32>& Parents, TArray<int32>& Reachable) const;
        void RestorePath(int32 Start, int32 Goal, const TArray<int32>& Parents, TArray<FVector2D>& OutPath) const;
        FVector2D CellCenter(int32 Index) const;
        int32 CellIndex(const FVector2D& Position) const;
    };

    struct FWalker
    {
        FVector2D Position = FVector2D::ZeroVector;
        FVector2D Facing = FVector2D(0, 1);
        TArray<FVector2D> Path;
        int32 PathIndex = 0;
        float PauseRemaining = 0.f;
        bool bMoving = false;
        bool bInitialized = false;
        FRandomStream Random;
    };

    BACKYARDMMO_API bool InitializeWalker(FWalker& Walker, const FGrid& Grid, uint32 Seed);
    /** Call after a layout change. Retains a valid position; a new building may
     * relocate its resident to a free cell. False means there is nowhere to roam.
     */
    BACKYARDMMO_API bool RevalidateWalker(FWalker& Walker, const FGrid& Grid);
    BACKYARDMMO_API void TickWalker(FWalker& Walker, const FGrid& Grid, float DeltaSeconds, float CellsPerSecond);
}

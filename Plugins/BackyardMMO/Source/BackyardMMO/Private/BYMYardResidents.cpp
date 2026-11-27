#include "BYMYardResidents.h"

namespace BYMYardResidents
{
    TArray<FResident> SelectRoster(const TMap<FString, int32>& ReadyArmy, int32 Limit)
    {
        TArray<FResident> Residents;
        const int32 CountLimit = FMath::Clamp(Limit, 0, MaximumResidents);
        if (CountLimit == 0) return Residents;
        TArray<FString> Types;
        for (const TPair<FString, int32>& Entry : ReadyArmy)
            if (!Entry.Key.IsEmpty() && Entry.Value > 0) Types.Add(Entry.Key);
        Types.Sort();
        Residents.Reserve(CountLimit);
        // At most 50 passes; a large army count cannot create an unbounded loop.
        for (int32 Ordinal = 0; Ordinal < CountLimit && Residents.Num() < CountLimit; ++Ordinal)
        {
            bool bAdded = false;
            for (const FString& Type : Types)
            {
                if (Residents.Num() == CountLimit) break;
                if (ReadyArmy.FindRef(Type) <= Ordinal) continue;
                FResident Resident;
                Resident.Type = Type; Resident.Ordinal = Ordinal;
                Resident.Id = Type + TEXT(":") + FString::FromInt(Ordinal);
                Residents.Add(MoveTemp(Resident)); bAdded = true;
            }
            if (!bAdded) break;
        }
        return Residents;
    }

    void FGrid::Reset(int32 InCells, const TArray<FIntRect>& Obstacles)
    {
        const int32 Side=FMath::Clamp(InCells,3,256);
        Reset(FIntRect(0,0,Side,Side),Obstacles);
    }

    void FGrid::Reset(const FIntRect& InBounds, const TArray<FIntRect>& Obstacles)
    {
        Width=FMath::Clamp(InBounds.Width(),3,256);Height=FMath::Clamp(InBounds.Height(),3,256);
        Bounds=FIntRect(InBounds.Min.X,InBounds.Min.Y,InBounds.Min.X+Width,InBounds.Min.Y+Height);
        Cells=FMath::Max(Width,Height);
        Blocked.Init(0,Width*Height);FreeCells.Reset();
        for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
            if(X==0||Y==0||X==Width-1||Y==Height-1)Blocked[Y*Width+X]=1;
        for(const FIntRect& Rect:Obstacles)
            for(int32 Y=FMath::Max(Rect.Min.Y,Bounds.Min.Y);Y<FMath::Min(Rect.Max.Y,Bounds.Max.Y);++Y)
                for(int32 X=FMath::Max(Rect.Min.X,Bounds.Min.X);X<FMath::Min(Rect.Max.X,Bounds.Max.X);++X)
                    Blocked[(Y-Bounds.Min.Y)*Width+X-Bounds.Min.X]=1;
        for(int32 I=0;I<Blocked.Num();++I)if(!Blocked[I])FreeCells.Add(I);
    }

    bool FGrid::IsCellWalkable(int32 X,int32 Y) const
    {
        if(X<Bounds.Min.X||Y<Bounds.Min.Y||X>=Bounds.Max.X||Y>=Bounds.Max.Y)return false;
        const int32 Index=(Y-Bounds.Min.Y)*Width+X-Bounds.Min.X;
        return Blocked.IsValidIndex(Index)&&!Blocked[Index];
    }

    bool FGrid::IsWalkable(const FVector2D& Position) const
    {
        if(!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y)
            ||Position.X<Bounds.Min.X||Position.Y<Bounds.Min.Y||Position.X>=Bounds.Max.X||Position.Y>=Bounds.Max.Y)return false;
        return IsCellWalkable(FMath::FloorToInt(Position.X),FMath::FloorToInt(Position.Y));
    }

    FVector2D FGrid::CellCenter(int32 Index) const
    {return FVector2D(Bounds.Min.X+Index%Width+.5,Bounds.Min.Y+Index/Width+.5);}

    int32 FGrid::CellIndex(const FVector2D& Position) const
    {return (FMath::FloorToInt(Position.Y)-Bounds.Min.Y)*Width+FMath::FloorToInt(Position.X)-Bounds.Min.X;}

    void FGrid::Search(int32 Start, int32 MaximumSteps, TArray<int32>& Parents, TArray<int32>& Reachable) const
    {
        Parents.Init(INDEX_NONE, Blocked.Num()); Reachable.Reset();
        if (!Blocked.IsValidIndex(Start) || Blocked[Start]) return;
        TArray<int32> Depth; Depth.Init(0, Blocked.Num());
        Parents[Start] = Start; Reachable.Add(Start);
        static const int32 DX[] = { 1, 0, -1, 0 }, DY[] = { 0, 1, 0, -1 };
        for (int32 I = 0; I < Reachable.Num(); ++I)
        {
            const int32 Index = Reachable[I], X = Bounds.Min.X+Index % Width, Y = Bounds.Min.Y+Index / Width;
            if (Depth[Index] >= MaximumSteps) continue;
            for (int32 Direction = 0; Direction < 4; ++Direction)
            {
                const int32 NX = X + DX[Direction], NY = Y + DY[Direction];
                if (!IsCellWalkable(NX, NY)) continue;
                const int32 Next = (NY-Bounds.Min.Y) * Width + NX-Bounds.Min.X;
                if (Parents[Next] != INDEX_NONE) continue;
                Parents[Next] = Index; Depth[Next] = Depth[Index] + 1; Reachable.Add(Next);
            }
        }
    }

    void FGrid::RestorePath(int32 Start, int32 Goal, const TArray<int32>& Parents, TArray<FVector2D>& OutPath) const
    {
        OutPath.Reset();
        if (!Parents.IsValidIndex(Goal) || Parents[Goal] == INDEX_NONE) return;
        TArray<FVector2D> Reverse;
        for (int32 Index = Goal; Index != Start; Index = Parents[Index]) Reverse.Add(CellCenter(Index));
        OutPath.Reserve(Reverse.Num());
        for (int32 I = Reverse.Num() - 1; I >= 0; --I) OutPath.Add(Reverse[I]);
    }

    bool FGrid::FindPath(const FVector2D& Start, const FVector2D& Goal, TArray<FVector2D>& OutPath) const
    {
        OutPath.Reset();
        if (!IsWalkable(Start) || !IsWalkable(Goal)) return false;
        const int32 StartIndex = CellIndex(Start);
        const int32 GoalIndex = CellIndex(Goal);
        if (StartIndex == GoalIndex) return true;
        TArray<int32> Parents, Reachable;
        Search(StartIndex, Width * Height, Parents, Reachable);
        if (Parents[GoalIndex] == INDEX_NONE) return false;
        RestorePath(StartIndex, GoalIndex, Parents, OutPath);
        return !OutPath.IsEmpty();
    }

    bool FGrid::ChooseWanderPath(const FVector2D& Start, FRandomStream& Random, TArray<FVector2D>& OutPath) const
    {
        OutPath.Reset();
        if (!IsWalkable(Start)) return false;
        const int32 StartIndex = CellIndex(Start);
        TArray<int32> Parents, Reachable;
        // A ten-step neighbourhood bounds planning work. Every candidate is
        // actually reachable; repeated random picks cannot spin behind a wall.
        Search(StartIndex, 10, Parents, Reachable);
        if (Reachable.Num() < 2) return false;
        const int32 Goal = Reachable[Random.RandRange(1, Reachable.Num() - 1)];
        RestorePath(StartIndex, Goal, Parents, OutPath);
        return !OutPath.IsEmpty();
    }

    bool FGrid::PickFreePosition(FRandomStream& Random, FVector2D& OutPosition) const
    {
        if (FreeCells.IsEmpty()) return false;
        OutPosition = CellCenter(FreeCells[Random.RandRange(0, FreeCells.Num() - 1)]);
        return true;
    }

    bool InitializeWalker(FWalker& Walker, const FGrid& Grid, uint32 Seed)
    {
        Walker = FWalker();
        Walker.Random.Initialize(static_cast<int32>(Seed));
        Walker.bInitialized = Grid.PickFreePosition(Walker.Random, Walker.Position);
        Walker.PauseRemaining = Walker.Random.FRandRange(.15f, 4.f);
        const float Angle = Walker.Random.FRandRange(0.f, 2.f * PI);
        Walker.Facing = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
        return Walker.bInitialized;
    }

    bool RevalidateWalker(FWalker& Walker, const FGrid& Grid)
    {
        Walker.Path.Reset(); Walker.PathIndex = 0; Walker.bMoving = false;
        if (!Walker.bInitialized || !Grid.IsWalkable(Walker.Position))
            Walker.bInitialized = Grid.PickFreePosition(Walker.Random, Walker.Position);
        Walker.PauseRemaining = Walker.Random.FRandRange(.15f, 1.5f);
        return Walker.bInitialized;
    }

    void TickWalker(FWalker& Walker, const FGrid& Grid, float DeltaSeconds, float CellsPerSecond)
    {
        Walker.bMoving = false;
        if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0 || !FMath::IsFinite(CellsPerSecond)) return;
        if (!Walker.bInitialized || !Grid.IsWalkable(Walker.Position))
        {
            RevalidateWalker(Walker, Grid);
            return;
        }
        const float Step = FMath::Min(DeltaSeconds, .1f);
        if (Walker.PauseRemaining > 0)
        {
            Walker.PauseRemaining = FMath::Max(0.f, Walker.PauseRemaining - Step);
            return;
        }
        if (Walker.PathIndex >= Walker.Path.Num())
        {
            Walker.PathIndex = 0;
            if (!Grid.ChooseWanderPath(Walker.Position, Walker.Random, Walker.Path))
            {
                Walker.PauseRemaining = Walker.Random.FRandRange(1.f, 4.f);
                return;
            }
        }
        double Budget = FMath::Clamp(CellsPerSecond, 0.f, 8.f) * Step;
        while (Budget > 0 && Walker.PathIndex < Walker.Path.Num())
        {
            const FVector2D Target = Walker.Path[Walker.PathIndex];
            if (!Grid.IsWalkable(Target)) { RevalidateWalker(Walker, Grid); return; }
            const FVector2D Delta = Target - Walker.Position;
            const double Distance = Delta.Size();
            if (Distance <= UE_SMALL_NUMBER) { ++Walker.PathIndex; continue; }
            Walker.Facing = Delta / Distance;
            const double Travel = FMath::Min(Budget, Distance);
            Walker.Position += Walker.Facing * Travel;
            Walker.bMoving = true; Budget -= Travel;
            if (Travel >= Distance) { Walker.Position = Target; ++Walker.PathIndex; }
        }
        if (Walker.PathIndex >= Walker.Path.Num())
        {
            Walker.Path.Reset(); Walker.PathIndex = 0;
            Walker.PauseRemaining = Walker.Random.FRandRange(.75f, 3.5f);
        }
    }
}

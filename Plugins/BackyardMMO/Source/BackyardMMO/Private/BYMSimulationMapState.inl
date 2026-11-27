// Included in BYMSim; this state is owned exclusively by the host simulation.
struct FMapCell
{
    FString Id, Owner, YardId, Parent;
    int32 X = 0, Y = 0, Kind = 100, Level = 0, Terrain = 0, TribeIndex = 0, Revision = 0;
    bool bHomeDefense = false;
    double ShieldUntil = 0, DestroyedUntil = 0;
};
struct FMapPlayer
{
    FString HomeCell;
    TArray<FString> Bookmarks;
    TMap<FString, int32> Retaliations;
    double ProductionAt = 0, RelocatedAt = 0, ProductionFraction = 0;
    bool bEntered = false;
};

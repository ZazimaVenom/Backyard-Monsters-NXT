// Included inside BYMSim::FYard. All state remains host-owned in the world snapshot.
struct FMushroom
{
    FString Id;
    double X = 0, Y = 0;
    int32 Frame = 1, Reward = 0;
    bool bPicking = false;
    double PickFinishAt = 0;
};
int32 ExpansionLevel = 0;
int32 PackingLevel = 0;
uint32 MushroomRandom = 1;
double MushroomLastGrowth = 0;
TArray<FMushroom> Mushrooms;
int32 MushroomsPicked = 0, GoldenMushroomsPicked = 0;
int64 MushroomShinyEarned = 0;


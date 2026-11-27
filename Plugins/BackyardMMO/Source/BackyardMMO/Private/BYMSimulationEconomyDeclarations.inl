// Included inside FBYMSimulation::FImpl.
struct FDecorationProduct
{
    int32 Type = 0, Price = 0, Limit = 0;
    FString Name, Texture, MenuTexture, Category;
};
TArray<FDecorationProduct> DecorationProducts;
bool LoadEconomyRules(FString& Error);
void InitializeEconomy(FYard& Y, bool bLegacy);
bool ReadEconomy(FYard& Y, const FObject& Source, FString& Error);
void WriteEconomy(const FYard& Y, const FObject& Object) const;
FObject EconomySnapshot(const FYard& Y, bool bPrivate) const;
void AdvanceEconomy(FYard& Y, double End);
double EconomyNextBoundary(const FYard& Y, double Cursor, double End) const;
bool HandlesEconomyCommand(const FString& Op) const;
FString ExecuteEconomy(FYard& Y, const FString& Op, const FObject& Params);
bool HasMushroomTask(const FYard& Y, const FString& Id) const;
int32 YardWidth(const FYard& Y) const;
int32 YardHeight(const FYard& Y) const;
int32 YardMinX(const FYard& Y) const;
int32 YardMinY(const FYard& Y) const;
bool EconomyFits(const FYard& Y, double X, double PosY, double Size, const FString& Ignore = FString()) const;
bool SpawnMushroom(FYard& Y, bool bInitial);
FString StoreReason(const FYard& Y) const;
FString MushroomClearReason(const FYard& Y) const;
FString BuildingSpeedupReason(const FYard& Y, const FBuilding& B, const FString& Code) const;
FObject BuildingSpeedupOptions(const FYard& Y, const FBuilding& B) const;


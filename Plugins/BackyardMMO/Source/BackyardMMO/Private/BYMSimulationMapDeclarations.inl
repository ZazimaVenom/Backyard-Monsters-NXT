// Included in FBYMSimulation::FImpl.
TMap<FString, FMapCell> MapBaseCells, MapCells;
TMap<FString, FString> MapYardCells;
TMap<FString, FMapPlayer> MapPlayers;
TMap<FString, TArray<FString>> MapOwned;
TMap<FString, FObject> MapTemplates;
TArray<int32> MapTerrain;
TSet<FString> OnlinePlayers, MapDirtySites;
TArray<FString> MapSiteCacheOrder;
bool bMapReady = false;
bool MapParticipant(const FString& Id) const { const FMapPlayer* P = MapPlayers.Find(Id); return P && P->bEntered; }
int32 TrainingScale(const FYard& Y) const { return MapParticipant(Y.Id) ? 3 : 1; }
bool LoadMapRules(FString& Error);
bool LoadMap(const FObject& Saved, FString& Error);
FObject SaveMap() const;
bool AddMapPlayer(const FString& Id, FString& Error, const FIntPoint* Destination = nullptr);
void RemoveMapPlayer(const FString& Id);
void ReindexMapOwners();
bool IsMapSite(const FString& YardId) const;
FMapCell* MapCellForYard(const FString& YardId);
const FMapCell* MapCellForYard(const FString& YardId) const;
FString MapGate(const FYard& Y) const;
FString MapRelocateReason(const FYard& Y) const;
FString RelocateMapPlayer(FYard& Y, const FObject& Params);
FString MapTemplateKey(const FMapCell& Cell) const;
bool EnsureMapSite(const FString& CellId, FString& Error);
FYard CreateMapSite(const FMapCell& Cell, const FObject& Template) const;
void AdvanceMapSite(FYard& Y, double End);
void AdvanceMapResources(FYard& Y, double End);
int64 MapStorage(const FString& Owner) const;
int64 MapProduction(const FString& Owner) const;
int32 MapDistanceBeyondRange(const FString& Owner, const FMapCell& Cell) const;
int32 MapStrongholdBonus(const FString& Owner, const FMapCell& Cell) const;
int32 MapFortificationReduction(const FMapCell& Cell) const;
double MapAttackBonus(const FBattle& Battle) const;
double MapTowerBonus(const FBattle& Battle) const;
double MapBuildingDamageMultiplier(const FBattle& Battle) const;
double MapDamagePercent(const FYard& Yard) const;
FString MapAttackReason(const FYard& Actor, const FMapCell& Cell) const;
FObject MapCellJSON(const FYard& Actor, const FMapCell& Cell, bool bSelected) const;
FObject MapView(const FYard& Actor, const FObject& Params) const;
FString ExecuteMap(FYard& Actor, const FString& Operation, const FObject& Params);
void FinishMapBattle(const FBattle& Battle, bool bWon, bool bContested);
bool BotMapAction(FYard& Actor);

bool TickMapStronghold(FBattle& Battle, FYard& Yard, FBuilding& Building, double Step);
void WriteMapCombat(const FBattle& Battle, const FObject& Object) const;
bool ReadMapCombat(FBattle& Battle, const FObject& Object, FString& Error);

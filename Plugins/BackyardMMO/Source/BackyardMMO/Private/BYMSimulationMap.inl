// Native, host-owned Map Room 3. Original source generation and layouts are
// bundled data; no web account/database service participates in this model.
namespace BYMMapState
{
    FString CellId(int32 X, int32 Y) { return FString::Printf(TEXT("cell-%d-%d"), X, Y); }
    FString ReplyWithView(bool bOk, const FString& Message, const FString& Yard = FString())
    {
        FObject Result = Obj(); Result->SetBoolField(TEXT("ok"), bOk);
        Result->SetStringField(bOk ? TEXT("message") : TEXT("error"), Message);
        if (bOk && !Yard.IsEmpty()) Result->SetStringField(TEXT("viewYardId"), Yard);
        return Encode(Result);
    }
    bool Position(const FObject& O, const TCHAR* Key, double Min, double Max, double& Value)
    { return O && O->TryGetNumberField(Key, Value) && FMath::IsFinite(Value) && Value >= Min && Value <= Max; }
    FString Name(const FMapCell& Cell)
    {
        if (Cell.Kind == BYMMapRoomRules::Resource) return TEXT("Resource Outpost");
        if (Cell.Kind == BYMMapRoomRules::Stronghold) return TEXT("Stronghold");
        if (Cell.Kind == BYMMapRoomRules::Fortification) return TEXT("Outpost Defender");
        if (Cell.Kind == BYMMapRoomRules::Tribe)
        {
            static const TCHAR* Names[] = {TEXT("Legionnaire"), TEXT("Kozu"), TEXT("Abunakki"), TEXT("Dreadnaught")};
            return FString(Names[FMath::Clamp(Cell.TribeIndex, 0, 3)]) + TEXT(" Outpost");
        }
        return Cell.Kind == BYMMapRoomRules::Player ? TEXT("Backyard") : TEXT("Terrain");
    }
    bool IsCapturable(int32 Kind)
    { return Kind == BYMMapRoomRules::Resource || Kind == BYMMapRoomRules::Stronghold || Kind == BYMMapRoomRules::Fortification; }
}

bool FBYMSimulation::FImpl::LoadMapRules(FString& Error)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
    FString Text; FObject Rules, World;
    if (!Plugin || !FFileHelper::LoadFileToString(Text, *FPaths::Combine(Plugin->GetContentDir(), TEXT("Data/maproom-rules.json"))) || !Decode(Text, Rules)
        || Number(Rules, TEXT("schema")) != 1)
    { Error = TEXT("Original Map Room rules are missing or invalid. Restore the full source content."); return false; }
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Plugin->GetContentDir(), TEXT("Data/maproom-world.json"))) || !Decode(Text, World)
        || Number(World, TEXT("schema")) != 1 || Number(World, TEXT("width")) != 500 || Number(World, TEXT("height")) != 500)
    { Error = TEXT("The original 500 by 500 Map Room terrain data is missing or invalid."); return false; }
    MapTemplates.Empty();
    // These three core buildings use their original structure levels and source
    // footprints. They remain unavailable in the ordinary construction catalog.
    for (const auto& Value : Array(Rules, TEXT("coreBuildings")))
    {
        const FObject Core = Value->AsObject(); FBuildingDef* D = Buildings.Find(static_cast<int32>(Number(Core, TEXT("id"))));
        if (!D) { Error = TEXT("A Map Room core building is missing."); return false; }
        const FArray& HP = Array(Core, TEXT("hp")); const FArray& Repair = Array(Core, TEXT("repairTime"));
        if (HP.IsEmpty() || HP.Num() != Repair.Num()) { Error = TEXT("Invalid Map Room core levels."); return false; }
        while (D->Levels.Num() < HP.Num()) D->Levels.Add(D->Levels.Last());
        for (int32 Index = 0; Index < HP.Num(); ++Index)
        {
            D->Levels[Index].HP = HP[Index]->AsNumber(); D->Levels[Index].RepairSeconds = Repair[Index]->AsNumber();
            const FArray& StatsArray = Array(Core, TEXT("stats"));
            if (StatsArray.IsValidIndex(Index))
            {
                const FObject S = StatsArray[Index]->AsObject();
                D->Levels[Index].Range = Number(S, TEXT("range")) / SourcePixelsPerCell;
                D->Levels[Index].Damage = Number(S, TEXT("damage"));
                D->Levels[Index].AttackInterval = Number(S, TEXT("rate"), 1); // GuardTower uses seconds.
            }
        }
        D->CombatRects.Empty(); FCombatRect Rect; Rect.Max = FVector2D(6.5, 6.5); D->CombatRects.Add(Rect);
    }
    for (const auto& Value : Array(Rules, TEXT("templates")))
    {
        const FObject Template = Value && Value->Type == EJson::Object ? Value->AsObject() : FObject();
        const FString Key = Str(Template, TEXT("key"));
        if (Key.IsEmpty() || MapTemplates.Contains(Key) || Array(Template, TEXT("buildings")).IsEmpty() || Array(Template, TEXT("buildings")).Num() > 1000)
        { Error = TEXT("An original Map Room yard template is invalid."); return false; }
        for (const auto& BV : Array(Template, TEXT("buildings")))
        {
            const FObject BO = BV && BV->Type == EJson::Object ? BV->AsObject() : FObject();
            const FBuildingDef* D = Buildings.Find(static_cast<int32>(Number(BO, TEXT("type"))));
            if (!D || Number(BO, TEXT("level"), 1) < 1 || Number(BO, TEXT("level"), 1) > D->Levels.Num())
            { Error = TEXT("A Map Room template refers to a missing building level."); return false; }
        }
        MapTemplates.Add(Key, Template);
    }
    if (MapTemplates.Num() != 26) { Error = TEXT("The full set of original Map Room templates is required."); return false; }
    MapTerrain.Init(0, 500 * 500); MapBaseCells.Empty();
    for (const auto& Value : Array(World, TEXT("cells")))
    {
        const FObject O = Value && Value->Type == EJson::Object ? Value->AsObject() : FObject();
        FMapCell Cell;
        if (!Integer(O, TEXT("x"), 0, 499, Cell.X) || !Integer(O, TEXT("y"), 0, 499, Cell.Y)
            || !Integer(O, TEXT("kind"), 0, 127, Cell.Kind) || !Integer(O, TEXT("terrain"), 0, 255, Cell.Terrain))
        { Error = TEXT("Invalid original Map Room cell."); return false; }
        Cell.Id = BYMMapState::CellId(Cell.X, Cell.Y); MapTerrain[Cell.Y * 500 + Cell.X] = Cell.Terrain;
        if (Cell.Kind == 0 || Cell.Kind == 100) continue;
        if (Cell.Kind != 1 && Cell.Kind != 102 && Cell.Kind != 103 && Cell.Kind != 104)
        { Error = TEXT("Unsupported original Map Room cell type."); return false; }
        if (!Integer(O, TEXT("level"), 1, 50, Cell.Level) || MapBaseCells.Contains(Cell.Id))
        { Error = TEXT("Duplicate or invalid original Map Room structure."); return false; }
        Cell.TribeIndex = static_cast<int32>(Number(O, TEXT("tribeIndex")));
        int32 ParentX = 0, ParentY = 0;
        if (Integer(O, TEXT("parentX"), 0, 499, ParentX) && Integer(O, TEXT("parentY"), 0, 499, ParentY)) Cell.Parent = BYMMapState::CellId(ParentX, ParentY);
        Cell.YardId = TEXT("map-") + Cell.Id;
        if (!MapTemplates.Contains(MapTemplateKey(Cell))) { Error = TEXT("A generated Map Room cell has no original yard template."); return false; }
        MapBaseCells.Add(Cell.Id, Cell);
    }
    MapCells = MapBaseCells; bMapReady = true; ReindexMapOwners(); return true;
}

FString FBYMSimulation::FImpl::MapTemplateKey(const FMapCell& Cell) const
{
    if (Cell.Kind == 102) return FString::Printf(TEXT("resources_%d"), Cell.Level);
    if (Cell.Kind == 103) return FString::Printf(TEXT("strongholds_%d"), Cell.Level);
    if (Cell.Kind == 104) return FString::Printf(TEXT("defenders_%d"), Cell.Level);
    static const TCHAR* Tribes[] = {TEXT("legionnaire"), TEXT("kozu"), TEXT("abunakki"), TEXT("dreadnaught")};
    return FString::Printf(TEXT("outposts_%s_%d"), Tribes[FMath::Clamp(Cell.TribeIndex, 0, 3)], Cell.Level);
}

void FBYMSimulation::FImpl::ReindexMapOwners()
{
    MapOwned.Empty(); MapYardCells.Empty();
    for (const auto& Pair : MapCells)
    {
        const FMapCell& Cell = Pair.Value;
        if (!Cell.Owner.IsEmpty()) MapOwned.FindOrAdd(Cell.Owner).Add(Cell.Id);
        if (!Cell.YardId.IsEmpty()) MapYardCells.Add(Cell.YardId, Cell.Id);
    }
    for (auto& Pair : MapOwned) Pair.Value.Sort();
}

FMapCell* FBYMSimulation::FImpl::MapCellForYard(const FString& YardId)
{ const FString* Id = MapYardCells.Find(YardId); return Id ? MapCells.Find(*Id) : nullptr; }
const FMapCell* FBYMSimulation::FImpl::MapCellForYard(const FString& YardId) const
{ const FString* Id = MapYardCells.Find(YardId); return Id ? MapCells.Find(*Id) : nullptr; }
bool FBYMSimulation::FImpl::IsMapSite(const FString& YardId) const
{ const FMapCell* Cell = MapCellForYard(YardId); return Cell && Cell->Kind != 101; }

bool FBYMSimulation::FImpl::AddMapPlayer(const FString& Id, FString& Error, const FIntPoint* Destination)
{
    if (MapPlayers.Contains(Id)) return true;
    if (!Yards.Contains(Id)) { Error = TEXT("Map owner does not have a main yard."); return false; }
    // Search every legal source cell in a deterministic, identity-specific
    // permutation. Minimum main-yard separation and seven-cell footprint apply
    // equally to accounts and rival bots. No captured structure is overwritten.
    const uint32 Start = GetTypeHash(Id) % (494U * 494U);
    FIntPoint Chosen(-1, -1); int32 BestDistance = TNumericLimits<int32>::Max();
    for (int32 Index = 0; Index < 494 * 494; ++Index)
    {
        const uint32 Flat = (Start + static_cast<uint32>(Index) * 7919U) % (494U * 494U);
        const FIntPoint Point(static_cast<int32>(Flat % 494) + 3, static_cast<int32>(Flat / 494) + 3);
        const int32 CandidateDistance = Destination ? BYMMapRoomRules::HexDistance(Point, *Destination) : 0;
        if (Destination && CandidateDistance >= BestDistance) continue;
        bool bAllowed = true;
        TArray<FIntPoint> Sector = BYMMapRoomRules::Neighbors(Point); Sector.Add(Point);
        for (const FIntPoint& P : Sector)
        {
            const FMapCell* Existing = MapCells.Find(BYMMapState::CellId(P.X, P.Y));
            if (!BYMMapRoomRules::IsValidCell(P) || (Existing && (Existing->Kind != 1 || !Existing->Owner.IsEmpty() || Busy(Existing->YardId)))) { bAllowed = false; break; }
        }
        if (!bAllowed) continue;
        for (const auto& Player : MapPlayers)
        {
            const FMapCell* Home = MapCells.Find(Player.Value.HomeCell);
            if (Home && BYMMapRoomRules::HexDistance(Point, FIntPoint(Home->X, Home->Y)) < 7) { bAllowed = false; break; }
        }
        if (!bAllowed) continue;
        Chosen = Point; BestDistance = CandidateDistance;
        if (!Destination || BestDistance == 0) break;
    }
    if (Chosen.X < 0) { Error = TEXT("No legal main-yard sector remains on this host's world map."); return false; }
    FMapCell Home; Home.X = Chosen.X; Home.Y = Chosen.Y; Home.Id = BYMMapState::CellId(Home.X, Home.Y);
    Home.Kind = 101; Home.Owner = Id; Home.YardId = Id; Home.Terrain = 5 + (Home.X * 73 + Home.Y * 31) % 45;
    // Placement can replace a neutral tribal sector. Remove its separate lazy
    // yard and provenance before the cell becomes a principal's main backyard.
    if (const FMapCell* Previous = MapCells.Find(Home.Id))
    { const FString PreviousYard = Previous->YardId; if (Previous->Kind != 101) Yards.Remove(PreviousYard); MapYardCells.Remove(PreviousYard); }
    MapDirtySites.Remove(Home.Id); MapSiteCacheOrder.Remove(Home.Id);
    MapCells.Add(Home.Id, Home);
    const int32 Levels[] = {5, 5, 10, 10, 15, 20}; int32 Direction = 0;
    for (const FIntPoint& P : BYMMapRoomRules::Neighbors(Chosen))
    {
        FMapCell Fort; Fort.X = P.X; Fort.Y = P.Y; Fort.Id = BYMMapState::CellId(P.X, P.Y);
        Fort.Kind = 104; Fort.Level = Levels[Direction++]; Fort.Parent = Home.Id; Fort.bHomeDefense = true;
        Fort.YardId = TEXT("map-") + Fort.Id; Fort.Terrain = 5 + (P.X * 73 + P.Y * 31) % 45;
        // Original surrounding defenders begin neutral and must be captured.
        Yards.Remove(Fort.YardId); MapDirtySites.Remove(Fort.Id); MapSiteCacheOrder.Remove(Fort.Id); MapCells.Add(Fort.Id, Fort);
    }
    FMapPlayer Player; Player.HomeCell = Home.Id; Player.ProductionAt = Now; MapPlayers.Add(Id, Player);
    MapOwned.FindOrAdd(Id).Add(Home.Id); MapYardCells.Add(Id, Home.Id);
    for (const FIntPoint& P : BYMMapRoomRules::Neighbors(Chosen)) { const FString Key = BYMMapState::CellId(P.X, P.Y); MapYardCells.Add(TEXT("map-") + Key, Key); }
    return true;
}

void FBYMSimulation::FImpl::RemoveMapPlayer(const FString& Id)
{
    const FMapPlayer* Player = MapPlayers.Find(Id); if (!Player) return;
    const FString HomeId = Player->HomeCell; TArray<FString> Remove;
    for (auto& Pair : MapCells)
    {
        FMapCell& Cell = Pair.Value;
        if (Cell.Id == HomeId || (Cell.bHomeDefense && Cell.Parent == HomeId)) Remove.Add(Cell.Id);
        else if (Cell.Owner == Id) { Cell.Owner.Reset(); ++Cell.Revision; }
    }
    for (const FString& CellId : Remove)
    {
        const FMapCell* Cell = MapCells.Find(CellId); if (Cell && Cell->Kind != 101) Yards.Remove(Cell->YardId);
        if (const FMapCell* Base = MapBaseCells.Find(CellId)) MapCells.Add(CellId, *Base); else MapCells.Remove(CellId);
    }
    MapPlayers.Remove(Id); OnlinePlayers.Remove(Id); ReindexMapOwners();
    for (auto& Pair : MapPlayers)
    { Pair.Value.Retaliations.Remove(Id); Pair.Value.Bookmarks.RemoveAll([this](const FString& Key) { return !MapCells.Contains(Key); }); }
}

FYard FBYMSimulation::FImpl::CreateMapSite(const FMapCell& Cell, const FObject& Template) const
{
    FYard Y; Y.Id = Cell.YardId; Y.Name = BYMMapState::Name(Cell); Y.LastEconomy = Now;
    Y.MushroomLastGrowth = Now; Y.ShieldUntil = Cell.ShieldUntil;
    double MinX = 0, MinY = 0, MaxX = 50, MaxY = 40;
    for (const auto& Value : Array(Template, TEXT("buildings")))
    {
        const FObject O = Value->AsObject(); FBuilding B;
        B.Id = TEXT("source-") + Str(O, TEXT("id")); B.Type = static_cast<int32>(Number(O, TEXT("type")));
        B.Level = static_cast<int32>(Number(O, TEXT("level"), 1)); B.TargetLevel = B.Level;
        B.Fortification = FMath::Clamp(static_cast<int32>(Number(Child(O, TEXT("source")), TEXT("fort"))), 0, 4);
        B.X = 25. + Number(O, TEXT("sourceX")) / SourcePixelsPerCell;
        B.Y = 20. + Number(O, TEXT("sourceY")) / SourcePixelsPerCell;
        const FLevel* L = Stats(B); const FBuildingDef* D = Buildings.Find(B.Type); if (!L || !D) continue;
        B.HP = L->HP;
        MinX = FMath::Min(MinX, B.X - 4.); MinY = FMath::Min(MinY, B.Y - 4.);
        double ExtentX = D->Cells, ExtentY = D->Cells;
        for (const FCombatRect& R : D->CombatRects) { ExtentX = FMath::Max(ExtentX, R.Max.X); ExtentY = FMath::Max(ExtentY, R.Max.Y); }
        MaxX = FMath::Max(MaxX, B.X + ExtentX + 4.); MaxY = FMath::Max(MaxY, B.Y + ExtentY + 4.);
        Y.Buildings.Add(MoveTemp(B));
    }
    Y.MapMinX = static_cast<int32>(std::floor(MinX)); Y.MapMinY = static_cast<int32>(std::floor(MinY));
    Y.MapWidth = static_cast<int32>(std::ceil(MaxX)) - Y.MapMinX;
    Y.MapHeight = static_cast<int32>(std::ceil(MaxY)) - Y.MapMinY;
    for (const auto& Pair : Monsters) Y.MonsterLevels.Add(Pair.Key, 1);
    const FObject Stock = Child(Template, TEXT("monsters"));
    if (Stock) for (const auto& Pair : Stock->Values)
    {
        const FString MonsterId(*Pair.Key);
        int32 Count = 0; if (Monsters.Contains(MonsterId) && Integer(Stock, *MonsterId, 1, 100000, Count)) Y.Army.Add(MonsterId, Count);
    }
    const FObject ResourcesObject = Child(Template, TEXT("resources"));
    for (int32 Index = 0; Index < 4; ++Index) Y.Resource[Index] = static_cast<int64>(FMath::Clamp(Number(ResourcesObject, *FString::Printf(TEXT("r%d"), Index + 1)), 0., ResourceLimit));
    return Y;
}

bool FBYMSimulation::FImpl::EnsureMapSite(const FString& CellId, FString& Error)
{
    const FMapCell* Cell = MapCells.Find(CellId);
    if (!Cell || Cell->Kind == 101 || Cell->DestroyedUntil > Now) { Error = TEXT("This cell has no available territory yard."); return false; }
    if (Cell->Kind == 1 && Cell->DestroyedUntil > 0 && Cell->DestroyedUntil <= Now)
    { FMapCell& Mutable = MapCells.FindChecked(CellId); Mutable.DestroyedUntil = 0; ++Mutable.Revision; Yards.Remove(Mutable.YardId); MapDirtySites.Remove(CellId); }
    MapSiteCacheOrder.Remove(CellId); MapSiteCacheOrder.Add(CellId);
    while (MapSiteCacheOrder.Num() > 256)
    {
        const FString OldId = MapSiteCacheOrder[0]; MapSiteCacheOrder.RemoveAt(0);
        const FMapCell* Old = MapCells.Find(OldId);
        if (Old && Old->Owner.IsEmpty() && !MapDirtySites.Contains(OldId) && !Busy(Old->YardId)) Yards.Remove(Old->YardId);
    }
    if (Yards.Contains(Cell->YardId)) return true;
    const FObject* Template = MapTemplates.Find(MapTemplateKey(*Cell));
    if (!Template) { Error = TEXT("The original territory layout is unavailable."); return false; }
    FYard Y = CreateMapSite(*Cell, *Template);
    if (Y.Buildings.IsEmpty()) { Error = TEXT("The original territory layout is empty."); return false; }
    const FString SiteYardId = Y.Id; Yards.Add(SiteYardId, MoveTemp(Y)); return true;
}

void FBYMSimulation::FImpl::AdvanceMapSite(FYard& Y, double End)
{
    const double Elapsed = FMath::Clamp(End - Y.LastEconomy, 0., 31536000.);
    for (FBuilding& B : Y.Buildings) if (B.bRepairing)
    {
        const FLevel* L = Stats(B); if (!L) continue;
        B.HP = FMath::Min(L->HP, B.HP + BYMEconomy::RepairRate(L->HP, L->RepairSeconds, ConstructionMultiplier()) * Elapsed);
        if (B.HP >= L->HP) B.bRepairing = false;
    }
    Y.LastEconomy = FMath::Max(Y.LastEconomy, End);
}

int64 FBYMSimulation::FImpl::MapStorage(const FString& Owner) const
{
    int64 Capacity = 0; const auto* Owned = MapOwned.Find(Owner); if (!Owned) return 0;
    for (const FString& Id : *Owned) { const FMapCell* Cell = MapCells.Find(Id); if (Cell && Cell->Kind == 102) Capacity += BYMMapRoomRules::ResourceCapacity(Cell->Level); }
    return FMath::Min<int64>(Capacity, static_cast<int64>(ResourceLimit));
}
int64 FBYMSimulation::FImpl::MapProduction(const FString& Owner) const
{
    int64 Rate = 0; const auto* Owned = MapOwned.Find(Owner); if (!Owned) return 0;
    for (const FString& Id : *Owned) { const FMapCell* Cell = MapCells.Find(Id); if (Cell && Cell->Kind == 102) Rate += BYMMapRoomRules::ResourceRate(Cell->Level); }
    return Rate;
}
void FBYMSimulation::FImpl::AdvanceMapResources(FYard& Y, double End)
{
    FMapPlayer* Player = MapPlayers.Find(Y.Id); if (!Player || End <= Player->ProductionAt) return;
    const int64 Rate = MapProduction(Y.Id);
    const double Credit = Rate * FMath::Min(31536000., End - Player->ProductionAt) + Player->ProductionFraction;
    const double Whole = std::floor(Credit + 1.e-8);
    const int64 Amount = static_cast<int64>(FMath::Min(ResourceLimit, Whole));
    Player->ProductionFraction = FMath::Clamp(Credit - Whole, 0., .999999999999);
    Player->ProductionAt = End;
    if (Amount <= 0) return;
    const int64 Capacity = Storage(Y);
    for (int32 Index = 0; Index < 4; ++Index) Y.Resource[Index] += FMath::Min(Amount, FMath::Max<int64>(0, Capacity - Y.Resource[Index]));
}

FString FBYMSimulation::FImpl::MapGate(const FYard& Y) const
{
    if (!bMapReady || !MapPlayers.Contains(Y.Id)) return TEXT("The host world map is unavailable.");
    if (AttainedLevel(Y, 14) < 6) return TEXT("Upgrade your Town Hall to level 6 to enter the World Map.");
    for (const FBuilding& B : Y.Buildings) if (B.Type == 11 && B.Level >= 2 && !HasWork(B))
    { const FLevel* L = Stats(B); if (L && B.HP >= L->HP * .5) return FString(); }
    return TEXT("Complete a level 2 Map Room and repair it to at least half health to enter the World Map.");
}

FString FBYMSimulation::FImpl::MapRelocateReason(const FYard& Y) const
{
    const FString Gate = MapGate(Y); if (!Gate.IsEmpty()) return Gate;
    if (Busy(Y.Id)) return TEXT("Finish your current battle before relocating.");
    const FMapPlayer* Player = MapPlayers.Find(Y.Id); const FMapCell* Home = Player ? MapCells.Find(Player->HomeCell) : nullptr;
    if (!Home) return TEXT("Your current map position is unavailable.");
    for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(Home->X, Home->Y)))
    { const FMapCell* Fort = MapCells.Find(BYMMapState::CellId(P.X, P.Y)); if (Fort && Busy(Fort->YardId)) return TEXT("A surrounding fortification is in battle. Relocate after it finishes."); }
    return FString();
}

FString FBYMSimulation::FImpl::RelocateMapPlayer(FYard& Y, const FObject& Params)
{
    const FString Reason = MapRelocateReason(Y); if (!Reason.IsEmpty()) return Reply(false, Reason);
    const FString OwnerId = Y.Id; const FMapPlayer PriorPlayer = MapPlayers.FindChecked(OwnerId);
    if (Str(Params, TEXT("mode")) != TEXT("random") || Str(Params, TEXT("expectedHome")) != PriorPlayer.HomeCell)
        return Reply(false, TEXT("Your map position changed. Refresh and confirm random relocation again."));
    // Choose and validate the complete destination BEFORE removing any saved territory.
    const uint32 Start = (GetTypeHash(OwnerId) + static_cast<uint32>(FMath::Fmod(Now, 1000000000.))) % (494U * 494U);
    FIntPoint Destination(-1, -1);
    for (int32 Index = 0; Index < 494 * 494; ++Index)
    {
        const uint32 Flat = (Start + static_cast<uint32>(Index) * 7919U) % (494U * 494U);
        const FIntPoint P(static_cast<int32>(Flat % 494) + 3, static_cast<int32>(Flat / 494) + 3);
        bool Legal = true; TArray<FIntPoint> Sector = BYMMapRoomRules::Neighbors(P); Sector.Add(P);
        for (const FIntPoint& S : Sector)
        { const FMapCell* Existing = MapCells.Find(BYMMapState::CellId(S.X, S.Y)); if (Existing && (Existing->Kind != 1 || !Existing->Owner.IsEmpty() || Busy(Existing->YardId))) { Legal = false; break; } }
        if (!Legal) continue;
        for (const auto& Pair : MapPlayers)
        { const FMapCell& Home = MapCells.FindChecked(Pair.Value.HomeCell); if (BYMMapRoomRules::HexDistance(P, FIntPoint(Home.X, Home.Y)) < 7) { Legal = false; break; } }
        if (Legal) { Destination = P; break; }
    }
    if (Destination.X < 0) return Reply(false, TEXT("No legal relocation sector is available. Your yard and territories have not changed."));
    const FMapCell OldHome = MapCells.FindChecked(PriorPlayer.HomeCell); TArray<FString> OldCells;
    OldCells.Add(OldHome.Id); for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(OldHome.X, OldHome.Y))) OldCells.Add(BYMMapState::CellId(P.X, P.Y));
    // AddMapPlayer is deterministic for an exact valid destination. Retain the old
    // cells until allocation succeeds, making a full-map failure non-destructive.
    MapPlayers.Remove(OwnerId); FString Error;
    if (!AddMapPlayer(OwnerId, Error, &Destination)) { MapPlayers.Add(OwnerId, PriorPlayer); return Reply(false, Error); }
    FMapPlayer& Player = MapPlayers.FindChecked(OwnerId); const FString NewHome = Player.HomeCell;
    Player = PriorPlayer; Player.HomeCell = NewHome; Player.RelocatedAt = Now; Player.bEntered = true;
    for (const FString& Key : OldCells)
    {
        const FMapCell* Old = MapCells.Find(Key); if (Old && Old->Kind != 101) Yards.Remove(Old->YardId);
        MapDirtySites.Remove(Key); MapSiteCacheOrder.Remove(Key);
        if (const FMapCell* Original = MapBaseCells.Find(Key)) MapCells.Add(Key, *Original); else MapCells.Remove(Key);
    }
    ReindexMapOwners();
    for (auto& Pair : MapPlayers) Pair.Value.Bookmarks.RemoveAll([this](const FString& Key) { return !MapCells.Contains(Key); });
    return BYMMapState::ReplyWithView(true, TEXT("Backyard relocated. Your resource outposts and strongholds remain yours; the old surrounding fortifications have been reset."), OwnerId);
}

int32 FBYMSimulation::FImpl::MapDistanceBeyondRange(const FString& Owner, const FMapCell& Cell) const
{
    int32 Distance = 1000; const auto* Owned = MapOwned.Find(Owner); if (!Owned) return Distance;
    for (const FString& Id : *Owned)
    {
        const FMapCell* Source = MapCells.Find(Id); if (!Source) continue;
        const int32 Range = BYMMapRoomRules::Range(Source->Kind, Source->Level); if (Range <= 0) continue;
        Distance = FMath::Min(Distance, FMath::Max(0, BYMMapRoomRules::HexDistance(FIntPoint(Source->X, Source->Y), FIntPoint(Cell.X, Cell.Y)) - Range));
    }
    return Distance;
}
int32 FBYMSimulation::FImpl::MapStrongholdBonus(const FString& Owner, const FMapCell& Cell) const
{
    int32 Bonus = 0; const auto* Owned = MapOwned.Find(Owner); if (!Owned) return 0;
    for (const FString& Id : *Owned)
    {
        const FMapCell* Source = MapCells.Find(Id);
        if (Source && Source->Kind == 103 && BYMMapRoomRules::HexDistance(FIntPoint(Source->X, Source->Y), FIntPoint(Cell.X, Cell.Y)) <= BYMMapRoomRules::Range(Source->Kind, Source->Level))
            Bonus += BYMMapRoomRules::StrongholdBonus(Source->Level);
    }
    return Bonus;
}
int32 FBYMSimulation::FImpl::MapFortificationReduction(const FMapCell& Cell) const
{
    if (Cell.Kind != 101 && Cell.Kind != 102 && Cell.Kind != 103) return 0;
    if (Cell.Owner.IsEmpty()) return 0; // Supplied server does not grant UID-zero tribal armor.
    int32 Count = 0;
    for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(Cell.X, Cell.Y)))
    { const FMapCell* Fort = MapCells.Find(BYMMapState::CellId(P.X, P.Y)); if (Fort && Fort->Kind == 104 && Fort->Owner == Cell.Owner) ++Count; }
    return BYMMapRoomRules::DamageReduction(Count);
}
double FBYMSimulation::FImpl::MapAttackBonus(const FBattle& Battle) const
{ const FMapCell* Cell = MapCells.Find(Battle.MapCellId); return Cell ? 1. + MapStrongholdBonus(Battle.Attacker, *Cell) * .01 : 1.; }
double FBYMSimulation::FImpl::MapTowerBonus(const FBattle& Battle) const
{ const FMapCell* Cell = MapCells.Find(Battle.MapCellId); return Cell ? 1. + MapStrongholdBonus(Cell->Owner, *Cell) * .01 : 1.; }
double FBYMSimulation::FImpl::MapBuildingDamageMultiplier(const FBattle& Battle) const
{ const FMapCell* Cell = MapCells.Find(Battle.MapCellId); return MapAttackBonus(Battle) * (Cell ? 1. - MapFortificationReduction(*Cell) * .01 : 1.); }
double FBYMSimulation::FImpl::MapDamagePercent(const FYard& Yard) const
{
    double Maximum = 0, Remaining = 0;
    for (const FBuilding& B : Yard.Buildings)
    {
        const FBuildingDef* D = Buildings.Find(B.Type); const FLevel* L = Stats(B);
        if (!D || !L || B.Level <= 0 || D->Type == TEXT("wall") || B.Type == 7 || (D->Type == TEXT("trap") && B.HP <= 0)) continue;
        Maximum += L->HP; Remaining += FMath::Clamp(B.HP, 0., L->HP);
    }
    return BYMMapRoomRules::CapturePercent(Remaining, Maximum);
}

FString FBYMSimulation::FImpl::MapAttackReason(const FYard& Actor, const FMapCell& Cell) const
{
    const FString Gate = MapGate(Actor); if (!Gate.IsEmpty()) return Gate;
    if (Cell.Kind != 1 && Cell.Kind != 101 && !BYMMapState::IsCapturable(Cell.Kind)) return TEXT("There is no yard to attack on this cell.");
    if (Cell.Owner == Actor.Id) return TEXT("This territory already belongs to you.");
    if (Cell.DestroyedUntil > Now) return TEXT("This wild outpost is regenerating.");
    if (Cell.ShieldUntil > Now) return TEXT("This territory is protected by a defense shield.");
    const FYard* Defender = Yards.Find(Cell.YardId);
    const int32 AttackerLevel = BYMExperience::Progress(Actor.EarnedExperience, Actor.BaseValue).Level;
    if (Cell.Kind == 101 && OnlinePlayers.Contains(Cell.Owner)) return TEXT("This backyard's player is currently online.");
    if (Cell.Kind == 101 && Defender)
    {
        const int32 DefenderLevel = BYMExperience::Progress(Defender->EarnedExperience, Defender->BaseValue).Level;
        const FMapPlayer* Participant = MapPlayers.Find(Actor.Id);
        if (AttackerLevel - DefenderLevel > 11 && !(AttackerLevel >= 40 && DefenderLevel >= 40) && (!Participant || Participant->Retaliations.FindRef(Cell.Owner) <= 0)) return TEXT("This backyard is more than 11 experience levels below you.");
    }
    if (Cell.Kind == 102 && AttackerLevel >= 32 && Cell.Level <= 20) return TEXT("Players level 32 or higher cannot attack level 10 or 20 resource outposts.");
    if (Defender && Defender->ShieldUntil > Now) return TEXT("This yard is protected by a defense shield.");
    if (Busy(Actor.Id) || Busy(Cell.YardId)) return TEXT("One of these yards is already in a battle.");
    if (Battles.Num() >= BattleLimit()) return TEXT("All battle slots are busy. Try again shortly.");
    if (ArmyCount(Actor) < 1 || LevelOf(Actor, 5) < 1) return TEXT("Train an army and repair or complete your Flinger before attacking.");
    const int64 Cost = BYMMapRoomRules::AttackCost(MapDistanceBeyondRange(Actor.Id, Cell), AttainedLevel(Actor, 14));
    for (int32 Index = 0; Index < 3; ++Index) if (Actor.Resource[Index] < Cost) return TEXT("Not enough Twigs, Pebbles or Putty for the distance cost.");
    return FString();
}

FObject FBYMSimulation::FImpl::MapCellJSON(const FYard& Actor, const FMapCell& Cell, bool bSelected) const
{
    FObject O = Obj(); const FYard* Owner = Yards.Find(Cell.Owner); const FYard* Yard = Yards.Find(Cell.YardId);
    const bool bRegeneratedTribe = Cell.Kind == 1 && Cell.DestroyedUntil > 0 && Cell.DestroyedUntil <= Now;
    const double DamagePercent = Yard && !bRegeneratedTribe ? MapDamagePercent(*Yard) : 0.;
    O->SetStringField(TEXT("id"), Cell.Id); O->SetNumberField(TEXT("x"), Cell.X); O->SetNumberField(TEXT("y"), Cell.Y);
    O->SetNumberField(TEXT("kind"), Cell.Kind); O->SetNumberField(TEXT("level"), Cell.Kind == 101 && Yard ? BYMExperience::Progress(Yard->EarnedExperience, Yard->BaseValue).Level : Cell.Level);
    O->SetStringField(TEXT("name"), Cell.Kind == 101 && Yard ? Yard->Name : BYMMapState::Name(Cell));
    O->SetStringField(TEXT("ownerId"), Cell.Owner); O->SetStringField(TEXT("ownerName"), Owner ? Owner->Name : TEXT("Wild Monsters"));
    O->SetBoolField(TEXT("ownerBot"), Owner && Owner->bBot); O->SetStringField(TEXT("yardId"), Cell.YardId);
    O->SetNumberField(TEXT("terrain"), Cell.Terrain); O->SetBoolField(TEXT("owned"), Cell.Owner == Actor.Id);
    O->SetNumberField(TEXT("range"), BYMMapRoomRules::Range(Cell.Kind, Cell.Level));
    O->SetBoolField(TEXT("inRange"), MapDistanceBeyondRange(Actor.Id, Cell) == 0);
    O->SetNumberField(TEXT("shieldUntil"), FMath::Max(Cell.ShieldUntil, Yard ? Yard->ShieldUntil : 0.));
    O->SetNumberField(TEXT("destroyedUntil"), Cell.DestroyedUntil); O->SetNumberField(TEXT("attackRevision"), Cell.Revision);
    O->SetBoolField(TEXT("requiresMapAttack"), Cell.Kind != 101 || MapParticipant(Actor.Id) || MapParticipant(Cell.Owner) || MapGate(Actor).IsEmpty() || (Owner && MapGate(*Owner).IsEmpty()));
    O->SetBoolField(TEXT("isDestroyed"), Cell.DestroyedUntil > Now);
    O->SetNumberField(TEXT("damagePercent"), DamagePercent);
    const int32 PlayerBuff = MapStrongholdBonus(Actor.Id, Cell), EnemyBuff = Cell.Owner == Actor.Id ? 0 : MapStrongholdBonus(Cell.Owner, Cell);
    O->SetStringField(TEXT("buffEffect"), PlayerBuff > 0 ? (EnemyBuff > 0 ? TEXT("mixed") : TEXT("player")) : (EnemyBuff > 0 ? TEXT("enemy") : TEXT("")));
    O->SetStringField(TEXT("parentId"), Cell.Parent);
    const FMapCell* DisplayParent = nullptr;
    if (Cell.Kind == 104) for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(Cell.X, Cell.Y)))
    {
        const FMapCell* Neighbor = MapCells.Find(BYMMapState::CellId(P.X, P.Y));
        if (Neighbor && (Neighbor->Kind == 101 || Neighbor->Kind == 102 || Neighbor->Kind == 103) && Neighbor->Owner == Cell.Owner
            && (!Cell.Owner.IsEmpty() || (Neighbor->X + Neighbor->Y) % 4 == (Cell.X + Cell.Y) % 4))
        { DisplayParent = Neighbor; break; }
    }
    O->SetBoolField(TEXT("fortifiesParent"), DisplayParent != nullptr);
    if (!DisplayParent) DisplayParent = MapCells.Find(Cell.Parent);
    if (DisplayParent) { O->SetNumberField(TEXT("parentX"), DisplayParent->X); O->SetNumberField(TEXT("parentY"), DisplayParent->Y); }
    const int32 Reduction = MapFortificationReduction(Cell);
    O->SetNumberField(TEXT("damageReduction"), Reduction);
    int32 Forts = 0;
    if (!Cell.Owner.IsEmpty()) for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(Cell.X, Cell.Y)))
    { const FMapCell* Fort = MapCells.Find(BYMMapState::CellId(P.X, P.Y)); if (Fort && Fort->Kind == 104 && Fort->Owner == Cell.Owner) ++Forts; }
    O->SetNumberField(TEXT("fortificationsOwned"), Forts); O->SetBoolField(TEXT("fullyFortified"), Forts == 6);
    O->SetNumberField(TEXT("resourceCapacity"), Cell.Kind == 102 ? static_cast<double>(BYMMapRoomRules::ResourceCapacity(Cell.Level)) : 0.);
    O->SetNumberField(TEXT("productionPerSecond"), Cell.Kind == 102 ? static_cast<double>(BYMMapRoomRules::ResourceRate(Cell.Level)) : 0.);
    O->SetNumberField(TEXT("monsterBonus"), Cell.Kind == 103 ? BYMMapRoomRules::StrongholdBonus(Cell.Level) : MapStrongholdBonus(Actor.Id, Cell));
    O->SetNumberField(TEXT("towerBonus"), Cell.Kind == 103 ? BYMMapRoomRules::StrongholdBonus(Cell.Level) : MapStrongholdBonus(Cell.Owner, Cell));
    const FString Gate = MapGate(Actor);
    const bool bSite = Cell.Kind == 1 || Cell.Kind == 101 || BYMMapState::IsCapturable(Cell.Kind);
    O->SetBoolField(TEXT("canScout"), Gate.IsEmpty() && bSite && Cell.DestroyedUntil <= Now);
    O->SetBoolField(TEXT("canEnter"), Gate.IsEmpty() && bSite && Cell.DestroyedUntil <= Now);
    if (bSelected)
    {
        const FString Reason = MapAttackReason(Actor, Cell); O->SetBoolField(TEXT("canAttack"), Reason.IsEmpty()); O->SetStringField(TEXT("reason"), Reason);
        const int64 Cost = BYMMapRoomRules::AttackCost(MapDistanceBeyondRange(Actor.Id, Cell), AttainedLevel(Actor, 14));
        const int64 Costs[] = {Cost, Cost, Cost, 0}; O->SetObjectField(TEXT("attackCost"), ResourceObject(Costs));
        O->SetNumberField(TEXT("distanceBeyondRange"), MapDistanceBeyondRange(Actor.Id, Cell));
        O->SetBoolField(TEXT("capturable"), BYMMapState::IsCapturable(Cell.Kind));
        O->SetNumberField(TEXT("destructionPercent"), DamagePercent);
        const FMapPlayer* Player = MapPlayers.Find(Actor.Id);
        O->SetNumberField(TEXT("retaliationsOwed"), Player ? Player->Retaliations.FindRef(Cell.Owner) : 0);
        O->SetBoolField(TEXT("bookmarked"), Player && Player->Bookmarks.Contains(Cell.Id));
        const bool bManage = Cell.Owner == Actor.Id && BYMMapState::IsCapturable(Cell.Kind) && !Busy(Cell.YardId) && !Busy(Actor.Id);
        O->SetBoolField(TEXT("canRepair"), bManage); O->SetBoolField(TEXT("canMove"), bManage);
        O->SetBoolField(TEXT("canAbandon"), bManage); O->SetStringField(TEXT("manageReason"), bManage ? FString() : TEXT("Manage your own territory outside a battle."));
        O->SetBoolField(TEXT("canRelocate"), false); // Set by the exact relocation flow, never a fake action.
        O->SetStringField(TEXT("captureRule"), Cell.Kind == 101 ? TEXT("Backyards cannot be captured.") : (Cell.Kind == 1 ? TEXT("Destroy this tribe outpost; it regenerates after three days.") : TEXT("Deal at least 90% non-wall building damage to capture this territory.")));
    }
    return O;
}

FObject FBYMSimulation::FImpl::MapView(const FYard& Actor, const FObject& Params) const
{
    FObject Result = Obj(); Result->SetBoolField(TEXT("ok"), true); FObject Map = Obj(); Result->SetObjectField(TEXT("map"), Map);
    Map->SetNumberField(TEXT("width"), 500); Map->SetNumberField(TEXT("height"), 500); Map->SetStringField(TEXT("selfId"), Actor.Id);
    Map->SetNumberField(TEXT("now"), Now); const FString Gate = MapGate(Actor);
    Map->SetBoolField(TEXT("available"), Gate.IsEmpty()); Map->SetStringField(TEXT("reason"), Gate);
    const FString RelocateReason = MapRelocateReason(Actor); Map->SetBoolField(TEXT("canRelocate"), RelocateReason.IsEmpty()); Map->SetStringField(TEXT("relocateReason"), RelocateReason);
    const FMapPlayer* Player = MapPlayers.Find(Actor.Id); const FMapCell* Home = Player ? MapCells.Find(Player->HomeCell) : nullptr;
    Map->SetStringField(TEXT("homeCellId"), Home ? Home->Id : FString());
    Map->SetNumberField(TEXT("homeX"), Home ? Home->X : 250); Map->SetNumberField(TEXT("homeY"), Home ? Home->Y : 250);
    int32 X = Home ? Home->X : 250, Y = Home ? Home->Y : 250, Radius = 12;
    int32 InputX = -1, InputY = -1;
    if (Params->HasField(TEXT("x")) && !Integer(Params, TEXT("x"), -1, 499, InputX)) return FObject();
    if (Params->HasField(TEXT("y")) && !Integer(Params, TEXT("y"), -1, 499, InputY)) return FObject();
    if ((InputX < 0) != (InputY < 0)) return FObject();
    if (InputX >= 0) { X = InputX; Y = InputY; }
    if (Params->HasField(TEXT("radius")) && !Integer(Params, TEXT("radius"), 1, 100, Radius)) return FObject();
    Radius = FMath::Clamp(Radius, 4, 15); Map->SetNumberField(TEXT("centerX"), X); Map->SetNumberField(TEXT("centerY"), Y); Map->SetNumberField(TEXT("radius"), Radius);
    int32 OwnedPage = 0;
    if (Params->HasField(TEXT("ownedPage")) && !Integer(Params, TEXT("ownedPage"), 0, 5000, OwnedPage)) return FObject();
    const auto* OwnedIds = MapOwned.Find(Actor.Id); const int32 OwnedTotal = OwnedIds ? OwnedIds->Num() : 0;
    const int32 OwnedPages = FMath::Max(1, (OwnedTotal + 49) / 50); OwnedPage = FMath::Min(OwnedPage, OwnedPages - 1);
    Map->SetNumberField(TEXT("ownedPage"), OwnedPage); Map->SetNumberField(TEXT("ownedPages"), OwnedPages); Map->SetNumberField(TEXT("ownedTotal"), OwnedTotal);
    Map->SetBoolField(TEXT("participant"), MapParticipant(Actor.Id));
    FArray Cells, Owned, Bookmarks;
    if (Gate.IsEmpty())
    {
        for (int32 Row = FMath::Max(0, Y - Radius); Row <= FMath::Min(499, Y + Radius); ++Row)
            for (int32 Column = FMath::Max(0, X - Radius); Column <= FMath::Min(499, X + Radius); ++Column)
            {
                const FString Key = BYMMapState::CellId(Column, Row); const FMapCell* Cell = MapCells.Find(Key);
                if (Cell) Cells.Add(MakeShared<FJsonValueObject>(MapCellJSON(Actor, *Cell, false)));
                else
                {
                    FObject Terrain = Obj(); Terrain->SetStringField(TEXT("id"), Key); Terrain->SetNumberField(TEXT("x"), Column); Terrain->SetNumberField(TEXT("y"), Row);
                    Terrain->SetNumberField(TEXT("kind"), 100); Terrain->SetNumberField(TEXT("terrain"), MapTerrain[Row * 500 + Column]);
                    Cells.Add(MakeShared<FJsonValueObject>(Terrain));
                }
            }
        if (OwnedIds) for (int32 Index = OwnedPage * 50; Index < FMath::Min(OwnedTotal, (OwnedPage + 1) * 50); ++Index) if (const FMapCell* Cell = MapCells.Find((*OwnedIds)[Index])) Owned.Add(MakeShared<FJsonValueObject>(MapCellJSON(Actor, *Cell, false)));
        if (Player) for (const FString& Key : Player->Bookmarks) if (const FMapCell* Cell = MapCells.Find(Key)) Bookmarks.Add(MakeShared<FJsonValueObject>(MapCellJSON(Actor, *Cell, false)));
        const FString Selected = Str(Params, TEXT("selected"), Home ? Home->Id : FString());
        if (const FMapCell* Cell = MapCells.Find(Selected)) Map->SetObjectField(TEXT("selected"), MapCellJSON(Actor, *Cell, true));
    }
    Map->SetArrayField(TEXT("cells"), Cells); Map->SetArrayField(TEXT("ownedCells"), Owned); Map->SetArrayField(TEXT("bookmarks"), Bookmarks);
    Map->SetNumberField(TEXT("resourceProductionPerSecond"), static_cast<double>(MapProduction(Actor.Id)));
    Map->SetNumberField(TEXT("resourceCapacity"), static_cast<double>(MapStorage(Actor.Id)));
    return Result;
}

FString FBYMSimulation::FImpl::ExecuteMap(FYard& Actor, const FString& Operation, const FObject& Params)
{
    if (Operation == TEXT("mapView"))
    {
        FObject Result = MapView(Actor, Params);
        if (Result && MapGate(Actor).IsEmpty()) { MapPlayers.FindChecked(Actor.Id).bEntered = true; Child(Result, TEXT("map"))->SetBoolField(TEXT("participant"), true); }
        return Result ? Encode(Result) : Reply(false, TEXT("Invalid map viewport coordinates or radius."));
    }
    const FString Gate = MapGate(Actor); if (!Gate.IsEmpty()) return Reply(false, Gate);
    if (Operation == TEXT("mapRelocate")) return RelocateMapPlayer(Actor, Params);
    const FString ActorId = Actor.Id, CellId = Str(Params, TEXT("cell")); FMapCell* Cell = MapCells.Find(CellId);
    if (!Cell) return Reply(false, TEXT("Choose a valid world-map territory."));
    if (Operation == TEXT("mapBookmark"))
    {
        bool bEnabled = false; if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled)) return Reply(false, TEXT("Invalid bookmark selection."));
        FMapPlayer& Player = MapPlayers.FindChecked(ActorId);
        if (bEnabled && !Player.Bookmarks.Contains(CellId))
        { if (Player.Bookmarks.Num() >= 100) return Reply(false, TEXT("The bookmark list is full.")); Player.Bookmarks.Add(CellId); }
        if (!bEnabled) Player.Bookmarks.Remove(CellId);
        return Reply(true, bEnabled ? TEXT("Territory bookmarked.") : TEXT("Bookmark removed."));
    }
    if (Operation == TEXT("mapInspect"))
    {
        if (Cell->DestroyedUntil > Now) return Reply(false, TEXT("This wild outpost is regenerating."));
        FString Error; if (Cell->Kind != 101 && !EnsureMapSite(CellId, Error)) return Reply(false, Error);
        return BYMMapState::ReplyWithView(true, TEXT("Territory selected."), Cell->YardId);
    }
    if (Operation == TEXT("mapInvade"))
    {
        const FString Reason = MapAttackReason(Actor, *Cell); if (!Reason.IsEmpty()) return Reply(false, Reason);
        int32 Revision = 0; FString ExpectedOwner;
        if (!Integer(Params, TEXT("expectedRevision"), 0, 1000000000, Revision) || Revision != Cell->Revision
            || !Params->TryGetStringField(TEXT("expectedOwner"), ExpectedOwner) || ExpectedOwner != Cell->Owner)
            return Reply(false, TEXT("Territory ownership changed. Refresh the map and confirm again."));
        const int64 Price = BYMMapRoomRules::AttackCost(MapDistanceBeyondRange(ActorId, *Cell), AttainedLevel(Actor, 14));
        int64 Quoted[4] = {0, 0, 0, 0};
        if (!ReadResources(Child(Params, TEXT("maxCost")), Quoted) || Quoted[3] != 0)
            return Reply(false, TEXT("Confirm the host's current attack cost."));
        for (int32 Index = 0; Index < 3; ++Index) if (Price > Quoted[Index]) return Reply(false, TEXT("The attack cost increased. Refresh and confirm the new quote."));
        const FString DefenderId = Cell->YardId;
        FString Error; if (Cell->Kind != 101 && !EnsureMapSite(CellId, Error)) return Reply(false, Error);
        // Creating a lazy site may reallocate the yard map; reacquire both sides.
        FYard* Attacker = Yards.Find(ActorId); FYard* Defender = Yards.Find(DefenderId);
        if (!Attacker || !Defender) return Reply(false, TEXT("The selected territory yard is unavailable."));
        Advance(*Defender); for (int32 Index = 0; Index < 3; ++Index) Attacker->Resource[Index] -= Price;
        FBattle Battle; Battle.Id = NewId(); Battle.Attacker = ActorId; Battle.Defender = DefenderId; Battle.MapCellId = CellId;
        Battle.StartedAt = Now; Battle.SimulatedAt = Now; Battle.EndsAt = Now + BattleSeconds(); Battle.InitialBuildingHP = 0;
        for (const FBuilding& B : Defender->Buildings) if (IsCombatObjective(B))
        { const FLevel* L = Stats(B); if (L) Battle.InitialBuildingHP += L->HP; if (B.HP > 0) ++Battle.InitialBuildings; }
        if (Battle.InitialBuildingHP <= 0) Battle.InitialBuildingHP = 1;
        Battle.InitialBuildings = FMath::Max(1, Battle.InitialBuildings);
        MapPlayers.FindChecked(ActorId).bEntered = true; MapDirtySites.Add(CellId);
        if (Cell->Kind == 101)
        {
            const int32 AL = BYMExperience::Progress(Attacker->EarnedExperience, Attacker->BaseValue).Level;
            const int32 DL = BYMExperience::Progress(Defender->EarnedExperience, Defender->BaseValue).Level;
            if (!(AL >= 40 && DL >= 40))
            {
                if (DL - AL > 11) { int32& Owed = MapPlayers.FindChecked(Cell->Owner).Retaliations.FindOrAdd(ActorId); if (Owed < MAX_int32) ++Owed; }
                if (AL - DL > 11) { FMapPlayer& Player = MapPlayers.FindChecked(ActorId); int32& Owed = Player.Retaliations.FindOrAdd(Cell->Owner); if (--Owed <= 0) Player.Retaliations.Remove(Cell->Owner); }
            }
        }
        Battles.Add(ActorId, MoveTemp(Battle));
        return BYMMapState::ReplyWithView(true, TEXT("Map attack started. Deploy your army; at least 90% building damage is required."), DefenderId);
    }
    if (Operation == TEXT("mapRepair") || Operation == TEXT("mapMove") || Operation == TEXT("mapAbandon"))
    {
        if (Cell->Owner != ActorId || !BYMMapState::IsCapturable(Cell->Kind)) return Reply(false, TEXT("Manage a territory that you own."));
        if (Busy(ActorId) || Busy(Cell->YardId)) return Reply(false, TEXT("Territory management is unavailable during a battle."));
        if (Operation == TEXT("mapAbandon"))
        {
            int32 Revision = 0; if (!Integer(Params, TEXT("expectedRevision"), 0, 1000000000, Revision) || Revision != Cell->Revision)
                return Reply(false, TEXT("Territory ownership changed. Refresh before abandoning it."));
            AdvanceMapResources(Actor, Now); Cell->Owner.Reset(); ++Cell->Revision; Cell->ShieldUntil = 0;
            if (FYard* Site = Yards.Find(Cell->YardId)) Site->ShieldUntil = 0;
            ReindexMapOwners(); return Reply(true, TEXT("Territory abandoned. Its production and bonuses no longer belong to you."));
        }
        FString Error; if (!EnsureMapSite(CellId, Error)) return Reply(false, Error);
        FYard* Yard = Yards.Find(Cell->YardId); if (!Yard) return Reply(false, TEXT("Territory yard unavailable."));
        Advance(*Yard); MapDirtySites.Add(CellId);
        if (Operation == TEXT("mapRepair"))
        {
            bool bStarted = false;
            for (FBuilding& B : Yard->Buildings) if (const FLevel* L = Stats(B))
                if (B.HP < L->HP) { B.HP = FMath::Max(B.HP, L->HP * .25); B.bRepairing = true; bStarted = true; }
            return Reply(bStarted, bStarted ? TEXT("Territory repair started. Original outpost repairs restore at least 25% health immediately.") : TEXT("This territory is already fully repaired."));
        }
        FBuilding* B = FindBuilding(*Yard, Str(Params, TEXT("building"))); int32 X = 0, Y = 0;
        if (!B || B->Type == 138 || B->Type == 139 || B->Type == 140 || B->bRepairing || B->HP <= 0
            || !Integer(Params, TEXT("x"), YardMinX(*Yard), YardMinX(*Yard) + YardWidth(*Yard), X)
            || !Integer(Params, TEXT("y"), YardMinY(*Yard), YardMinY(*Yard) + YardHeight(*Yard), Y)
            || !Fits(*Yard, X, Y, Footprint(*B), B->Id)) return Reply(false, TEXT("Choose a movable territory building and an empty location inside its yard."));
        B->X = X; B->Y = Y; return Reply(true, TEXT("Territory building moved."));
    }
    return Reply(false, TEXT("Unknown world-map action."));
}

void FBYMSimulation::FImpl::FinishMapBattle(const FBattle& Battle, bool bWon, bool bContested)
{
    FMapCell* Cell = MapCells.Find(Battle.MapCellId); if (!Cell || !bContested) return;
    FYard* Attacker = Yards.Find(Battle.Attacker); FYard* Defender = Yards.Find(Battle.Defender);
    if (!Attacker || !Defender) return;
    Cell->ShieldUntil = Cell->Kind == 101 ? Defender->ShieldUntil : 0.; Defender->ShieldUntil = Cell->ShieldUntil;
    if (Cell->Kind != 101) MapDirtySites.Add(Cell->Id);
    if (!bWon || MapDamagePercent(*Defender) < 90) return;
    if (Cell->Kind == 101) return; // A main backyard is never transferable.
    AdvanceMapResources(*Attacker, Now);
    if (FYard* Previous = Yards.Find(Cell->Owner)) AdvanceMapResources(*Previous, Now);
    if (Cell->Kind == 1)
    { Cell->DestroyedUntil = Now + 3. * 86400.; Cell->ShieldUntil = 0; }
    else
    {
        Cell->Owner = Battle.Attacker; Defender->Army.Empty();
        for (int64& Resource : Defender->Resource) Resource = 0;
        for (FBuilding& Building : Defender->Buildings) Building.Garrison.Empty();
        // Capture preserves original building health; the owner may repair it.
        Defender->ShieldUntil = Cell->ShieldUntil;
    }
    ++Cell->Revision; ReindexMapOwners();

}

bool FBYMSimulation::FImpl::BotMapAction(FYard& Actor)
{
    if (!MapGate(Actor).IsEmpty() || Busy(Actor.Id)) return false;
    MapPlayers.FindChecked(Actor.Id).bEntered = true;
    const auto* Owned = MapOwned.Find(Actor.Id);
    if (Owned && Actor.Decisions % 5 == 0)
        for (const FString& Id : *Owned)
        {
            const FMapCell* Cell = MapCells.Find(Id); if (!Cell || !BYMMapState::IsCapturable(Cell->Kind)) continue;
            const FYard* Yard = Yards.Find(Cell->YardId); if (!Yard) continue;
            bool NeedsRepair = false; for (const FBuilding& B : Yard->Buildings) { const FLevel* L = Stats(B); if (L && B.HP < L->HP && !B.bRepairing) { NeedsRepair = true; break; } }
            if (NeedsRepair) { FObject P = Obj(); P->SetStringField(TEXT("cell"), Id); FObject R; Decode(ExecuteMap(Actor, TEXT("mapRepair"), P), R); if (Flag(R, TEXT("ok"))) return true; }
        }
    if (ArmyCount(Actor) < 5 || Actor.Decisions % 4 != 0) return false;
    const FMapPlayer* Player = MapPlayers.Find(Actor.Id); const FMapCell* Home = Player ? MapCells.Find(Player->HomeCell) : nullptr; if (!Home) return false;
    const int32 Radius = 16; const int32 Span = Radius * 2 + 1;
    const int32 Start = static_cast<int32>((GetTypeHash(Actor.Id) + static_cast<uint32>(Actor.Decisions)) % static_cast<uint32>(Span * Span));
    for (int32 Offset = 0; Offset < Span * Span; ++Offset)
    {
        const int32 Index = (Start + Offset * 17) % (Span * Span);
        const FMapCell* Cell = MapCells.Find(BYMMapState::CellId(Home->X + Index % Span - Radius, Home->Y + Index / Span - Radius));
        if (!Cell || Cell->Owner == Actor.Id || (!BYMMapState::IsCapturable(Cell->Kind) && Cell->Kind != 101) || !MapAttackReason(Actor, *Cell).IsEmpty()) continue;
        FObject P = Obj(); P->SetStringField(TEXT("cell"), Cell->Id); P->SetStringField(TEXT("expectedOwner"), Cell->Owner); P->SetNumberField(TEXT("expectedRevision"), Cell->Revision);
        const int64 Cost = BYMMapRoomRules::AttackCost(MapDistanceBeyondRange(Actor.Id, *Cell), AttainedLevel(Actor, 14)); const int64 Costs[] = {Cost, Cost, Cost, 0};
        P->SetObjectField(TEXT("maxCost"), ResourceObject(Costs));
        FObject Response; Decode(ExecuteMap(Actor, TEXT("mapInvade"), P), Response); return Flag(Response, TEXT("ok"));
    }
    return false;
}

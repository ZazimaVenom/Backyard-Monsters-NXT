// Host-owned Map Room save state; included after BYMSimulationMap.inl.
FObject FBYMSimulation::FImpl::SaveMap() const
{
    FObject O = Obj(); O->SetNumberField(TEXT("version"), 1); FArray Cells, Players, Sites;
    TSet<FString> ActiveSites;
    for (const auto& Pair : Battles)
        if (!Pair.Value.MapCellId.IsEmpty()) ActiveSites.Add(Pair.Value.MapCellId);
    TArray<FString> Keys; MapCells.GetKeys(Keys); Keys.Sort();
    for (const FString& Key : Keys)
    {
        const FMapCell& Cell = MapCells.FindChecked(Key);
        if (Cell.Kind == 101 || Cell.bHomeDefense || Cell.Revision > 0 || !Cell.Owner.IsEmpty())
        {
            FObject Item = Obj(); Item->SetStringField(TEXT("id"), Cell.Id); Item->SetNumberField(TEXT("x"), Cell.X); Item->SetNumberField(TEXT("y"), Cell.Y);
            Item->SetNumberField(TEXT("kind"), Cell.Kind); Item->SetNumberField(TEXT("level"), Cell.Level);
            Item->SetStringField(TEXT("owner"), Cell.Owner); Item->SetStringField(TEXT("yard"), Cell.YardId);
            Item->SetStringField(TEXT("parent"), Cell.Parent); Item->SetBoolField(TEXT("homeDefense"), Cell.bHomeDefense);
            Item->SetNumberField(TEXT("revision"), Cell.Revision); Item->SetNumberField(TEXT("shieldUntil"), Cell.ShieldUntil);
            Item->SetNumberField(TEXT("destroyedUntil"), Cell.DestroyedUntil); Cells.Add(MakeShared<FJsonValueObject>(Item));
        }
        // Merely scouting a pristine source template must not grow the save
        // forever. Changed, owned and currently contested sites are durable;
        // an evicted pristine view can be reconstructed from the source data.
        if (Cell.Kind == 101 || (!MapDirtySites.Contains(Cell.Id)
            && Cell.Owner.IsEmpty() && !ActiveSites.Contains(Cell.Id))) continue;
        const FYard* Yard = Yards.Find(Cell.YardId); if (!Yard) continue;
        FObject Site = Obj(); Site->SetStringField(TEXT("cell"), Cell.Id); Site->SetNumberField(TEXT("lastEconomy"), Yard->LastEconomy);
        Site->SetObjectField(TEXT("resources"), ResourceObject(Yard->Resource)); FArray Structures;
        FObject Army = Obj();
        for (const auto& Pair : Yard->Army) if (Pair.Value > 0) Army->SetNumberField(Pair.Key, Pair.Value);
        Site->SetObjectField(TEXT("army"), Army);
        const FObject* SourceTemplate = MapTemplates.Find(MapTemplateKey(Cell));
        FYard Original;
        TMap<FString, const FBuilding*> OriginalBuildings;
        if (SourceTemplate)
        {
            Original = CreateMapSite(Cell, *SourceTemplate);
            for (const FBuilding& B : Original.Buildings) OriginalBuildings.Add(B.Id, &B);
            Site->SetStringField(TEXT("buildingEncoding"), TEXT("source-delta-v1"));
        }
        for (const FBuilding& B : Yard->Buildings)
        {
            const FBuilding* const* OriginalEntry = OriginalBuildings.Find(B.Id);
            const FBuilding* Baseline = OriginalEntry ? *OriginalEntry : nullptr;
            bool bGarrisonChanged = !Baseline || Baseline->Garrison.Num() != B.Garrison.Num();
            if (Baseline) for (const auto& Pair : B.Garrison)
                if (Baseline->Garrison.FindRef(Pair.Key) != Pair.Value) bGarrisonChanged = true;
            // Fully repaired, unmoved source structures need no durable row.
            // This matters for hundreds of walls in every captured layout.
            if (Baseline && B.X == Baseline->X && B.Y == Baseline->Y && B.HP == Baseline->HP
                && B.bRepairing == Baseline->bRepairing && !bGarrisonChanged) continue;
            FObject Building = Obj(); Building->SetStringField(TEXT("id"), B.Id); Building->SetNumberField(TEXT("x"), B.X); Building->SetNumberField(TEXT("y"), B.Y);
            Building->SetNumberField(TEXT("hp"), B.HP); Building->SetBoolField(TEXT("repairing"), B.bRepairing);
            if (B.Type == 22)
            {
                FObject Garrison = Obj();
                for (const auto& Pair : B.Garrison) if (Pair.Value > 0) Garrison->SetNumberField(Pair.Key, Pair.Value);
                Building->SetObjectField(TEXT("garrison"), Garrison);
            }
            Structures.Add(MakeShared<FJsonValueObject>(Building));
        }
        Site->SetArrayField(TEXT("buildings"), Structures); Sites.Add(MakeShared<FJsonValueObject>(Site));
    }
    Keys.Reset(); MapPlayers.GetKeys(Keys); Keys.Sort();
    for (const FString& Key : Keys)
    {
        const FMapPlayer& P = MapPlayers.FindChecked(Key); FObject Item = Obj(); Item->SetStringField(TEXT("id"), Key);
        Item->SetStringField(TEXT("home"), P.HomeCell); Item->SetNumberField(TEXT("productionAt"), P.ProductionAt); Item->SetNumberField(TEXT("relocatedAt"), P.RelocatedAt);
        Item->SetBoolField(TEXT("entered"), P.bEntered);
        Item->SetNumberField(TEXT("productionFraction"), P.ProductionFraction);
        FObject Retaliations = Obj();
        for (const auto& Pair : P.Retaliations) if (Pair.Value > 0) Retaliations->SetNumberField(Pair.Key, Pair.Value);
        Item->SetObjectField(TEXT("retaliations"), Retaliations);
        FArray Bookmarks; for (const FString& Id : P.Bookmarks) Bookmarks.Add(MakeShared<FJsonValueString>(Id));
        Item->SetArrayField(TEXT("bookmarks"), Bookmarks); Players.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("cells"), Cells); O->SetArrayField(TEXT("players"), Players); O->SetArrayField(TEXT("sites"), Sites); return O;
}

bool FBYMSimulation::FImpl::LoadMap(const FObject& Saved, FString& Error)
{
    auto Fail = [&](const TCHAR* Message) { Error = FString(TEXT("World map save is invalid: ")) + Message + TEXT(". Original save has been preserved."); return false; };
    if (!Saved->HasField(TEXT("worldMap")))
    {
        TArray<FString> Owners; Yards.GetKeys(Owners); Owners.Sort();
        for (const FString& Owner : Owners) if (!AddMapPlayer(Owner, Error)) return false;
        return true;
    }
    const int32 ExpectedPlayers = Yards.Num();
    const FObject O = Child(Saved, TEXT("worldMap")); int32 Version = 0;
    if (!Integer(O, TEXT("version"), 1, 1, Version)) return Fail(TEXT("unsupported territory version"));
    const FArray* Cells = nullptr; const FArray* Players = nullptr; const FArray* Sites = nullptr;
    if (!O->TryGetArrayField(TEXT("cells"), Cells) || !Cells || Cells->Num() > 250000
        || !O->TryGetArrayField(TEXT("players"), Players) || !Players || Players->Num() != ExpectedPlayers
        || !O->TryGetArrayField(TEXT("sites"), Sites) || !Sites || Sites->Num() > 250000)
        return Fail(TEXT("territory arrays missing or oversized"));
    TSet<FString> SeenCells, MainOwners;
    for (const auto& Value : *Cells)
    {
        const FObject Item = Value && Value->Type == EJson::Object ? Value->AsObject() : FObject(); FMapCell Cell;
        if (!Item || !Integer(Item, TEXT("x"), 0, 499, Cell.X) || !Integer(Item, TEXT("y"), 0, 499, Cell.Y)
            || !Integer(Item, TEXT("kind"), 1, 104, Cell.Kind) || !Integer(Item, TEXT("level"), 0, 50, Cell.Level)
            || !Integer(Item, TEXT("revision"), 0, 1000000000, Cell.Revision) || !Item->TryGetBoolField(TEXT("homeDefense"), Cell.bHomeDefense))
            return Fail(TEXT("invalid territory cell fields"));
        Cell.Id = Str(Item, TEXT("id")); Cell.Owner = Str(Item, TEXT("owner")); Cell.YardId = Str(Item, TEXT("yard")); Cell.Parent = Str(Item, TEXT("parent"));
        if (Cell.Id != BYMMapState::CellId(Cell.X, Cell.Y) || SeenCells.Contains(Cell.Id)
            || (!Cell.Owner.IsEmpty() && !Yards.Contains(Cell.Owner))) return Fail(TEXT("duplicate cell or missing owner"));
        if (!BYMMapState::Position(Item, TEXT("shieldUntil"), 0, Now + 31536000., Cell.ShieldUntil)
            || !BYMMapState::Position(Item, TEXT("destroyedUntil"), 0, Now + 3. * 86400., Cell.DestroyedUntil)) return Fail(TEXT("invalid territory timestamps"));
        Cell.Terrain = MapTerrain[Cell.Y * 500 + Cell.X];
        // Dynamic homes and their six fortifications use the same stable
        // ground-atlas variation as AddMapPlayer. Reconstruct it after a save
        // reload instead of reverting to the terrain that the yard replaced.
        if (Cell.Kind == 101 || Cell.bHomeDefense) Cell.Terrain = 5 + (Cell.X * 73 + Cell.Y * 31) % 45;
        const FMapCell* Base = MapBaseCells.Find(Cell.Id);
        if (Cell.Kind == 101)
        {
            if (Cell.Owner.IsEmpty() || Cell.YardId != Cell.Owner || MainOwners.Contains(Cell.Owner) || Cell.bHomeDefense || Cell.Level != 0 || !Cell.Parent.IsEmpty()
                || Cell.X < 3 || Cell.X > 497 || Cell.Y < 3 || Cell.Y > 497
                || (Base && Base->Kind != 1)) return Fail(TEXT("invalid main-yard sector"));
            MainOwners.Add(Cell.Owner);
        }
        else if (Cell.bHomeDefense)
        {
            if (Cell.Kind != 104 || Cell.Parent.IsEmpty() || Cell.YardId != TEXT("map-") + Cell.Id || (Base && Base->Kind != 1)) return Fail(TEXT("invalid main-yard defender"));
            if (!MapTemplates.Contains(MapTemplateKey(Cell))) return Fail(TEXT("unknown defender level"));
        }
        else
        {
            if (!Base || Cell.Kind != Base->Kind || Cell.Level != Base->Level || Cell.YardId != Base->YardId || Cell.Parent != Base->Parent)
                return Fail(TEXT("territory differs from the original map"));
            Cell.TribeIndex = Base->TribeIndex;
        }
        if ((Cell.Kind == 1 && !Cell.Owner.IsEmpty()) || (Cell.Kind != 1 && Cell.DestroyedUntil != 0)
            || (Cell.Kind != 101 && Cell.ShieldUntil != 0)) return Fail(TEXT("invalid capture, shield or regeneration state"));
        SeenCells.Add(Cell.Id); MapCells.Add(Cell.Id, Cell);
    }
    // Unique sites cannot exceed the actual generated/overlaid site count.
    // The previous arbitrary 10,000 cache limit could reject a valid long-lived
    // host after enough territory had been fought over.
    if (Sites->Num() > MapCells.Num()) return Fail(TEXT("territory site count exceeds this world"));
    TSet<FString> SeenPlayers;
    for (const auto& Value : *Players)
    {
        const FObject Item = Value && Value->Type == EJson::Object ? Value->AsObject() : FObject();
        const FString Id = Str(Item, TEXT("id")); FMapPlayer Player; Player.HomeCell = Str(Item, TEXT("home"));
        const FMapCell* Home = MapCells.Find(Player.HomeCell);
        if (!Yards.Contains(Id) || SeenPlayers.Contains(Id) || !Home || Home->Kind != 101 || Home->Owner != Id
            || !BYMMapState::Position(Item, TEXT("productionAt"), 0, Now + 86400., Player.ProductionAt)
            || !BYMMapState::Position(Item, TEXT("relocatedAt"), 0, Now + 86400., Player.RelocatedAt)) return Fail(TEXT("invalid player world-map state"));
        // These fields were absent from earlier territory saves. Missing
        // fields use their default values, while malformed present fields are
        // rejected so a corrupted fraction cannot mint production on reload.
        if ((Item->HasField(TEXT("entered")) && !Item->TryGetBoolField(TEXT("entered"), Player.bEntered))
            || (Item->HasField(TEXT("productionFraction"))
                && (!BYMMapState::Position(Item, TEXT("productionFraction"), 0, 1, Player.ProductionFraction)
                    || Player.ProductionFraction >= 1)))
            return Fail(TEXT("invalid entry state or fractional production"));
        if (Item->HasField(TEXT("retaliations")))
        {
            const FObject Retaliations = Child(Item, TEXT("retaliations"));
            if (!Retaliations || Retaliations->Values.Num() >= ExpectedPlayers)
                return Fail(TEXT("invalid retaliation table"));
            for (const auto& Pair : Retaliations->Values)
            {
                const FString RetaliationOwner(*Pair.Key);
                int32 Count = 0;
                if (RetaliationOwner == Id || !Yards.Contains(RetaliationOwner)
                    || !Integer(Retaliations, *RetaliationOwner, 0, TNumericLimits<int32>::Max(), Count))
                    return Fail(TEXT("invalid retaliation owner or count"));
                if (Count > 0) Player.Retaliations.Add(RetaliationOwner, Count);
            }
        }
        const FArray* Bookmarks = nullptr;
        if (!Item->TryGetArrayField(TEXT("bookmarks"), Bookmarks) || !Bookmarks || Bookmarks->Num() > 100) return Fail(TEXT("invalid bookmark list"));
        for (const auto& Bookmark : *Bookmarks)
        {
            if (!Bookmark || Bookmark->Type != EJson::String) return Fail(TEXT("invalid bookmark"));
            const FString Key = Bookmark->AsString(); if (!MapCells.Contains(Key) || Player.Bookmarks.Contains(Key)) return Fail(TEXT("duplicate or unavailable bookmark"));
            Player.Bookmarks.Add(Key);
        }
        for (const auto& Previous : MapPlayers)
        {
            const FMapCell& Other = MapCells.FindChecked(Previous.Value.HomeCell);
            if (BYMMapRoomRules::HexDistance(FIntPoint(Home->X, Home->Y), FIntPoint(Other.X, Other.Y)) < 7) return Fail(TEXT("main yards violate original separation"));
        }
        SeenPlayers.Add(Id); MapPlayers.Add(Id, Player);
    }
    if (MainOwners.Num() != ExpectedPlayers) return Fail(TEXT("missing main-yard owner"));
    const int32 HomeLevels[] = {5, 5, 10, 10, 15, 20};
    for (const auto& Pair : MapPlayers)
    {
        const FMapCell& Home = MapCells.FindChecked(Pair.Value.HomeCell); int32 Direction = 0;
        for (const FIntPoint& P : BYMMapRoomRules::Neighbors(FIntPoint(Home.X, Home.Y)))
        {
            const FMapCell* Fort = MapCells.Find(BYMMapState::CellId(P.X, P.Y));
            if (!Fort || !Fort->bHomeDefense || Fort->Parent != Home.Id || Fort->Kind != 104 || Fort->Level != HomeLevels[Direction++]) return Fail(TEXT("missing or invalid surrounding defender"));
        }
    }
    for (const auto& Pair : MapCells) if (Pair.Value.bHomeDefense)
    {
        const FMapCell* Parent = MapCells.Find(Pair.Value.Parent);
        if (!Parent || Parent->Kind != 101 || BYMMapRoomRules::HexDistance(FIntPoint(Parent->X, Parent->Y), FIntPoint(Pair.Value.X, Pair.Value.Y)) != 1)
            return Fail(TEXT("orphaned home defender"));
    }
    ReindexMapOwners(); TSet<FString> SeenSites;
    MapDirtySites.Empty(); MapSiteCacheOrder.Empty();
    for (const auto& Value : *Sites)
    {
        const FObject Item = Value && Value->Type == EJson::Object ? Value->AsObject() : FObject(); const FString CellId = Str(Item, TEXT("cell"));
        const FMapCell* Cell = MapCells.Find(CellId);
        if (!Cell || Cell->Kind == 101 || SeenSites.Contains(CellId)) return Fail(TEXT("duplicate or invalid territory yard"));
        const FObject* Template = MapTemplates.Find(MapTemplateKey(*Cell)); if (!Template) return Fail(TEXT("unknown territory template"));
        FYard Yard = CreateMapSite(*Cell, *Template);
        if (!BYMMapState::Position(Item, TEXT("lastEconomy"), 0, Now + 86400., Yard.LastEconomy)
            || !ReadResources(Child(Item, TEXT("resources")), Yard.Resource)) return Fail(TEXT("invalid territory accrual state"));
        if (Item->HasField(TEXT("army")))
        {
            const FObject Army = Child(Item, TEXT("army"));
            if (!Army || Army->Values.Num() > 100) return Fail(TEXT("invalid territory army"));
            Yard.Army.Empty();
            for (const auto& Pair : Army->Values)
            {
                const FString ArmyMonsterId(*Pair.Key);
                int32 Count = 0;
                if (!Monsters.Contains(ArmyMonsterId) || !Integer(Army, *ArmyMonsterId, 0, 100000, Count))
                    return Fail(TEXT("unknown or invalid territory monster count"));
                if (Count > 0) Yard.Army.Add(ArmyMonsterId, Count);
            }
        }
        else if (!Cell->Owner.IsEmpty()) Yard.Army.Empty();
        const bool bDelta = Item->HasField(TEXT("buildingEncoding"));
        if (bDelta && Str(Item, TEXT("buildingEncoding")) != TEXT("source-delta-v1"))
            return Fail(TEXT("unsupported territory building encoding"));
        const FArray* Structures = nullptr;
        if (!Item->TryGetArrayField(TEXT("buildings"), Structures) || !Structures
            || (bDelta ? Structures->Num() > Yard.Buildings.Num() : Structures->Num() != Yard.Buildings.Num()))
            return Fail(TEXT("territory structures differ from original template"));
        TSet<FString> SeenBuildings, MovedBuildings;
        for (const auto& BuildingValue : *Structures)
        {
            const FObject BuildingObject = BuildingValue && BuildingValue->Type == EJson::Object ? BuildingValue->AsObject() : FObject();
            const FString BuildingId = Str(BuildingObject, TEXT("id")); FBuilding* B = FindBuilding(Yard, BuildingId);
            if (!B || SeenBuildings.Contains(BuildingId)) return Fail(TEXT("duplicate or unknown territory building"));
            const double OriginalX = B->X, OriginalY = B->Y; const FLevel* L = Stats(*B);
            if (!L || !BYMMapState::Position(BuildingObject, TEXT("x"), YardMinX(Yard), YardMinX(Yard) + YardWidth(Yard), B->X)
                || !BYMMapState::Position(BuildingObject, TEXT("y"), YardMinY(Yard), YardMinY(Yard) + YardHeight(Yard), B->Y)
                || !BYMMapState::Position(BuildingObject, TEXT("hp"), 0, L->HP, B->HP) || !BuildingObject->TryGetBoolField(TEXT("repairing"), B->bRepairing))
                return Fail(TEXT("invalid territory building position or health"));
            const bool bMoved = FMath::Abs(B->X - OriginalX) > .000001 || FMath::Abs(B->Y - OriginalY) > .000001;
            if (bMoved)
            {
                if (B->Type == 138 || B->Type == 139 || B->Type == 140
                    || B->X != std::floor(B->X) || B->Y != std::floor(B->Y))
                    return Fail(TEXT("immovable or off-grid territory building"));
                MovedBuildings.Add(BuildingId);
            }
            else { B->X = OriginalX; B->Y = OriginalY; }
            if (BuildingObject->HasField(TEXT("garrison")))
            {
                const FObject Garrison = Child(BuildingObject, TEXT("garrison"));
                if (B->Type != 22 || !Garrison || Garrison->Values.Num() > Monsters.Num()
                    || (B->HP <= 0 && Garrison->Values.Num() > 0))
                    return Fail(TEXT("invalid territory Bunker garrison"));
                B->Garrison.Empty();
                for (const auto& Pair : Garrison->Values)
                {
                    const FString GarrisonMonsterId(*Pair.Key);
                    int32 Count = 0;
                    if (!Monsters.Contains(GarrisonMonsterId) || !BYMBunkerRules::IsAllowedSpecies(GarrisonMonsterId)
                        || !Integer(Garrison, *GarrisonMonsterId, 1, 1024, Count))
                        return Fail(TEXT("invalid territory Bunker monster count"));
                    B->Garrison.Add(GarrisonMonsterId, Count);
                }
                if (BunkerUsed(Yard, *B) > BYMBunkerRules::Capacity(B->Level))
                    return Fail(TEXT("territory Bunker garrison exceeds capacity"));
            }
            SeenBuildings.Add(BuildingId);
        }
        // Original authored layouts intentionally contain quarter-cell
        // positions and overlapping footprint boxes. Preserve them exactly.
        // A player-authored move must pass the same authoritative placement
        // rule as mapMove, against all final saved positions, regardless of
        // serialization order. Do not silently trust a moved overlap.
        for (const FBuilding& B : Yard.Buildings)
            if (MovedBuildings.Contains(B.Id) && !Fits(Yard, static_cast<int32>(B.X), static_cast<int32>(B.Y), Footprint(B), B.Id))
                return Fail(TEXT("moved territory building overlaps another footprint or yard boundary"));
        int32 GarrisonCount = 0;
        for (const FBuilding& B : Yard.Buildings) for (const auto& Pair : B.Garrison) GarrisonCount += Pair.Value;
        if (GarrisonCount > 1024) return Fail(TEXT("territory garrison exceeds the defensive simulation bound"));
        const FString SiteYardId = Yard.Id;
        SeenSites.Add(CellId); MapDirtySites.Add(CellId); Yards.Add(SiteYardId, MoveTemp(Yard));
    }
    // Capturing always materializes and retains the site's exact battle state.
    // Accepting an owned cell without its site row would let a truncated save
    // recreate damaged buildings and the old NPC wallet at source defaults.
    for (const auto& Pair : MapCells)
        if (Pair.Value.Kind != 101 && !Pair.Value.Owner.IsEmpty() && !SeenSites.Contains(Pair.Key))
            return Fail(TEXT("owned territory is missing its persistent yard state"));
    return true;
}


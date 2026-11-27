// Included by BYMSimulation.cpp after FImpl's declaration. Bot layout choices
// use the same footprint, mushroom, health and command rules as human moves.

bool FBYMSimulation::FImpl::MigrateBotLayout(FYard& Y)
{
    if (!Y.bBot || IsMapSite(Y.Id) || Y.LayoutRevision >= 1 || Busy(Y.Id) || Y.Buildings.Num() != 11
        || !Y.Queue.IsEmpty() || !Y.MonsterResearch.IsEmpty() || !Y.MonsterUnlock.Type.IsEmpty()) return false;
    for (const FWorker& Worker : Y.Workers) if (!Worker.Kind.IsEmpty()) return false;
    for (const FYard::FMushroom& Mushroom : Y.Mushrooms) if (Mushroom.bPicking) return false;
    static const int32 StarterTypes[] = {14, 15, 5, 6, 13, 8, 1, 2, 3, 4, 20};
    for (const int32 Type : StarterTypes) if (CountOf(Y, Type) != 1) return false;
    for (const FBuilding& Building : Y.Buildings)
        if (Building.Level != 1 || HasWork(Building) || Building.WorkRemaining > 0. || !Building.JobId.IsEmpty()
            || !MoveReason(Y, Building).IsEmpty()) return false;

    // Plan on a copy with the old footprints removed. Existing mushrooms,
    // resources, levels, health, IDs and all paid state remain untouched.
    // Larger starters are placed first so small collectors do not fragment the
    // footprint required by Housing and the Flinger.
    FYard Candidate = Y;
    Candidate.Buildings.Reset();
    for (const int32 Type : StarterTypes)
    {
        const FBuilding* Existing = Y.Buildings.FindByPredicate([Type](const FBuilding& Building) { return Building.Type == Type; });
        const FBuildingDef* Definition = Buildings.Find(Type);
        if (!Existing || !Definition) return false;
        int32 X = 0, PosY = 0;
        if (!FreeSpot(Candidate, Definition->Cells, X, PosY, Type)) return false;
        FBuilding Planned = *Existing; Planned.X = X; Planned.Y = PosY;
        Candidate.Buildings.Add(MoveTemp(Planned));
    }
    // Recheck the complete proposed layout, not just each insertion prefix.
    for (const FBuilding& Planned : Candidate.Buildings)
        if (!Fits(Candidate, static_cast<int32>(Planned.X), static_cast<int32>(Planned.Y), Footprint(Planned), Planned.Id)) return false;
    TArray<FVector2D> NewPositions; NewPositions.Reserve(Y.Buildings.Num());
    for (const FBuilding& Existing : Y.Buildings)
    {
        const FBuilding* Planned = FindBuilding(Candidate, Existing.Id);
        if (!Planned) return false;
        NewPositions.Add(FVector2D(Planned->X, Planned->Y));
    }
    // Only positions and the one-time migration marker are committed. Preserve
    // the original building array order and every other gameplay field.
    for (int32 Index = 0; Index < Y.Buildings.Num(); ++Index)
    { Y.Buildings[Index].X = NewPositions[Index].X; Y.Buildings[Index].Y = NewPositions[Index].Y; }
    Y.LayoutRevision = 1;
    return true;
}

bool FBYMSimulation::FImpl::BotLayoutMove(FYard& Y)
{
    if (!Y.bBot || IsMapSite(Y.Id) || Busy(Y.Id) || Y.Buildings.IsEmpty()) return false;
    const FIntRect Bounds(YardMinX(Y), YardMinY(Y), YardMinX(Y) + YardWidth(Y), YardMinY(Y) + YardHeight(Y));
    // One candidate building per action bounds the planning cost. Ineligible
    // buildings are skipped, but the bot cannot reorganize a whole yard for
    // free while a player would have to satisfy individual move validation.
    const int32 Offset = static_cast<int32>((GetTypeHash(Y.Id) + static_cast<uint32>(Y.Decisions))
        % static_cast<uint32>(Y.Buildings.Num()));
    for (int32 Step = 0; Step < FMath::Min(8, Y.Buildings.Num()); ++Step)
    {
        const FBuilding& Building = Y.Buildings[(Offset + Step) % Y.Buildings.Num()];
        if (!MoveReason(Y, Building).IsEmpty()) continue;
        const FBuildingDef* Definition = Buildings.Find(Building.Type); if (!Definition) continue;
        int32 Occurrence = 0;
        for (const FBuilding& Other : Y.Buildings) if (Other.Type == Building.Type && Other.Id < Building.Id) ++Occurrence;
        const auto Plan = BYMBotLayout::MakePlan(Y.Id, Building.Type, Occurrence, Bounds, Definition->Cells);
        if (!Plan.bValid) continue;
        const int32 CurrentX = static_cast<int32>(Building.X), CurrentY = static_cast<int32>(Building.Y);
        uint64 BestScore = BYMBotLayout::RankCandidate(Plan, CurrentX, CurrentY);
        int32 BestX = CurrentX, BestY = CurrentY;
        for (int32 PosY = Bounds.Min.Y + 4; PosY <= Bounds.Max.Y - Definition->Cells - 4; ++PosY)
            for (int32 X = Bounds.Min.X + 4; X <= Bounds.Max.X - Definition->Cells - 4; ++X)
            {
                const uint64 Score = BYMBotLayout::RankCandidate(Plan, X, PosY);
                if (Score >= BestScore || !Fits(Y, X, PosY, Definition->Cells, Building.Id)) continue;
                BestScore = Score; BestX = X; BestY = PosY;
            }
        if (BestX == CurrentX && BestY == CurrentY) return false;
        FObject Params = Obj(); Params->SetStringField(TEXT("building"), Building.Id);
        Params->SetNumberField(TEXT("x"), BestX); Params->SetNumberField(TEXT("y"), BestY);
        FObject Response;
        const bool Accepted = Decode(Execute(Y.Id, TEXT("move"), Params), Response) && Flag(Response, TEXT("ok"));
        if (Accepted) Y.LayoutRevision = 1;
        return Accepted;
    }
    return false;
}

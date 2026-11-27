// Original source: GuardTower.as / GuardTowerTesla, Targeting.as,
// BFOUNDATION.damage and com/monsters/utils/MathUtils.as. This is deliberately
// separate from BUILDING25's charge/burst/cool cycle: source type 138 has four
// independent continuous lightning emitters with a one-second firing clock.
namespace BYMMapCombat
{
    FVector2D EmitterOrigin(const FBuilding& Building, int32 Index, bool bDamaged)
    {
        static const FVector2D Intact[4] = {
            FVector2D(-.5, -68), FVector2D(58, -37),
            FVector2D(-.5, -7), FVector2D(-59, -40)
        };
        static const FVector2D Damaged[4] = {
            FVector2D(-.5, -68), FVector2D(58, -26),
            FVector2D(-6, -7), FVector2D(-67, -53)
        };
        // Map templates retain their original Cartesian pixels, translated by
        // (25,20) native cells. BFOUNDATION.Setup uses GRID.ToISO (floor), and
        // GuardTower adds the screen-space offset to that upper-left anchor.
        // The building's footprint center is not an emitter origin.
        const double X = (Building.X - 25.) * SourcePixelsPerCell;
        const double Y = (Building.Y - 20.) * SourcePixelsPerCell;
        const FVector2D Offset = bDamaged ? Damaged[Index] : Intact[Index];
        const double IsoX = std::floor(X - Y) + Offset.X;
        const double IsoY = std::floor((X + Y) * .5) + Offset.Y;
        return FVector2D(25. + (IsoY + IsoX * .5) / SourcePixelsPerCell,
            20. + (IsoY - IsoX * .5) / SourcePixelsPerCell);
    }

    double AcquisitionDistance(const FVector2D& Origin, const FVector2D& Position)
    {
        // PATHING.FromISO truncates each source Cartesian coordinate before
        // Targeting's distance test. Undo the map's integer translation first:
        // trunc(-0.5) and trunc(499.5)-500 differ at the source map origin.
        const double X = std::trunc((Position.X - 25.) * SourcePixelsPerCell)
            - std::trunc((Origin.X - 25.) * SourcePixelsPerCell);
        const double Y = std::trunc((Position.Y - 20.) * SourcePixelsPerCell)
            - std::trunc((Origin.Y - 20.) * SourcePixelsPerCell);
        return FMath::Sqrt(X * X + Y * Y);
    }

    bool InsideEmitterArc(int32 Index, const FVector2D& Origin, const FVector2D& Position)
    {
        const FVector2D Delta = Position - Origin;
        const double Degrees = std::atan2((Delta.X + Delta.Y) * .5, Delta.X - Delta.Y)
            * (180. / 3.14159265358979323846);
        // GuardTower's special left arc compares abs(angle), including both
        // ends. Other arcs are inclusive too, so adjacent emitters can share a
        // target at their boundary; this is not four exclusive quadrants.
        switch (Index)
        {
            case 0: return Degrees >= -180. && Degrees <= 0.;
            case 1: return Degrees >= -90. && Degrees <= 90.;
            case 2: return Degrees >= 0. && Degrees <= 180.;
            case 3: return FMath::Abs(Degrees) >= 90. && FMath::Abs(Degrees) <= 180.;
            default: return false;
        }
    }
}

bool FBYMSimulation::FImpl::TickMapStronghold(FBattle& Battle, FYard& Yard, FBuilding& Building, double Step)
{
    if (Building.Type != 138) return false;
    // The clock is Battle.SimulatedAt, advanced once by TickBattle. Advancing
    // four countdowns with Step here would change source acquisition timing.
    (void)Yard;
    (void)Step;
    const FLevel* Level = Stats(Building);
    if (!Level || Building.HP <= 0 || Building.Level <= 0 || HasWork(Building)
        || Level->Damage <= 0 || Level->Range <= 0) return true;

    FMapTowerState& State = Battle.MapTowers.FindOrAdd(Building.Id);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector2D Origin = BYMMapCombat::EmitterOrigin(Building, Index, Building.HP < Level->HP);
        auto IsTargetable = [&](const FTroop& Troop)
        { return Troop.HP > 0 && Monsters.Contains(Troop.Type); };
        FTroop* Target = Battle.Troops.FindByPredicate([&](const FTroop& Troop)
        { return Troop.Id == State.Targets[Index] && IsTargetable(Troop); });
        // GuardTowerTesla.tick only replaces a missing/dead/untargetable
        // target. Do not drop a living target which leaves the original arc or
        // radius: getTarget applies those tests only during acquisition.
        if (!Target)
        {
            State.Targets[Index].Reset();
            double Closest = Level->Range * SourcePixelsPerCell;
            for (FTroop& Candidate : Battle.Troops)
            {
                if (!IsTargetable(Candidate)) continue;
                const double Distance = BYMMapCombat::AcquisitionDistance(Origin, Candidate.Position);
                if (Distance < Closest && BYMMapCombat::InsideEmitterArc(Index, Origin, Candidate.Position))
                { Closest = Distance; Target = &Candidate; }
            }
            if (Target) State.Targets[Index] = Target->Id;
        }
        if (!Target || Battle.SimulatedAt + .000001 < State.NextFireAt[Index]) continue;

        // GuardTower captures BFOUNDATION.damage (an int property), then
        // applies that amount directly. Unlike ordinary tower Fire methods,
        // its emitters do not attenuate damage when the building loses HP.
        const double ShotDamage = std::trunc(FMath::Max(0., Level->Damage * MapTowerBonus(Battle))) * DamageMultiplier();
        const double AppliedDamage = FMath::Min(Target->HP, ShotDamage);
        FDefenseVisual& Visual = Battle.DefenseVisuals.FindOrAdd(Building.Id);
        Visual.Target = Target->Id;
        Visual.Facing = (Target->Position - Origin).GetSafeNormal();
        Visual.LastAttackAt = Battle.SimulatedAt;
        ++Visual.AttackSequence;
        CombatEvent(Battle, TEXT("defenseAttack"), Building.Id, Target->Id, Origin,
            Target->Position, Target->Type, Building, true, AppliedDamage);
        Target->HP = FMath::Max(0., Target->HP - AppliedDamage);
        if (Target->HP <= 0)
        {
            Target->State = TEXT("dead");
            CombatEvent(Battle, TEXT("troopDied"), Building.Id, Target->Id, Origin,
                Target->Position, Target->Type, Building, true, AppliedDamage);
        }
        // _rate is seconds for GuardTowerTesla, not ordinary BTOWER frames.
        // Start from the actual shot time; a long idle period must not create
        // accumulated catch-up shots on a newly acquired target.
        State.NextFireAt[Index] = Battle.SimulatedAt + Level->AttackInterval;
    }
    return true;
}

void FBYMSimulation::FImpl::WriteMapCombat(const FBattle& Battle, const FObject& Out) const
{
    FObject Towers = Obj();
    for (const auto& Pair : Battle.MapTowers)
    {
        FArray Emitters;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            FObject Emitter = Obj();
            Emitter->SetStringField(TEXT("target"), Pair.Value.Targets[Index]);
            Emitter->SetNumberField(TEXT("remaining"), FMath::Max(0., Pair.Value.NextFireAt[Index] - Battle.SimulatedAt));
            Emitters.Add(MakeShared<FJsonValueObject>(Emitter));
        }
        Towers->SetArrayField(Pair.Key, Emitters);
    }
    Out->SetObjectField(TEXT("mapTowerEmitters"), Towers);
}

bool FBYMSimulation::FImpl::ReadMapCombat(FBattle& Battle, const FObject& Saved, FString& Error)
{
    auto Fail = [&](const TCHAR* Reason)
    { Error = FString(TEXT("World simulation save is invalid: ")) + Reason + TEXT(". Original save has been preserved."); return false; };
    // Compatible with pre-Map-Room saves, which cannot contain a stronghold
    // battle. An explicitly present malformed field must never be ignored.
    if (!Saved->HasField(TEXT("mapTowerEmitters"))) return true;
    const FObject Towers = Child(Saved, TEXT("mapTowerEmitters"));
    const FYard* Yard = Yards.Find(Battle.Defender);
    if (!Towers.IsValid() || Towers->Values.Num() > 1000 || !Yard)
        return Fail(TEXT("invalid Stronghold emitter table"));
    TMap<FString, FMapTowerState> Loaded;
    for (const auto& Pair : Towers->Values)
    {
        const FString TowerId(*Pair.Key);
        const FBuilding* Building = FindBuilding(*Yard, TowerId);
        const FLevel* Level = Building ? Stats(*Building) : nullptr;
        const FArray* Emitters = nullptr;
        if (!Building || Building->Type != 138 || !Level
            || !Towers->TryGetArrayField(*TowerId, Emitters) || !Emitters || Emitters->Num() != 4)
            return Fail(TEXT("invalid Stronghold emitter ownership or count"));
        FMapTowerState State;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const TSharedPtr<FJsonValue>& Value = (*Emitters)[Index];
            if (!Value.IsValid() || Value->Type != EJson::Object)
                return Fail(TEXT("invalid Stronghold emitter entry"));
            const FObject Emitter = Value->AsObject();
            double Remaining = -1;
            if (!Emitter->TryGetStringField(TEXT("target"), State.Targets[Index])
                || !Emitter->TryGetNumberField(TEXT("remaining"), Remaining)
                || !FMath::IsFinite(Remaining) || Remaining < 0 || Remaining > Level->AttackInterval + .000001
                || State.Targets[Index].Len() > 160
                || (!State.Targets[Index].IsEmpty() && !Battle.Troops.ContainsByPredicate([&](const FTroop& Troop)
                    { return Troop.Id == State.Targets[Index]; })))
                return Fail(TEXT("invalid Stronghold target or firing countdown"));
            State.NextFireAt[Index] = Battle.SimulatedAt + Remaining;
        }
        Loaded.Add(TowerId, MoveTemp(State));
    }
    Battle.MapTowers = MoveTemp(Loaded);
    return true;
}

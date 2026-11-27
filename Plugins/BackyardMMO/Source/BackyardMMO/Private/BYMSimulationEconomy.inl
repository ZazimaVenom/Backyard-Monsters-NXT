// Host-only original store, mushroom and yard-expansion state. Included once by BYMSimulation.cpp.
// Source provenance and exact branch distinctions: Content/Data/store-rules.json.
namespace BYMStoreRules
{
    constexpr int32 WorkerPrices[] = {250, 500, 1000, 2000};
    constexpr int32 ExpansionPrices[] = {50, 100, 150, 200, 250, 300};
    constexpr double GrowthInterval = 17280.;
    constexpr double PickSeconds = 62. / 40.;
    constexpr int64 ShinyLimit = 1000000000;
    int32 ExpandedCells(int32 BaseCells, int32 Level)
    {
        int32 Pixels = BaseCells * 20;
        for (int32 I = 0; I < Level; ++I) Pixels = static_cast<int32>(Pixels * 1.1);
        return (Pixels + 19) / 20;
    }
    uint32 NextRandom(uint32& State)
    {
        // A private, persisted simulation stream. Rewards are rolled once at
        // spawn and never accepted from a client or rerolled on save/load.
        if (State == 0) State = 0x9e3779b9U;
        State ^= State << 13; State ^= State >> 17; State ^= State << 5;
        return State;
    }
    double UnitRandom(uint32& State) { return static_cast<double>(NextRandom(State)) / 4294967296.; }
    int32 SpeedupPrice(const FString& Code, double Remaining)
    {
        if (Code == TEXT("SP1")) return 0;
        if (Code == TEXT("SP2")) return 20;
        if (Code == TEXT("SP3")) return 40;
        // Flash countdowns are integer seconds. Round the native continuous
        // countdown up so the next source second has not elapsed prematurely.
        const double Seconds = std::ceil(FMath::Max(0., Remaining));
        return Seconds <= 300. ? 0 : static_cast<int32>(FMath::Min(std::ceil(Seconds * 20. / 3600.), std::floor(std::sqrt(Seconds * .8))));
    }
    bool SavedWhole(const FObject& O, const TCHAR* Key, double Min, double Max, double& Value)
    {
        return O.IsValid() && O->TryGetNumberField(Key, Value) && FMath::IsFinite(Value)
            && Value >= Min && Value <= Max && std::floor(Value) == Value;
    }
}

int32 FBYMSimulation::FImpl::YardWidth(const FYard& Y) const
{ return Y.MapWidth > 0 ? Y.MapWidth : BYMStoreRules::ExpandedCells(FMath::CeilToInt(Cells() * 1.25), Y.ExpansionLevel); }
int32 FBYMSimulation::FImpl::YardHeight(const FYard& Y) const
{ return Y.MapHeight > 0 ? Y.MapHeight : BYMStoreRules::ExpandedCells(Cells(), Y.ExpansionLevel); }
int32 FBYMSimulation::FImpl::YardMinX(const FYard& Y) const
{ return Y.MapWidth > 0 ? Y.MapMinX : static_cast<int32>(std::floor((FMath::CeilToInt(Cells() * 1.25) - YardWidth(Y)) * .5)); }
int32 FBYMSimulation::FImpl::YardMinY(const FYard& Y) const
{ return Y.MapHeight > 0 ? Y.MapMinY : static_cast<int32>(std::floor((Cells() - YardHeight(Y)) * .5)); }

bool FBYMSimulation::FImpl::LoadEconomyRules(FString& Error)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
    FString Text; FObject Rules;
    if (!Plugin.IsValid() || !FFileHelper::LoadFileToString(Text, *FPaths::Combine(Plugin->GetContentDir(), TEXT("Data/store-rules.json"))) || !Decode(Text, Rules))
    { Error = TEXT("Original store rules are missing or invalid. Restore Content/Data/store-rules.json from the full source package."); return false; }
    int32 Schema = 0;
    if (!Integer(Rules, TEXT("schema"), 1, 1, Schema)) { Error = TEXT("Unsupported original store-rules schema."); return false; }
    DecorationProducts.Reset(); TSet<int32> Seen;
    for (const TSharedPtr<FJsonValue>& Value : Array(Rules, TEXT("decorations")))
    {
        const FObject Item = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : FObject();
        FDecorationProduct Product;
        if (!Integer(Item, TEXT("type"), 1, 100000, Product.Type) || Seen.Contains(Product.Type)
            || !Integer(Item, TEXT("price"), 1, 1000000, Product.Price) || !Integer(Item, TEXT("limit"), 0, 1000, Product.Limit))
        { Error = TEXT("Invalid or duplicate original decoration product."); return false; }
        const FBuildingDef* Definition = Buildings.Find(Product.Type);
        if (!Definition || Definition->Type != TEXT("decoration") || Definition->Levels.IsEmpty())
        { Error = TEXT("A decoration product has no original building definition."); return false; }
        Product.Name = Str(Item, TEXT("name"), Definition->Name); Product.Texture = Str(Item, TEXT("texture"), Definition->Texture);
        Product.MenuTexture = Str(Item, TEXT("menuTexture"), Product.Texture); Product.Category = Str(Item, TEXT("category"));
        if (Product.Texture.IsEmpty()) { Error = TEXT("An original decoration product has no artwork."); return false; }
        Seen.Add(Product.Type); DecorationProducts.Add(MoveTemp(Product));
    }
    if (DecorationProducts.IsEmpty()) { Error = TEXT("The original decoration shop is empty."); return false; }
    DecorationProducts.Sort([](const FDecorationProduct& A, const FDecorationProduct& B) { return A.Type < B.Type; });
    return true;
}

bool FBYMSimulation::FImpl::EconomyFits(const FYard& Y, double X, double PosY, double Size, const FString& Ignore) const
{
    // Mushroom collision uses the original 30-pixel footprint, not its thumbnail.
    for (const FYard::FMushroom& M : Y.Mushrooms)
        if (M.Id != Ignore && X < M.X + 1.5 && X + Size > M.X && PosY < M.Y + 1.5 && PosY + Size > M.Y) return false;
    return true;
}

bool FBYMSimulation::FImpl::SpawnMushroom(FYard& Y, bool bInitial)
{
    if (Y.Mushrooms.Num() >= 20) return false;
    const double MinX = YardMinX(Y) - 10., MinY = YardMinY(Y) - 10.;
    const double Width = YardWidth(Y) + 20., Height = YardHeight(Y) + 20.;
    for (int32 Attempt = 0; Attempt < 5000; ++Attempt)
    {
        FYard::FMushroom M;
        M.X = std::floor((MinX + BYMStoreRules::UnitRandom(Y.MushroomRandom) * Width) * 2.) * .5;
        M.Y = std::floor((MinY + BYMStoreRules::UnitRandom(Y.MushroomRandom) * Height) * 2.) * .5;
        // Initial mushrooms occupy the same surrounding terrain collar as the
        // original setup. Collision-safe host placement replaces Flash's five
        // unconstrained screen-space clusters and preserves existing paid yards.
        if (bInitial && M.X >= YardMinX(Y) + 3 && M.Y >= YardMinY(Y) + 3
            && M.X < YardMinX(Y) + YardWidth(Y) - 3 && M.Y < YardMinY(Y) + YardHeight(Y) - 3) continue;
        bool bBlocked = !EconomyFits(Y, M.X, M.Y, 1.5);
        for (const FBuilding& B : Y.Buildings)
        {
            const double Size = Footprint(B);
            if (M.X < B.X + Size && M.X + 1.5 > B.X && M.Y < B.Y + Size && M.Y + 1.5 > B.Y) { bBlocked = true; break; }
        }
        if (bBlocked) continue;
        M.Id = NewId(); M.Frame = 1 + static_cast<int32>(BYMStoreRules::NextRandom(Y.MushroomRandom) % 5U);
        if (BYMStoreRules::NextRandom(Y.MushroomRandom) % 4U == 0U)
            M.Reward = BYMStoreRules::NextRandom(Y.MushroomRandom) % 3U == 1U ? 8 : 3;
        Y.Mushrooms.Add(MoveTemp(M)); return true;
    }
    return false;
}

void FBYMSimulation::FImpl::InitializeEconomy(FYard& Y, bool bLegacy)
{
    (void)bLegacy;
    Y.Shiny = FMath::Clamp<int64>(GetDefault<UBYMSettings>()->StartingShiny, 0, BYMStoreRules::ShinyLimit);
    Y.PurchasedWorkers = 0; Y.ExpansionLevel = 0; Y.PackingLevel = 0;
    Y.MushroomRandom = GetTypeHash(NewId()) | 1U; Y.MushroomLastGrowth = Now;
    Y.Mushrooms.Reset(); Y.MushroomsPicked = 0; Y.GoldenMushroomsPicked = 0; Y.MushroomShinyEarned = 0;
    for (int32 I = 0; I < 20; ++I) SpawnMushroom(Y, true);
}

bool FBYMSimulation::FImpl::ReadEconomy(FYard& Y, const FObject& Source, FString& Error)
{
    // Missing extension is a legacy save. The caller initializes it after all
    // buildings have loaded, so mushrooms cannot overlap an existing layout.
    if (!Source->HasField(TEXT("economy"))) return true;
    const FObject O = Child(Source, TEXT("economy"));
    auto Fail = [&Error]() { Error = TEXT("Invalid saved Shiny, store purchases or mushroom state. The original save has been preserved."); return false; };
    int32 Version = 0; double Value = 0;
    if (!Integer(O, TEXT("version"), 1, 1, Version)
        || !BYMStoreRules::SavedWhole(O, TEXT("shiny"), 0, BYMStoreRules::ShinyLimit, Value)) return Fail();
    Y.Shiny = static_cast<int64>(Value);
    if (!Integer(O, TEXT("purchasedWorkers"), 0, 4, Y.PurchasedWorkers)
        || !Integer(O, TEXT("expansionLevel"), 0, 6, Y.ExpansionLevel)
        || !Integer(O, TEXT("packingLevel"), 0, 10, Y.PackingLevel)
        || !BYMStoreRules::SavedWhole(O, TEXT("mushroomRandom"), 1, 4294967295., Value)) return Fail();
    Y.MushroomRandom = static_cast<uint32>(Value);
    if (!O->TryGetNumberField(TEXT("lastGrowth"), Y.MushroomLastGrowth) || !FMath::IsFinite(Y.MushroomLastGrowth)
        || Y.MushroomLastGrowth < 0 || Y.MushroomLastGrowth > Now + 86400.
        || !Integer(O, TEXT("mushroomsPicked"), 0, MAX_int32, Y.MushroomsPicked)
        || !Integer(O, TEXT("goldenMushroomsPicked"), 0, Y.MushroomsPicked, Y.GoldenMushroomsPicked)
        || !BYMStoreRules::SavedWhole(O, TEXT("mushroomShinyEarned"), 0, BYMStoreRules::ShinyLimit, Value)) return Fail();
    Y.MushroomShinyEarned = static_cast<int64>(Value);
    const FArray* Items = nullptr;
    if (!O->TryGetArrayField(TEXT("mushrooms"), Items) || !Items || Items->Num() > 20) return Fail();
    TSet<FString> Ids;
    for (const TSharedPtr<FJsonValue>& Entry : *Items)
    {
        const FObject M = Entry.IsValid() && Entry->Type == EJson::Object ? Entry->AsObject() : FObject(); FYard::FMushroom Saved;
        Saved.Id = Str(M, TEXT("id"));
        if (Saved.Id.IsEmpty() || Saved.Id.Len() > 80 || Ids.Contains(Saved.Id)
            || !Integer(M, TEXT("frame"), 1, 5, Saved.Frame) || !Integer(M, TEXT("reward"), 0, 8, Saved.Reward)
            || (Saved.Reward != 0 && Saved.Reward != 3 && Saved.Reward != 8)
            || !M->TryGetBoolField(TEXT("picking"), Saved.bPicking)
            || !M->TryGetNumberField(TEXT("x"), Saved.X) || !FMath::IsFinite(Saved.X)
            || !M->TryGetNumberField(TEXT("y"), Saved.Y) || !FMath::IsFinite(Saved.Y)
            || Saved.X < YardMinX(Y) - 10 || Saved.X > YardMinX(Y) + YardWidth(Y) + 10
            || Saved.Y < YardMinY(Y) - 10 || Saved.Y > YardMinY(Y) + YardHeight(Y) + 10
            || !M->TryGetNumberField(TEXT("pickFinishAt"), Saved.PickFinishAt) || !FMath::IsFinite(Saved.PickFinishAt)
            || Saved.PickFinishAt < 0 || Saved.PickFinishAt > Now + 31536000.
            || (Saved.bPicking != (Saved.PickFinishAt > 0)) || !EconomyFits(Y, Saved.X, Saved.Y, 1.5)) return Fail();
        Ids.Add(Saved.Id); Y.Mushrooms.Add(MoveTemp(Saved));
    }
    return true;
}

void FBYMSimulation::FImpl::WriteEconomy(const FYard& Y, const FObject& Object) const
{
    FObject O = Obj(); O->SetNumberField(TEXT("version"), 1);
    O->SetNumberField(TEXT("shiny"), static_cast<double>(Y.Shiny)); O->SetNumberField(TEXT("purchasedWorkers"), Y.PurchasedWorkers);
    O->SetNumberField(TEXT("expansionLevel"), Y.ExpansionLevel); O->SetNumberField(TEXT("packingLevel"), Y.PackingLevel);
    O->SetNumberField(TEXT("mushroomRandom"), static_cast<double>(Y.MushroomRandom)); O->SetNumberField(TEXT("lastGrowth"), Y.MushroomLastGrowth);
    O->SetNumberField(TEXT("mushroomsPicked"), Y.MushroomsPicked); O->SetNumberField(TEXT("goldenMushroomsPicked"), Y.GoldenMushroomsPicked);
    O->SetNumberField(TEXT("mushroomShinyEarned"), static_cast<double>(Y.MushroomShinyEarned)); FArray Mushrooms;
    for (const FYard::FMushroom& M : Y.Mushrooms)
    {
        FObject Item = Obj(); Item->SetStringField(TEXT("id"), M.Id); Item->SetNumberField(TEXT("x"), M.X); Item->SetNumberField(TEXT("y"), M.Y);
        Item->SetNumberField(TEXT("frame"), M.Frame); Item->SetNumberField(TEXT("reward"), M.Reward);
        Item->SetBoolField(TEXT("picking"), M.bPicking); Item->SetNumberField(TEXT("pickFinishAt"), M.PickFinishAt);
        Mushrooms.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("mushrooms"), Mushrooms); Object->SetObjectField(TEXT("economy"), O);
}

FString FBYMSimulation::FImpl::StoreReason(const FYard& Y) const
{
    if (Busy(Y.Id)) return TEXT("Return to your yard after the battle to use the General Store.");
    bool bExists = false;
    for (const FBuilding& B : Y.Buildings) if (B.Type == 12)
    {
        bExists = true; const FLevel* L = Stats(B);
        if (L && B.Level >= 1 && B.HP >= L->HP * .5) return FString();
    }
    return bExists ? TEXT("Finish building or repair the General Store to at least half health.") : TEXT("Build a General Store to spend Shiny.");
}
FString FBYMSimulation::FImpl::MushroomClearReason(const FYard& Y) const
{
    if (Busy(Y.Id)) return TEXT("Finish the battle before picking mushrooms.");
    if (AvailableWorkers(Y) <= 0) return TEXT("All workers are busy. Wait for a worker to finish.");
    return FString();
}

FString FBYMSimulation::FImpl::BuildingSpeedupReason(const FYard& Y, const FBuilding& B, const FString& Code) const
{
    // Source selected-building speedup shortcuts do not require a General
    // Store; the ordinary product shop has a separate building/health gate.
    if (Busy(Y.Id)) return TEXT("Finish the battle before speeding up building work.");
    if (!HasWork(B) || B.JobId.IsEmpty() || B.WorkRemaining <= 0.) return TEXT("That construction or upgrade has already finished.");
    // Repair speedups select a different source target and use a different
    // health formula. They are not represented as construction purchases.
    if (B.bRepairing) return TEXT("Finish the current repair before speeding up this construction or upgrade.");
    if (B.SpeedupRevision >= 1000000000) return TEXT("This job cannot accept another speedup purchase.");
    const double Seconds = std::ceil(B.WorkRemaining);
    if (Code == TEXT("SP1")) { if (Seconds > 300.) return TEXT("More than five minutes remain."); }
    else if (Code == TEXT("SP2")) { if (Seconds < 3600.) return TEXT("Less than one hour remains. Choose Finish Now instead."); }
    else if (Code == TEXT("SP3")) { if (Seconds < 7200.) return TEXT("Less than two hours remain. Choose Finish Now instead."); }
    else if (Code == TEXT("SP4")) { if (Seconds <= 300.) return TEXT("Use the free Close Enough option."); }
    else return TEXT("That speedup is unavailable.");
    if (Y.Shiny < BYMStoreRules::SpeedupPrice(Code, B.WorkRemaining)) return TEXT("Not enough Shiny.");
    return FString();
}

FObject FBYMSimulation::FImpl::BuildingSpeedupOptions(const FYard& Y, const FBuilding& B) const
{
    FObject Job = Obj(); const FBuildingDef* Definition = Buildings.Find(B.Type);
    Job->SetStringField(TEXT("building"), B.Id); Job->SetStringField(TEXT("job"), B.JobId);
    Job->SetStringField(TEXT("name"), Definition ? Definition->Name : FString(TEXT("Building")));
    Job->SetNumberField(TEXT("remaining"), B.WorkRemaining); Job->SetNumberField(TEXT("revision"), B.SpeedupRevision);
    static const TCHAR* Codes[] = {TEXT("SP1"), TEXT("SP2"), TEXT("SP3"), TEXT("SP4")};
    static const TCHAR* Names[] = {TEXT("Close enough"), TEXT("Reduce by 1 hour"), TEXT("Reduce by 2 hours"), TEXT("FINISH NOW!")};
    FArray Options;
    for (int32 I = 0; I < 4; ++I)
    {
        FObject Option = Obj(); Option->SetStringField(TEXT("code"), Codes[I]); Option->SetStringField(TEXT("name"), Names[I]);
        Option->SetNumberField(TEXT("price"), BYMStoreRules::SpeedupPrice(Codes[I], B.WorkRemaining));
        const FString Reason = BuildingSpeedupReason(Y, B, Codes[I]); Option->SetBoolField(TEXT("canBuy"), Reason.IsEmpty()); Option->SetStringField(TEXT("reason"), Reason);
        Options.Add(MakeShared<FJsonValueObject>(Option));
    }
    Job->SetArrayField(TEXT("options"), Options); return Job;
}

FObject FBYMSimulation::FImpl::EconomySnapshot(const FYard& Y, bool bPrivate) const
{
    FObject O = Obj(); O->SetNumberField(TEXT("width"), YardWidth(Y)); O->SetNumberField(TEXT("height"), YardHeight(Y));
    O->SetNumberField(TEXT("minX"), YardMinX(Y)); O->SetNumberField(TEXT("minY"), YardMinY(Y));
    O->SetNumberField(TEXT("maxX"), YardMinX(Y) + YardWidth(Y)); O->SetNumberField(TEXT("maxY"), YardMinY(Y) + YardHeight(Y));
    FArray Mushrooms; const FString ClearReason = bPrivate ? MushroomClearReason(Y) : FString();
    for (const FYard::FMushroom& M : Y.Mushrooms)
    {
        FObject Item = Obj(); Item->SetStringField(TEXT("id"), M.Id); Item->SetNumberField(TEXT("x"), M.X); Item->SetNumberField(TEXT("y"), M.Y);
        Item->SetNumberField(TEXT("frame"), M.Frame); Item->SetBoolField(TEXT("picking"), M.bPicking);
        Item->SetNumberField(TEXT("remaining"), M.bPicking ? FMath::Max(0., M.PickFinishAt - Now) : 0.);
        Item->SetNumberField(TEXT("pickFinishAt"), M.PickFinishAt);
        if (bPrivate)
        {
            const FString Reason = M.bPicking ? FString(TEXT("A worker is already picking this mushroom.")) : ClearReason;
            Item->SetBoolField(TEXT("canClear"), Reason.IsEmpty()); Item->SetStringField(TEXT("reason"), Reason);
        }
        Mushrooms.Add(MakeShared<FJsonValueObject>(Item));
    }
    O->SetArrayField(TEXT("mushrooms"), Mushrooms);
    if (!bPrivate) return O;
    O->SetNumberField(TEXT("shiny"), static_cast<double>(Y.Shiny)); O->SetNumberField(TEXT("workersPurchased"), Y.PurchasedWorkers);
    O->SetNumberField(TEXT("expansionLevel"), Y.ExpansionLevel); O->SetNumberField(TEXT("packingLevel"), Y.PackingLevel);
    O->SetNumberField(TEXT("nextMushroomAt"), Y.Mushrooms.Num() < 10 ? Y.MushroomLastGrowth + BYMStoreRules::GrowthInterval : 0.);
    O->SetNumberField(TEXT("mushroomsPicked"), Y.MushroomsPicked); O->SetNumberField(TEXT("mushroomShinyEarned"), static_cast<double>(Y.MushroomShinyEarned));
    O->SetBoolField(TEXT("canClearMushrooms"), ClearReason.IsEmpty()); O->SetStringField(TEXT("clearReason"), ClearReason);
    const FString ShopReason = StoreReason(Y); O->SetBoolField(TEXT("storeAvailable"), ShopReason.IsEmpty()); O->SetStringField(TEXT("storeReason"), ShopReason);
    FArray Products;
    auto Product = [&](const TCHAR* Code, const TCHAR* Name, const TCHAR* Description, int32 Owned, int32 Max, int32 Price, const TCHAR* Category)
    {
        FObject P = Obj(); P->SetStringField(TEXT("code"), Code); P->SetStringField(TEXT("name"), Name); P->SetStringField(TEXT("description"), Description);
        P->SetStringField(TEXT("category"), Category); P->SetNumberField(TEXT("price"), Price); P->SetNumberField(TEXT("owned"), Owned); P->SetNumberField(TEXT("max"), Max);
        FString Reason = ShopReason;
        if (Reason.IsEmpty() && Owned >= Max) Reason = TEXT("You already own the maximum.");
        if (Reason.IsEmpty() && Y.Shiny < Price) Reason = TEXT("Not enough Shiny.");
        P->SetBoolField(TEXT("canBuy"), Reason.IsEmpty()); P->SetStringField(TEXT("reason"), Reason); Products.Add(MakeShared<FJsonValueObject>(P));
    };
    Product(TEXT("BEW"), TEXT("1 Extra Worker"), TEXT("One extra worker to help you build and upgrade your yard. Maximum five workers."),
        Y.PurchasedWorkers, 4, Y.PurchasedWorkers < 4 ? BYMStoreRules::WorkerPrices[Y.PurchasedWorkers] : 0, TEXT("Construction"));
    Product(TEXT("ENL"), TEXT("More Yardage"), TEXT("Increase the width and height of your yard by 10% to make room for more stuff."),
        Y.ExpansionLevel, 6, Y.ExpansionLevel < 6 ? BYMStoreRules::ExpansionPrices[Y.ExpansionLevel] : 0, TEXT("Construction"));
    Product(TEXT("BIP"), TEXT("Improved packing skills"), TEXT("Improves the capacity of all resources by another 10%."),
        Y.PackingLevel, 10, Y.PackingLevel < 10 ? (Y.PackingLevel + 1) * 50 : 0, TEXT("Resources"));
    O->SetArrayField(TEXT("products"), Products);
    FArray Speedups;
    for (const FBuilding& B : Y.Buildings) if (HasWork(B)) Speedups.Add(MakeShared<FJsonValueObject>(BuildingSpeedupOptions(Y, B)));
    O->SetArrayField(TEXT("speedups"), Speedups);
    FArray Decorations;
    for (const FDecorationProduct& D : DecorationProducts)
    {
        const FBuildingDef* Definition = Buildings.Find(D.Type); if (!Definition) continue;
        FObject P = Obj(); P->SetNumberField(TEXT("type"), D.Type); P->SetStringField(TEXT("name"), D.Name); P->SetNumberField(TEXT("price"), D.Price);
        P->SetStringField(TEXT("texture"), D.Texture); P->SetStringField(TEXT("menuTexture"), D.MenuTexture); P->SetStringField(TEXT("category"), D.Category);
        P->SetNumberField(TEXT("cells"), Definition->Cells); P->SetNumberField(TEXT("limit"), D.Limit); P->SetNumberField(TEXT("owned"), CountOf(Y, D.Type));
        FString Reason = ShopReason;
        if (Reason.IsEmpty() && Y.Buildings.Num() >= FMath::Clamp(GetDefault<UBYMSettings>()->MaxBuildingsPerYard, 1, 1000)) Reason = TEXT("The yard building limit has been reached.");
        if (Reason.IsEmpty() && D.Limit > 0 && CountOf(Y, D.Type) >= D.Limit) Reason = TEXT("You already own the maximum of this decoration.");
        if (Reason.IsEmpty() && Y.Shiny < D.Price) Reason = TEXT("Not enough Shiny.");
        P->SetBoolField(TEXT("canBuy"), Reason.IsEmpty()); P->SetStringField(TEXT("reason"), Reason); Decorations.Add(MakeShared<FJsonValueObject>(P));
    }
    O->SetArrayField(TEXT("decorations"), Decorations); return O;
}

bool FBYMSimulation::FImpl::HasMushroomTask(const FYard& Y, const FString& Id) const
{ return Y.Mushrooms.ContainsByPredicate([&Id](const FYard::FMushroom& M) { return M.Id == Id && M.bPicking; }); }

double FBYMSimulation::FImpl::EconomyNextBoundary(const FYard& Y, double Cursor, double End) const
{
    double Boundary = End;
    for (const FYard::FMushroom& M : Y.Mushrooms) if (M.bPicking && M.PickFinishAt > Cursor) Boundary = FMath::Min(Boundary, M.PickFinishAt);
    // Growth is processed in bounded catch-up batches, not one loop per interval.
    return Boundary;
}

void FBYMSimulation::FImpl::AdvanceEconomy(FYard& Y, double End)
{
    for (int32 Index = Y.Mushrooms.Num() - 1; Index >= 0; --Index)
    {
        const FYard::FMushroom& M = Y.Mushrooms[Index];
        if (!M.bPicking || M.PickFinishAt > End || !IsMushroomWorkerArrived(Y, M.Id, End)) continue;
        const FString Id = M.Id; const int32 Award = M.Reward;
        Y.Shiny = FMath::Min<int64>(BYMStoreRules::ShinyLimit, Y.Shiny + Award);
        Y.MushroomsPicked = FMath::Min(Y.MushroomsPicked, MAX_int32 - 1) + 1;
        if (Award > 0) Y.GoldenMushroomsPicked = FMath::Min(Y.GoldenMushroomsPicked, MAX_int32 - 1) + 1;
        Y.MushroomShinyEarned = FMath::Min<int64>(BYMStoreRules::ShinyLimit, Y.MushroomShinyEarned + Award);
        Y.Mushrooms.RemoveAt(Index); ReleaseMushroomWorker(Y, Id, End);
        FObject Log = Obj(); Log->SetStringField(TEXT("kind"), TEXT("mushroom")); Log->SetNumberField(TEXT("at"), End); Log->SetNumberField(TEXT("shiny"), Award);
        Log->SetStringField(TEXT("message"), Award > 0 ? FString::Printf(TEXT("Found %d Shiny in a golden mushroom."), Award) : FString(TEXT("Mushroom picked. No Shiny this time.")));
        Y.Log.Add(Log); if (Y.Log.Num() > 100) Y.Log.RemoveAt(0, Y.Log.Num() - 100);
    }
    if (End < Y.MushroomLastGrowth + BYMStoreRules::GrowthInterval) return;
    const int32 Due = static_cast<int32>(FMath::Min(10., std::floor((End - Y.MushroomLastGrowth) / BYMStoreRules::GrowthInterval)));
    Y.MushroomLastGrowth = End;
    const int32 Count = FMath::Min(Due, FMath::Max(0, 10 - Y.Mushrooms.Num()));
    for (int32 I = 0; I < Count; ++I) SpawnMushroom(Y, false);
}

bool FBYMSimulation::FImpl::HandlesEconomyCommand(const FString& Op) const
{ return Op == TEXT("shopBuy") || Op == TEXT("decorate") || Op == TEXT("clearmushroom") || Op == TEXT("shopSpeedup"); }

FString FBYMSimulation::FImpl::ExecuteEconomy(FYard& Y, const FString& Op, const FObject& Params)
{
    if (Op == TEXT("clearmushroom"))
    {
        const FString Id = Str(Params, TEXT("id"));
        FYard::FMushroom* Mushroom = Y.Mushrooms.FindByPredicate([&Id](const FYard::FMushroom& M) { return M.Id == Id; });
        if (!Mushroom) return Reply(false, TEXT("That mushroom no longer exists in your yard."));
        if (Mushroom->bPicking) return Reply(false, TEXT("A worker is already picking that mushroom."));
        const FString Reason = MushroomClearReason(Y); if (!Reason.IsEmpty()) return Reply(false, Reason);
        double Arrival = 0;
        if (!AssignMushroomWorker(Y, Id, FVector2D(Mushroom->X + .75, Mushroom->Y + .75), Arrival))
            return Reply(false, TEXT("All workers are busy. Wait for a worker to finish."));
        Mushroom->bPicking = true; Mushroom->PickFinishAt = Arrival + BYMStoreRules::PickSeconds;
        return Reply(true, TEXT("A worker is on the way to pick the mushroom."));
    }
    if (Op == TEXT("shopSpeedup"))
    {
        FBuilding* Building = FindBuilding(Y, Str(Params, TEXT("building"))); const FString Job = Str(Params, TEXT("job"));
        const FString Code = Str(Params, TEXT("code")); int32 Revision = -1, MaxPrice = -1;
        if (!Building || Job.IsEmpty() || Job != Building->JobId
            || !Integer(Params, TEXT("revision"), 0, 1000000000, Revision) || Revision != Building->SpeedupRevision
            || !Integer(Params, TEXT("maxPrice"), 0, 1000000000, MaxPrice))
            return Reply(false, TEXT("That building job or speedup quote has changed. Review it before purchasing."));
        const FString Error = BuildingSpeedupReason(Y, *Building, Code); if (!Error.IsEmpty()) return Reply(false, Error);
        const int32 Price = BYMStoreRules::SpeedupPrice(Code, Building->WorkRemaining);
        if (Price > MaxPrice) return Reply(false, TEXT("The current price exceeds your displayed quote. Review it before purchasing."));
        Y.Shiny -= Price; ++Building->SpeedupRevision;
        if (Code == TEXT("SP1") || Code == TEXT("SP4")) Building->WorkRemaining = 0.;
        else Building->WorkRemaining = FMath::Max(0., Building->WorkRemaining - (Code == TEXT("SP2") ? 3600. : 7200.));
        if (Building->WorkRemaining <= 0.)
        {
            // Source BuyB calls Constructed/Upgraded directly: completion does
            // not wait for worker arrival or require undamaged paid work.
            CompleteBuildingWork(Y, *Building, Now); RefreshBaseValue(Y);
            return Reply(true, TEXT("Building work finished."));
        }
        return Reply(true, TEXT("Building work shortened."));
    }
    const FString Reason = StoreReason(Y); if (!Reason.IsEmpty()) return Reply(false, Reason);
    if (Op == TEXT("shopBuy"))
    {
        const FString Code = Str(Params, TEXT("code")); int32 Expected = -1;
        if (!Integer(Params, TEXT("expectedLevel"), 0, 10, Expected)) return Reply(false, TEXT("Refresh the store price before purchasing."));
        int32* Owned = nullptr; int32 Maximum = 0, Price = 0;
        if (Code == TEXT("BEW")) { Owned = &Y.PurchasedWorkers; Maximum = 4; if (*Owned < Maximum) Price = BYMStoreRules::WorkerPrices[*Owned]; }
        else if (Code == TEXT("ENL")) { Owned = &Y.ExpansionLevel; Maximum = 6; if (*Owned < Maximum) Price = BYMStoreRules::ExpansionPrices[*Owned]; }
        else if (Code == TEXT("BIP")) { Owned = &Y.PackingLevel; Maximum = 10; if (*Owned < Maximum) Price = (*Owned + 1) * 50; }
        else return Reply(false, TEXT("That store product is unavailable."));
        if (Expected != *Owned) return Reply(false, TEXT("That purchase tier has changed. Review the current store price."));
        if (*Owned >= Maximum) return Reply(false, TEXT("You already own the maximum."));
        if (Price <= 0 || Y.Shiny < Price) return Reply(false, TEXT("Not enough Shiny."));
        Y.Shiny -= Price; ++*Owned;
        if (Code == TEXT("BEW")) SyncWorkers(Y, Now);
        return Reply(true, Code == TEXT("BEW") ? TEXT("Extra worker hired.") : (Code == TEXT("ENL") ? TEXT("Your yard has expanded.") : TEXT("Resource packing capacity improved.")));
    }
    if (Op == TEXT("decorate"))
    {
        int32 Type = 0, X = 0, PosY = 0;
        if (!Integer(Params, TEXT("type"), 1, 100000, Type)
            || !Integer(Params, TEXT("x"), YardMinX(Y), YardMinX(Y) + YardWidth(Y), X)
            || !Integer(Params, TEXT("y"), YardMinY(Y), YardMinY(Y) + YardHeight(Y), PosY)) return Reply(false, TEXT("Invalid decoration or placement coordinates."));
        const FDecorationProduct* Product = DecorationProducts.FindByPredicate([Type](const FDecorationProduct& P) { return P.Type == Type; });
        const FBuildingDef* Definition = Buildings.Find(Type);
        if (!Product || !Definition || Definition->Levels.IsEmpty()) return Reply(false, TEXT("That decoration is unavailable in the original shop."));
        if (Y.Buildings.Num() >= FMath::Clamp(GetDefault<UBYMSettings>()->MaxBuildingsPerYard, 1, 1000)
            || (Product->Limit > 0 && CountOf(Y, Type) >= Product->Limit)) return Reply(false, TEXT("The decoration or yard limit has been reached."));
        if (Y.Shiny < Product->Price) return Reply(false, TEXT("Not enough Shiny."));
        if (!Fits(Y, X, PosY, Definition->Cells)) return Reply(false, TEXT("Choose an empty location inside your yard border."));
        FBuilding Building; Building.Id = NewId(); Building.Type = Type; Building.Level = 1; Building.TargetLevel = 1;
        Building.X = X; Building.Y = PosY; Building.HP = Definition->Levels[0].HP;
        Y.Shiny -= Product->Price; Y.Buildings.Add(MoveTemp(Building));
        return Reply(true, TEXT("Decoration placed."));
    }
    return Reply(false, TEXT("Unknown store action."));
}

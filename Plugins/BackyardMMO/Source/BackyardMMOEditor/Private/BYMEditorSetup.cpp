#include "BYMEditorSetup.h"

#include "BYMBoard.h"
#include "BYMGameInstance.h"
#include "BYMGameMode.h"
#include "BYMMenuGameMode.h"
#include "BYMSettings.h"
#include "BYMTextureImportSettings.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/UnrealEdTypes.h"
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Factories/SoundFactory.h"
#include "Factories/TextureFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/MessageLog.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/ProjectPackagingSettings.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogBYMSetup, Log, All);

namespace
{
    bool GSetupBusy = false;
    constexpr const TCHAR* SpriteMaterial = TEXT("/BackyardMMO/Materials/M_Sprite");
    constexpr const TCHAR* AlphaSpriteMaterial = TEXT("/BackyardMMO/Materials/M_SpriteAlpha");
    constexpr const TCHAR* ImportCacheName = TEXT("BackyardMMO/ImportCache.json");

    FString PluginRoot()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BackyardMMO"));
        return Plugin ? Plugin->GetBaseDir() : FString();
    }

    bool PackageExists(const FString& Package)
    {
        return FPackageName::DoesPackageExist(Package);
    }

    TSharedPtr<FJsonObject> ReadJson(const FString& File)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        if (FFileHelper::LoadFileToString(Text, *File))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

    bool WriteJson(const FString& File, const TSharedRef<FJsonObject>& Json)
    {
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        return FJsonSerializer::Serialize(Json, Writer)
            && FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    FString SourceDigest(const FString& File)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *File)) return FString();
        FMD5 Hasher;
        Hasher.Update(Bytes.GetData(), Bytes.Num());
        uint8 Digest[16];
        Hasher.Final(Digest);
        return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
    }

    bool SaveAsset(UObject* Asset, FBYMSetupReport& Report, bool bMap = false)
    {
        if (!Asset) return false;
        UPackage* Package = Asset->GetOutermost();
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
            bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
        {
            Report.Errors.Add(TEXT("Cannot resolve package path: ") + Package->GetName());
            return false;
        }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
        {
            Report.Errors.Add(TEXT("Could not save generated asset: ") + Filename);
            return false;
        }
        return true;
    }

    void FinalizeTextureDefaults(const TArray<TPair<FString, FString>>& Textures, FBYMSetupReport& Report)
    {
        if (Textures.IsEmpty()) return;
        FScopedSlowTask Progress(static_cast<float>(Textures.Num()),
            NSLOCTEXT("BackyardMMO", "TextureDefaultsProgress", "Finalizing color texture defaults"));
        // This finalization is part of import completion, including after a canceled
        // import. Only paths reached by the import loop are included in that case.
        if (!IsRunningCommandlet()) Progress.MakeDialog(false);
        FTextureCompilingManager::Get().FinishAllCompilation();
        for (const TPair<FString, FString>& Entry : Textures)
        {
            Progress.EnterProgressFrame(1.0f, FText::FromString(Entry.Value));
            TStrongObjectPtr<UTexture2D> Texture(LoadObject<UTexture2D>(nullptr, *Entry.Key));
            if (!Texture.IsValid())
            {
                Report.Errors.Add(TEXT("Cannot finalize color texture defaults: ") + Entry.Value);
                continue;
            }
            const bool bChanged = BYMTextureImportSettings::ApplyColorDefaults(*Texture.Get(), Entry.Value);
            if (!BYMTextureImportSettings::MatchesColorDefaults(*Texture.Get(), Entry.Value))
            {
                Report.Errors.Add(TEXT("Texture did not retain Default/sRGB artwork settings: ") + Entry.Value);
                continue;
            }
            // Unchanged source hashes never bypass this pass. Already-correct
            // textures need no rebuild; corrected or unsaved packages are saved.
            if (bChanged || Texture->GetOutermost()->IsDirty())
            {
                Texture->MarkPackageDirty();
                if (!SaveAsset(Texture.Get(), Report)) continue;
            }
            ++Report.TextureDefaultsVerified;
            if (bChanged) ++Report.TextureDefaultsCorrected;
        }
        Report.Notes.Add(FString::Printf(TEXT("Final texture pass: %d color defaults verified, %d corrected after import (including cached assets). Import-time normal-map notices do not describe the finalized settings."),
            Report.TextureDefaultsVerified, Report.TextureDefaultsCorrected));
    }

    void EnsurePrimitiveDepth(AActor* Actor)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
        for (UPrimitiveComponent* Component : Components)
        {
            Component->SetRenderCustomDepth(true);
            Component->SetCustomDepthStencilValue(1);
        }
    }

    bool ValidMapName(const FString& Name)
    {
        return Name.StartsWith(TEXT("/BackyardMMO/Maps/")) && FPackageName::IsValidLongPackageName(Name);
    }

    FBYMSetupReport BusyReport()
    {
        FBYMSetupReport Result;
        Result.Errors.Add(TEXT("A Backyard Monsters NXT setup operation is already running."));
        return Result;
    }
}

FString FBYMSetupReport::Summary() const
{
    return FString::Printf(TEXT("%s: %d imported, %d unchanged, %d texture defaults verified, %d corrected, %d maps created, %d errors."),
        Succeeded() ? TEXT("Setup ready") : TEXT("Setup needs attention"), Imported, Unchanged,
        TextureDefaultsVerified, TextureDefaultsCorrected, CreatedMaps, Errors.Num());
}

bool FBYMEditorSetup::IsBusy() { return GSetupBusy; }

bool FBYMEditorSetup::NeedsFirstSetup()
{
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    if (!PackageExists(Settings->MainMenuMap) || !PackageExists(Settings->WorldMap)
        || !PackageExists(SpriteMaterial) || !PackageExists(AlphaSpriteMaterial)) return true;
    // Updated full-source releases can add animation/effect textures even when
    // maps already exist. A normal .uproject open must import that new payload.
    const FString Root = PluginRoot();
    const FString ManifestDigest = SourceDigest(Root / TEXT("Content/Data/asset-manifest.json"));
    const FString CatalogDigest = SourceDigest(Root / TEXT("Content/Data/catalog.json"));
    if (ManifestDigest.IsEmpty() || CatalogDigest.IsEmpty()) return true;
    const TSharedPtr<FJsonObject> Cache = ReadJson(FPaths::ProjectSavedDir() / ImportCacheName);
    FString ImportedRevision;
    if (Cache) Cache->TryGetStringField(TEXT("__SourceRevision"), ImportedRevision);
    return ImportedRevision != ManifestDigest + TEXT(":") + CatalogDigest;
}

FBYMSetupReport FBYMEditorSetup::SetupAll()
{
    if (GSetupBusy) return BusyReport();
    TGuardValue<bool> Guard(GSetupBusy, true);
    FBYMSetupReport Result;
    Import(Result);
    if (Result.Succeeded()) CreateMaterial(Result);
    if (Result.Succeeded()) CreateMaps(Result);
    if (Result.Succeeded()) ConfigureProject(Result);
    ValidateInternal(Result);
    WriteReport(Result);
    return Result;
}

FBYMSetupReport FBYMEditorSetup::ImportAssets()
{
    if (GSetupBusy) return BusyReport();
    TGuardValue<bool> Guard(GSetupBusy, true);
    FBYMSetupReport Result;
    Import(Result);
    if (Result.Succeeded()) CreateMaterial(Result);
    WriteReport(Result);
    return Result;
}

FBYMSetupReport FBYMEditorSetup::GenerateMaps()
{
    if (GSetupBusy) return BusyReport();
    TGuardValue<bool> Guard(GSetupBusy, true);
    FBYMSetupReport Result;
    CreateMaterial(Result);
    if (Result.Succeeded()) CreateMaps(Result);
    if (Result.Succeeded()) ConfigureProject(Result);
    WriteReport(Result);
    return Result;
}

FBYMSetupReport FBYMEditorSetup::Validate()
{
    if (GSetupBusy) return BusyReport();
    TGuardValue<bool> Guard(GSetupBusy, true);
    FBYMSetupReport Result;
    ValidateInternal(Result);
    WriteReport(Result);
    return Result;
}

void FBYMEditorSetup::Import(FBYMSetupReport& Report)
{
    const FString Root = PluginRoot();
    if (Root.IsEmpty()) { Report.Errors.Add(TEXT("The BackyardMMO plugin is not enabled.")); return; }
    const FString SourceRoot = FPaths::ConvertRelativePathToFull(Root / TEXT("SourceAssets"));
    if (!IFileManager::Get().DirectoryExists(*SourceRoot))
    {
        Report.Errors.Add(TEXT("The bundled SourceAssets folder is missing: ") + SourceRoot);
        return;
    }
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *SourceRoot, TEXT("*"), true, false, false);
    Files = Files.FilterByPredicate([](const FString& File)
    {
        const FString Extension = FPaths::GetExtension(File).ToLower();
        return Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("wav");
    });
    Files.Sort();
    if (Files.IsEmpty()) { Report.Errors.Add(TEXT("SourceAssets contains no PNG, JPG or WAV files.")); return; }

    const FString CacheFile = FPaths::ProjectSavedDir() / ImportCacheName;
    TSharedPtr<FJsonObject> Cache = ReadJson(CacheFile);
    if (!Cache) Cache = MakeShared<FJsonObject>();
    TSet<FString> ObjectPaths;
    TArray<TPair<FString, FString>> TexturePaths;
    {
        FScopedSlowTask Progress(static_cast<float>(Files.Num()), NSLOCTEXT("BackyardMMO", "ImportProgress", "Importing original Backyard Monsters assets"));
        if (!IsRunningCommandlet()) Progress.MakeDialog(true);
        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

        for (const FString& File : Files)
        {
            if (Progress.ShouldCancel())
            {
                Report.Errors.Add(TEXT("Import canceled. Successfully imported assets are retained; run Import / Refresh to resume."));
                break;
            }
            FString Relative = FPaths::ConvertRelativePathToFull(File);
            FString RelativeBase = SourceRoot + TEXT("/");
            FPaths::MakePathRelativeTo(Relative, *RelativeBase);
            FPaths::NormalizeFilename(Relative);
            Progress.EnterProgressFrame(1.0f, FText::FromString(Relative));
            const FString ObjectPath = ABYMBoard::AssetObjectPath(Relative);
            const FString Folded = ObjectPath.ToLower();
            if (ObjectPath.IsEmpty() || ObjectPaths.Contains(Folded))
            {
                Report.Errors.Add(TEXT("An invalid or colliding source asset path was found: ") + Relative);
                continue;
            }
            ObjectPaths.Add(Folded);
            const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
            const FString AssetName = FPackageName::ObjectPathToObjectName(ObjectPath);
            if (!FPackageName::IsValidLongPackageName(PackageName))
            {
                Report.Errors.Add(TEXT("Invalid generated package name for ") + Relative);
                continue;
            }
            // Queue BEFORE the source-hash cache check. Every image must pass the
            // separate finalization stage, even if no source pixels need reimporting.
            if (!FPaths::GetExtension(File).Equals(TEXT("wav"), ESearchCase::IgnoreCase))
                TexturePaths.Emplace(ObjectPath, Relative);
            const FString Digest = SourceDigest(File);
            FString OldDigest;
            Cache->TryGetStringField(Relative, OldDigest);
            // Cache never suppresses importing a missing .uasset, including after deleting Intermediate/Saved.
            if (!Digest.IsEmpty() && Digest == OldDigest && PackageExists(PackageName))
            {
                ++Report.Unchanged;
                continue;
            }
            if (Digest.IsEmpty()) { Report.Errors.Add(TEXT("Cannot read source asset: ") + Relative); continue; }

            TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
            Task->Filename = File;
            Task->DestinationName = AssetName;
            Task->DestinationPath = FPackageName::GetLongPackagePath(PackageName);
            Task->bAutomated = true;
            Task->bReplaceExisting = true;
            Task->bReplaceExistingSettings = false;
            Task->bAsync = false;
            Task->bSave = false;
            if (FPaths::GetExtension(File).Equals(TEXT("wav"), ESearchCase::IgnoreCase))
            {
                Task->Factory = NewObject<USoundFactory>(Task.Get());
            }
            else
            {
                // A concrete legacy factory ensures DestinationName is honored rather than a pipeline rename.
                UTextureFactory* Factory = NewObject<UTextureFactory>(Task.Get());
                // The factory may still classify blue/purple art as normal maps.
                // Explicit final color defaults below are authoritative, not that guess.
                Factory->CompressionSettings = TC_Default;
                Factory->LODGroup = TEXTUREGROUP_Pixels2D;
                Factory->MipGenSettings = TMGS_NoMipmaps;
                Factory->NoCompression = true;
                Factory->NoAlpha = false;
                Factory->bFlipNormalMapGreenChannel = false;
                Factory->bCreateMaterial = false;
                UTextureFactory::SuppressImportOverwriteDialog(false);
                Task->Factory = Factory;
            }
            AssetTools.ImportAssetTasks({ Task.Get() });
            UObject* Imported = nullptr;
            for (UObject* Candidate : Task->GetObjects())
            {
                if (Candidate && Candidate->GetPathName() == ObjectPath) { Imported = Candidate; break; }
            }
            if (!Imported)
            {
                Report.Errors.Add(TEXT("Import failed or returned an unexpected object path: ") + Relative);
                continue;
            }
            if (UTexture2D* Texture = Cast<UTexture2D>(Imported))
            {
                // Keep interrupted imports safe as each file is checkpointed; the
                // complete post-import pass also handles cached and late-changed assets.
                BYMTextureImportSettings::ApplyColorDefaults(*Texture, Relative);
            }
            Imported->MarkPackageDirty();
            if (SaveAsset(Imported, Report))
            {
                Cache->SetStringField(Relative, Digest);
                ++Report.Imported;
                // Persist every batch so canceled imports resume safely.
                if ((Report.Imported % 100) == 0) WriteJson(CacheFile, Cache.ToSharedRef());
            }
        }
    }
    // All ImportAssetTasks calls have returned and the import progress scope has
    // closed. Complete texture builds, enforce defaults, verify, and save before
    // Setup All can create maps or the importer can report success.
    FinalizeTextureDefaults(TexturePaths, Report);
    if (Report.Succeeded())
    {
        const FString ManifestDigest = SourceDigest(Root / TEXT("Content/Data/asset-manifest.json"));
        const FString CatalogDigest = SourceDigest(Root / TEXT("Content/Data/catalog.json"));
        if (!ManifestDigest.IsEmpty() && !CatalogDigest.IsEmpty())
            Cache->SetStringField(TEXT("__SourceRevision"), ManifestDigest + TEXT(":") + CatalogDigest);
    }
    if (!WriteJson(CacheFile, Cache.ToSharedRef())) Report.Notes.Add(TEXT("Import succeeded but the optional incremental cache could not be saved."));
    Report.Notes.Add(TEXT("Render CustomDepth is a primitive-component property, not a texture property. All Backyard Monsters NXT sprite components enable it when created."));
}

void FBYMEditorSetup::CreateMaterial(FBYMSetupReport& Report)
{
    // Ground remains masked. Artwork needs true alpha so the source shadow
    // attenuation is preserved instead of turning into an opaque black silhouette.
    for (const TCHAR* PackagePath : { SpriteMaterial, AlphaSpriteMaterial })
    {
        const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
        const bool bTranslucent = FString(PackagePath) == AlphaSpriteMaterial;
        if (PackageExists(PackagePath))
        {
            if (bTranslucent)
            {
                UMaterial* Existing = LoadObject<UMaterial>(nullptr, *(FString(PackagePath) + TEXT(".") + Name));
                if (Existing)
                {
                    // Upgrade only the exact old generated opacity connection.
                    // Preserve any custom opacity graph the project owner added.
                    auto* Data = Existing->GetEditorOnlyData();
                    auto* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Data->Opacity.Expression);
                    auto* Color = Cast<UMaterialExpressionMultiply>(Data->EmissiveColor.Expression);
                    auto* Tint = Color ? Cast<UMaterialExpressionVectorParameter>(Color->B.Expression) : nullptr;
                    if (Existing->BlendMode == BLEND_Translucent && Texture && Tint
                        && Texture->ParameterName == TEXT("SpriteTexture") && Tint->ParameterName == TEXT("SpriteTint")
                        && Data->Opacity.OutputIndex == 4 && Color->A.Expression == Texture
                        && Color->A.OutputIndex == 0 && Color->B.OutputIndex == 0)
                    {
                        Existing->Modify();
                        UMaterialExpressionMultiply* Alpha = NewObject<UMaterialExpressionMultiply>(Existing);
                        Alpha->A.Connect(4, Texture); Alpha->B.Connect(4, Tint);
                        Existing->GetExpressionCollection().AddExpression(Alpha);
                        Data->Opacity.Connect(0, Alpha);
                        Existing->PostEditChange(); Existing->MarkPackageDirty(); SaveAsset(Existing, Report);
                        Report.Notes.Add(TEXT("Enabled sprite-tint opacity on the previous generated alpha material."));
                    }
                }
            }
            Report.Notes.Add(TEXT("Preserved existing material: ") + Name);
            continue;
        }
        UPackage* Package = CreatePackage(PackagePath);
        UMaterial* Material = NewObject<UMaterial>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
        Material->BlendMode = bTranslucent ? BLEND_Translucent : BLEND_Masked;
        Material->TwoSided = true;
        Material->OpacityMaskClipValue = 0.2f;
        Material->AllowTranslucentCustomDepthWrites = bTranslucent;
        Material->SetShadingModel(MSM_Unlit);

        auto AddExpression = [Material](UMaterialExpression* Expression)
        {
            Material->GetExpressionCollection().AddExpression(Expression);
        };
        UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material);
        AddExpression(UV);
        UMaterialExpressionVectorParameter* Scale = NewObject<UMaterialExpressionVectorParameter>(Material);
        Scale->ParameterName = TEXT("UVScale");
        Scale->DefaultValue = FLinearColor(1, 1, 0, 0);
        AddExpression(Scale);
        UMaterialExpressionVectorParameter* Offset = NewObject<UMaterialExpressionVectorParameter>(Material);
        Offset->ParameterName = TEXT("UVOffset");
        Offset->DefaultValue = FLinearColor(0, 0, 0, 0);
        AddExpression(Offset);
        UMaterialExpressionComponentMask* ScaleRG = NewObject<UMaterialExpressionComponentMask>(Material);
        ScaleRG->R = true; ScaleRG->G = true; ScaleRG->B = false; ScaleRG->A = false;
        ScaleRG->Input.Connect(0, Scale); AddExpression(ScaleRG);
        UMaterialExpressionComponentMask* OffsetRG = NewObject<UMaterialExpressionComponentMask>(Material);
        OffsetRG->R = true; OffsetRG->G = true; OffsetRG->B = false; OffsetRG->A = false;
        OffsetRG->Input.Connect(0, Offset); AddExpression(OffsetRG);
        UMaterialExpressionMultiply* ScaledUV = NewObject<UMaterialExpressionMultiply>(Material);
        ScaledUV->A.Connect(0, UV); ScaledUV->B.Connect(0, ScaleRG); AddExpression(ScaledUV);
        UMaterialExpressionAdd* FinalUV = NewObject<UMaterialExpressionAdd>(Material);
        FinalUV->A.Connect(0, ScaledUV); FinalUV->B.Connect(0, OffsetRG); AddExpression(FinalUV);
    
        UMaterialExpressionTextureSampleParameter2D* Texture = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
        Texture->ParameterName = TEXT("SpriteTexture");
        Texture->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
        Texture->SamplerType = SAMPLERTYPE_Color;
        Texture->Coordinates.Connect(0, FinalUV);
        AddExpression(Texture);
        UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Material);
        Tint->ParameterName = TEXT("SpriteTint");
        Tint->DefaultValue = FLinearColor::White;
        AddExpression(Tint);
        UMaterialExpressionMultiply* Color = NewObject<UMaterialExpressionMultiply>(Material);
        Color->A.Connect(0, Texture); Color->B.Connect(0, Tint); AddExpression(Color);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
        if (bTranslucent)
        {
            UMaterialExpressionMultiply* Alpha = NewObject<UMaterialExpressionMultiply>(Material);
            Alpha->A.Connect(4, Texture); Alpha->B.Connect(4, Tint); AddExpression(Alpha);
            Material->GetEditorOnlyData()->Opacity.Connect(0, Alpha);
        }
        else Material->GetEditorOnlyData()->OpacityMask.Connect(4, Texture);
        Material->PostEditChange();
        FAssetRegistryModule::AssetCreated(Material);
        Material->MarkPackageDirty();
        SaveAsset(Material, Report);
    }
}

void FBYMEditorSetup::CreateMaps(FBYMSetupReport& Report)
{
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    const TArray<TPair<FString, UClass*>> MapDefinitions = {
        { Settings->MainMenuMap, ABYMMenuGameMode::StaticClass() },
        { Settings->WorldMap, ABYMGameMode::StaticClass() }
    };
    for (const TPair<FString, UClass*>& Definition : MapDefinitions)
    {
        const FString& MapName = Definition.Key;
        if (!ValidMapName(MapName))
        {
            Report.Errors.Add(TEXT("Map settings must name a package under /BackyardMMO/Maps/: ") + MapName);
            continue;
        }
        if (PackageExists(MapName))
        {
            Report.Notes.Add(TEXT("Preserved existing map: ") + MapName);
            continue;
        }
        UPackage* Package = CreatePackage(*MapName);
        Package->SetPackageFlags(PKG_ContainsMap);
        UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false,
            FName(*FPackageName::GetLongPackageAssetName(MapName)), Package, true);
        if (!World) { Report.Errors.Add(TEXT("Cannot create world: ") + MapName); continue; }
        World->SetFlags(RF_Public | RF_Standalone);
        World->GetWorldSettings()->DefaultGameMode = Definition.Value;
        World->GetWorldSettings()->bEnableWorldBoundsChecks = false;
        // Fresh maps open above the board instead of looking horizontally into empty space.
        // Index zero is the top orthographic view; the normal perspective viewport uses index three.
        const float Extent = FMath::Clamp(Settings->YardCells, 40, 100)
            * FMath::Clamp(Settings->CellSize, 20.f, 1000.f);
        const float ViewWidth = FMath::Max(FMath::Clamp(Settings->CameraWidth, 1000.f, 20000.f), Extent * 1.25f);
        if (World->EditorViews.Num() < LVT_MAX) World->EditorViews.SetNum(LVT_MAX);
        for (const ELevelViewportType ViewType : { LVT_Perspective, LVT_OrthoTop })
        {
            FLevelViewportInfo& View = World->EditorViews[ViewType];
            View.CamPosition = FVector(Extent * 0.5f, Extent * 0.5f, ViewWidth);
            View.CamRotation = FRotator(-90.f, -90.f, 0.f);
            View.CamOrthoZoom = ViewWidth;
            View.CamUpdated = true;
        }
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags = RF_Transactional;
        ABYMBoard* Board = World->SpawnActor<ABYMBoard>(ABYMBoard::StaticClass(), FTransform::Identity, Spawn);
        if (Board)
        {
            Board->SetActorLabel(TEXT("Backyard original artwork board"));
            EnsurePrimitiveDepth(Board);
        }
        APlayerStart* Start = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(FVector(0, 0, 3000)), Spawn);
        if (Start) Start->SetActorLabel(TEXT("Top-down camera start"));
        FAssetRegistryModule::AssetCreated(World);
        World->MarkPackageDirty();
        if (SaveAsset(World, Report, true)) ++Report.CreatedMaps;
        // Keep the saved asset valid for the asset registry and the validation pass.
        // It has no engine world context and is never ticked; GC owns its eventual cleanup.
        World->RemoveFromRoot();
    }
}

void FBYMEditorSetup::ConfigureProject(FBYMSetupReport& Report)
{
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    UGameMapsSettings* Maps = GetMutableDefault<UGameMapsSettings>();
    const FString MenuObjectPath = Settings->MainMenuMap + TEXT(".") + FPackageName::GetLongPackageAssetName(Settings->MainMenuMap);
    Maps->EditorStartupMap = FSoftObjectPath(MenuObjectPath);
    Maps->GameInstanceClass = FSoftClassPath(UBYMGameInstance::StaticClass());
    UGameMapsSettings::SetGameDefaultMap(MenuObjectPath);
    UGameMapsSettings::SetGlobalDefaultGameMode(ABYMMenuGameMode::StaticClass()->GetPathName());
    if (!Maps->TryUpdateDefaultConfigFile()) Report.Errors.Add(TEXT("Could not write project map/game-instance settings to DefaultEngine.ini."));

    UProjectPackagingSettings* Packaging = GetMutableDefault<UProjectPackagingSettings>();
    const auto AddMap = [Packaging](const FString& Path)
    {
        if (!Packaging->MapsToCook.ContainsByPredicate([&Path](const FFilePath& Value) { return Value.FilePath == Path; }))
        {
            FFilePath Entry; Entry.FilePath = Path; Packaging->MapsToCook.Add(Entry);
        }
    };
    AddMap(Settings->MainMenuMap); AddMap(Settings->WorldMap);
    if (!Packaging->DirectoriesToAlwaysCook.ContainsByPredicate([](const FDirectoryPath& Path) { return Path.Path == TEXT("/BackyardMMO"); }))
    {
        FDirectoryPath Entry; Entry.Path = TEXT("/BackyardMMO"); Packaging->DirectoriesToAlwaysCook.Add(Entry);
    }
    if (!Packaging->TryUpdateDefaultConfigFile()) Report.Errors.Add(TEXT("Could not write project cooking settings to DefaultGame.ini."));

    const FString EngineConfig = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
    GConfig->SetInt(TEXT("/Script/Engine.RendererSettings"), TEXT("r.CustomDepth"), 3, EngineConfig);
    GConfig->Flush(false, EngineConfig);
    GConfig->SetInt(TEXT("/Script/Engine.RendererSettings"), TEXT("r.CustomDepth"), 3, GEngineIni);
    if (IConsoleVariable* CustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
        CustomDepth->Set(3, ECVF_SetByProjectSetting);
    Report.Notes.Add(TEXT("Configured MainMenu, native game instance, both map cook entries, plugin content cooking and CustomDepth with stencil."));
}

void FBYMEditorSetup::ValidateInternal(FBYMSetupReport& Report)
{
    const FString Root = PluginRoot();
    if (Root.IsEmpty()) { Report.Errors.Add(TEXT("BackyardMMO plugin is not enabled.")); return; }
    const UBYMSettings* Settings = GetDefault<UBYMSettings>();
    const TArray<TPair<FString, UClass*>> MapDefinitions = {
        { Settings->MainMenuMap, ABYMMenuGameMode::StaticClass() },
        { Settings->WorldMap, ABYMGameMode::StaticClass() }
    };
    for (const TPair<FString, UClass*>& Definition : MapDefinitions)
    {
        if (!PackageExists(Definition.Key))
        {
            Report.Errors.Add(TEXT("Missing generated map: ") + Definition.Key);
            continue;
        }
        const FString WorldPath = Definition.Key + TEXT(".") + FPackageName::GetLongPackageAssetName(Definition.Key);
        UWorld* World = LoadObject<UWorld>(nullptr, *WorldPath);
        if (!World || !World->GetWorldSettings() || World->GetWorldSettings()->DefaultGameMode != Definition.Value)
            Report.Errors.Add(TEXT("The map game mode does not match the native Backyard Monsters NXT mode: ") + Definition.Key);
    }
    for (const TCHAR* PackagePath : { SpriteMaterial, AlphaSpriteMaterial })
    {
        const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
        const FString ObjectPath = FString(PackagePath) + TEXT(".") + Name;
        const bool bTranslucent = FString(PackagePath) == AlphaSpriteMaterial;
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
        if (!Material)
        {
            Report.Errors.Add(TEXT("Missing generated material: ") + Name);
            continue;
        }
        const EBlendMode ExpectedBlend = bTranslucent ? BLEND_Translucent : BLEND_Masked;
        if (Material->BlendMode != ExpectedBlend || !Material->TwoSided || !Material->GetShadingModels().HasShadingModel(MSM_Unlit))
            Report.Errors.Add(Name + (bTranslucent
                ? TEXT(" must use translucent, two-sided, unlit rendering.")
                : TEXT(" must use masked, two-sided, unlit rendering.")));
        if (bTranslucent && !Material->AllowTranslucentCustomDepthWrites)
            Report.Errors.Add(Name + TEXT(" must allow translucent custom-depth writes."));
        if (!IsRunningCommandlet() && FApp::CanEverRender())
        {
            // Check the actual shader result for each graph, including the alpha
            // variant, rather than accepting a package that renders as fallback.
            if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform, EMaterialQualityLevel::High))
            {
                Resource->FinishCompilation();
                for (const FString& Error : Resource->GetCompileErrors())
                    Report.Errors.Add(Name + TEXT(" shader compilation: ") + Error);
            }
            else Report.Notes.Add(Name + TEXT(" has no active High-quality editor shader resource; shader compilation could not be checked."));
        }
    }

    const TSharedPtr<FJsonObject> Catalog = ReadJson(Root / TEXT("Content/Data/catalog.json"));
    if (!Catalog)
    {
        Report.Errors.Add(TEXT("The original-data catalog Content/Data/catalog.json is missing or invalid."));
        return;
    }
    for (const FString& Category : { FString(TEXT("buildings")), FString(TEXT("monsters")) })
    {
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!Catalog->TryGetArrayField(Category, Entries) || !Entries || Entries->IsEmpty())
            Report.Errors.Add(TEXT("Catalog category is empty: ") + Category);
    }
    // Validate every runtime artwork/audio reference, including level composites,
    // damaged/destroyed layers, terrain, monster atlases, and converted music.
    // Raw imageLayers contain local filenames resolved by the converter, so only
    // complete SourceAssets-relative paths are considered here.
    TSet<FString> ReferencedAssets;
    TFunction<void(const TSharedPtr<FJsonValue>&)> Visit;
    Visit = [&Visit, &ReferencedAssets](const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid()) return;
        if (Value->Type == EJson::Object)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Value->AsObject()->Values) Visit(Field.Value);
        }
        else if (Value->Type == EJson::Array)
        {
            for (const TSharedPtr<FJsonValue>& Child : Value->AsArray()) Visit(Child);
        }
        else if (Value->Type == EJson::String)
        {
            const FString Path = Value->AsString();
            const FString Extension = FPaths::GetExtension(Path).ToLower();
            if (Path.Contains(TEXT("/")) && !Path.Contains(TEXT(":")) && (Extension == TEXT("png") || Extension == TEXT("jpg") || Extension == TEXT("jpeg") || Extension == TEXT("wav")))
                ReferencedAssets.Add(Path);
        }
    };
    Visit(MakeShared<FJsonValueObject>(Catalog));
    for (const FString& Asset : ReferencedAssets)
    {
        const FString ObjectPath = ABYMBoard::AssetObjectPath(Asset);
        if (!PackageExists(FPackageName::ObjectPathToPackageName(ObjectPath)))
            Report.Errors.Add(TEXT("Catalog artwork or audio has not been imported: ") + Asset);
    }
    const int32 References = ReferencedAssets.Num();
    const UGameMapsSettings* Maps = GetDefault<UGameMapsSettings>();
    if (FPackageName::ObjectPathToPackageName(UGameMapsSettings::GetGameDefaultMap()) != Settings->MainMenuMap)
        Report.Errors.Add(TEXT("Game Default Map is not the configured Backyard Monsters NXT MainMenu."));
    if (Maps->GameInstanceClass != FSoftClassPath(UBYMGameInstance::StaticClass()))
        Report.Errors.Add(TEXT("Project Game Instance is not BYMGameInstance."));
    const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
    if (!Packaging->DirectoriesToAlwaysCook.ContainsByPredicate([](const FDirectoryPath& Path) { return Path.Path == TEXT("/BackyardMMO"); }))
        Report.Errors.Add(TEXT("The /BackyardMMO content directory is missing from Always Cook."));
    for (const TPair<FString, UClass*>& Definition : MapDefinitions)
    {
        if (!Packaging->MapsToCook.ContainsByPredicate([&Definition](const FFilePath& Entry) { return Entry.FilePath == Definition.Key; }))
            Report.Errors.Add(TEXT("A required map is missing from Maps to Cook: ") + Definition.Key);
    }
    int32 CustomDepth = 0;
    GConfig->GetInt(TEXT("/Script/Engine.RendererSettings"), TEXT("r.CustomDepth"), CustomDepth,
        FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini"));
    if (CustomDepth != 3) Report.Errors.Add(TEXT("CustomDepth with stencil is not enabled in project renderer settings."));
    Report.Notes.Add(FString::Printf(TEXT("Validated %d distinct catalog artwork/audio references. This validates setup assets and settings, not gameplay/network performance."), References));
}

void FBYMEditorSetup::WriteReport(const FBYMSetupReport& Report)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("createdUtc"), FDateTime::UtcNow().ToIso8601());
    Json->SetBoolField(TEXT("ok"), Report.Succeeded());
    Json->SetNumberField(TEXT("imported"), Report.Imported);
    Json->SetNumberField(TEXT("unchanged"), Report.Unchanged);
    Json->SetNumberField(TEXT("textureDefaultsVerified"), Report.TextureDefaultsVerified);
    Json->SetNumberField(TEXT("textureDefaultsCorrected"), Report.TextureDefaultsCorrected);
    Json->SetNumberField(TEXT("mapsCreated"), Report.CreatedMaps);
    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FString& Error : Report.Errors) Errors.Add(MakeShared<FJsonValueString>(Error));
    Json->SetArrayField(TEXT("errors"), Errors);
    TArray<TSharedPtr<FJsonValue>> Notes;
    for (const FString& Note : Report.Notes) Notes.Add(MakeShared<FJsonValueString>(Note));
    Json->SetArrayField(TEXT("notes"), Notes);
    WriteJson(FPaths::ProjectSavedDir() / TEXT("BackyardMMO/SetupReport.json"), Json);
}

void FBYMEditorSetup::Present(const FBYMSetupReport& Report)
{
    UE_LOG(LogBYMSetup, Display, TEXT("%s"), *Report.Summary());
    for (const FString& Note : Report.Notes) UE_LOG(LogBYMSetup, Display, TEXT("%s"), *Note);
    for (const FString& Error : Report.Errors) UE_LOG(LogBYMSetup, Error, TEXT("%s"), *Error);
    if (IsRunningCommandlet()) return;
    FMessageLog Log(TEXT("BackyardMMO"));
    Log.NewPage(FText::FromString(Report.Summary()));
    for (const FString& Note : Report.Notes) Log.Info(FText::FromString(Note));
    for (const FString& Error : Report.Errors) Log.Error(FText::FromString(Error));
    FNotificationInfo Notification(FText::FromString(Report.Summary()));
    Notification.ExpireDuration = Report.Succeeded() ? 6.0f : 12.0f;
    Notification.bUseSuccessFailIcons = true;
    if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Notification))
        Item->SetCompletionState(Report.Succeeded() ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    if (!Report.Succeeded()) Log.Open(EMessageSeverity::Error);
}

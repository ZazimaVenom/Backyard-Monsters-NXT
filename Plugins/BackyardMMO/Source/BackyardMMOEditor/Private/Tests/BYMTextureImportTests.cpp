#if WITH_DEV_AUTOMATION_TESTS

#include "BYMTextureImportSettings.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMColorTextureDefaultsTest, "BackyardMMO.Assets.ColorTextureDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBYMColorTextureDefaultsTest::RunTest(const FString& Parameters)
{
    // The RGB values resemble normal-map colors, but these are ordinary sprite
    // pixels with transparent and partially transparent edges. The final import
    // pass must correct their settings without changing any original source byte.
    const uint8 OriginalPixels[] = {
        255, 128, 128,   0, 255, 127, 128,  32, 250, 128, 127,  64, 255, 128, 128, 255,
        255, 128, 128,  16, 253, 127, 129,  80, 252, 129, 128, 128, 255, 128, 128, 255,
        255, 128, 128,   0, 251, 128, 128, 100, 254, 128, 129, 160, 255, 128, 128, 255,
        255, 128, 128,   0, 255, 128, 128,   0, 255, 128, 128, 200, 255, 128, 128, 255
    };
    TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient));
    if (!TestNotNull(TEXT("Create isolated source texture"), Texture.Get())) return false;
    Texture->Source.Init(4, 4, 1, 1, TSF_BGRA8, OriginalPixels);
    Texture->CompressionSettings = TC_Normalmap;
    Texture->SRGB = false;
    Texture->CompressionNone = false;
    Texture->CompressionNoAlpha = true;
    Texture->bFlipGreenChannel = true;
    Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
    Texture->MipGenSettings = TMGS_FromTextureGroup;
    Texture->AddressX = TA_Wrap;
    Texture->AddressY = TA_Wrap;
    Texture->Filter = TF_Nearest;
    Texture->NeverStream = false;

    const FString SpritePath(TEXT("source_client/shapes/567.png"));
    TestFalse(TEXT("Normal-map settings are not accepted as color defaults"),
        BYMTextureImportSettings::MatchesColorDefaults(*Texture, SpritePath));
    TestTrue(TEXT("Finalization corrects the misclassified texture"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, SpritePath));
    TestTrue(TEXT("All required defaults are present after texture compilation"),
        BYMTextureImportSettings::MatchesColorDefaults(*Texture, SpritePath));
    TestTrue(TEXT("Compression uses the actual Default preset"), Texture->CompressionSettings == TC_Default);
    TestTrue(TEXT("Original colors are interpreted as sRGB"), Texture->SRGB);
    TestTrue(TEXT("Color alpha is retained without green-channel inversion"),
        Texture->CompressionNone && !Texture->CompressionNoAlpha && !Texture->bFlipGreenChannel);
    TestTrue(TEXT("Sprite edges clamp instead of sampling another atlas edge"),
        Texture->AddressX == TA_Clamp && Texture->AddressY == TA_Clamp);
    TestTrue(TEXT("Original source dimensions and format are retained"),
        Texture->Source.GetSizeX() == 4 && Texture->Source.GetSizeY() == 4 && Texture->Source.GetFormat() == TSF_BGRA8);
    const uint8* SourcePixels = Texture->Source.LockMipReadOnly(0);
    const bool bSourceBytesUnchanged = SourcePixels
        && FMemory::Memcmp(SourcePixels, OriginalPixels, sizeof(OriginalPixels)) == 0;
    Texture->Source.UnlockMip(0);
    TestTrue(TEXT("Every original RGB and alpha byte survives normalization"), bSourceBytesUnchanged);

    TestFalse(TEXT("Repeating the final pass does not rebuild an already-correct texture"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, SpritePath));

    // Simulate a pre-existing/cached asset or a later import callback restoring
    // normal-map classification. The source pixels have not changed, so a digest
    // cache alone cannot detect this failure; the settings check must still do so.
    Texture->CompressionSettings = TC_Normalmap;
    Texture->SRGB = false;
    Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
    TestTrue(TEXT("Unchanged source pixels do not prevent correcting old settings"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, SpritePath));
    TestTrue(TEXT("Previously cached normal-map settings are corrected"),
        BYMTextureImportSettings::MatchesColorDefaults(*Texture, SpritePath));

    const FString TerrainPath(TEXT("derived/terrain/grass_blended_tile.png"));
    TestTrue(TEXT("The seamless terrain composite receives its wrap exception"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, TerrainPath));
    TestTrue(TEXT("Terrain wraps on both axes and still uses color defaults"),
        Texture->AddressX == TA_Wrap && Texture->AddressY == TA_Wrap
        && BYMTextureImportSettings::MatchesColorDefaults(*Texture, TerrainPath));
    TestFalse(TEXT("Terrain's completed final pass is also idempotent"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, TerrainPath));
    TestTrue(TEXT("A normal sprite path restores clamped edges"),
        BYMTextureImportSettings::ApplyColorDefaults(*Texture, SpritePath));
    TestTrue(TEXT("Sprite defaults remain correct after the terrain check"),
        BYMTextureImportSettings::MatchesColorDefaults(*Texture, SpritePath));
    return true;
}

#endif

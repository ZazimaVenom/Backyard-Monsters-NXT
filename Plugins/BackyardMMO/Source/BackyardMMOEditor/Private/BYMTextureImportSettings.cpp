#include "BYMTextureImportSettings.h"

#include "Engine/Texture2D.h"
#include "TextureCompiler.h"

namespace BYMTextureImportSettings
{
    namespace
    {
        TextureAddress ArtworkAddress(const FString& SourceRelativePath)
        {
            return SourceRelativePath == TEXT("derived/terrain/grass_blended_tile.png") ? TA_Wrap : TA_Clamp;
        }
    }

    bool MatchesColorDefaults(const UTexture2D& Texture, const FString& SourceRelativePath)
    {
        const TextureAddress Address = ArtworkAddress(SourceRelativePath);
        return Texture.CompressionSettings == TC_Default && Texture.SRGB
            && Texture.CompressionNone && !Texture.CompressionNoAlpha && !Texture.bFlipGreenChannel
            && Texture.LODGroup == TEXTUREGROUP_Pixels2D && Texture.MipGenSettings == TMGS_NoMipmaps
            && Texture.Filter == TF_Bilinear && Texture.NeverStream
            && Texture.AddressX == Address && Texture.AddressY == Address;
    }

    bool ApplyColorDefaults(UTexture2D& Texture, const FString& SourceRelativePath)
    {
        UTexture* Textures[] = { &Texture };
        FTextureCompilingManager::Get().FinishCompilation(Textures);
        if (MatchesColorDefaults(Texture, SourceRelativePath)) return false;

        Texture.Modify();
        Texture.PreEditChange(nullptr);
        // Default color compression is deliberately explicit. None of the original
        // PNG/JPG files are normals, regardless of their name or dominant colors.
        Texture.CompressionSettings = TC_Default;
        Texture.SRGB = true;
        Texture.CompressionNone = true;
        Texture.CompressionNoAlpha = false;
        Texture.bFlipGreenChannel = false;
        Texture.LODGroup = TEXTUREGROUP_Pixels2D;
        Texture.MipGenSettings = TMGS_NoMipmaps;
        Texture.Filter = TF_Bilinear;
        Texture.NeverStream = true;
        // Preserve the terrain's continuous repeat and the atlases' clamped edges.
        Texture.AddressX = ArtworkAddress(SourceRelativePath);
        Texture.AddressY = ArtworkAddress(SourceRelativePath);
        Texture.PostEditChange();
        FTextureCompilingManager::Get().FinishCompilation(Textures);
        return true;
    }
}

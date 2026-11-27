#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** The bundled Flash images are color artwork, including blue/purple monster sheets. */
namespace BYMTextureImportSettings
{
    bool MatchesColorDefaults(const UTexture2D& Texture, const FString& SourceRelativePath);

    /** Waits for texture compilation, applies color defaults and completes the rebuild.
     * Returns true when settings changed. Does not save the package or modify source pixels.
     */
    bool ApplyColorDefaults(UTexture2D& Texture, const FString& SourceRelativePath);
}

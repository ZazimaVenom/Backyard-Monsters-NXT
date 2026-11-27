#pragma once

#include "CoreMinimal.h"

/** UV geometry comes from authored source dimensions, not a streamed texture's
 * temporarily resident mip size. No texture objects or mutable shared state.
 */
namespace BYMSpriteAtlas
{
    struct FLayout
    {
        int32 Columns = 1;
        int32 Rows = 1;
        int32 Frames = 1;
        FVector2D Stride = FVector2D(1, 1);
        FVector2D Scale = FVector2D(1, 1);
        FVector2D Inset = FVector2D(0, 0);
    };

    /** Zero requested counts derive from the complete cells available in the
     * authored sheet. Negative/oversized counts and invalid geometry fail,
     * leaving Out unchanged so a later texture-readiness retry is safe.
     * Cells must be at least one source pixel; fractional dimensions above
     * that minimum are retained without integer rounding.
     */
    BACKYARDMMO_API bool MakeLayout(double FrameWidth, double FrameHeight,
        double TextureWidth, double TextureHeight, int32 RequestedColumns,
        int32 RequestedRows, int32 RequestedFrames, FLayout& Out);

    /** Clamps a requested cell to this layout's grid. Combined with Scale,
     * samples run from the first to last texel centre inside the chosen cell.
     */
    BACKYARDMMO_API FVector2D FrameOffset(const FLayout& Layout, int32 Column, int32 Row);
}

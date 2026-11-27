#include "BYMSpriteAtlas.h"

#include <cmath>
#include <limits>

namespace BYMSpriteAtlas
{
    namespace
    {
        bool ResolveAxis(double FrameSize, double SheetSize, int32 Requested, int32& Out)
        {
            if (!FMath::IsFinite(FrameSize) || !FMath::IsFinite(SheetSize)
                || FrameSize < 1. || SheetSize <= 0 || Requested < 0) return false;
            const double Ratio = SheetSize / FrameSize;
            if (!FMath::IsFinite(Ratio) || Ratio < 1.) return false;
            // Permit only floating-point roundoff at an exact cell boundary,
            // never a partial extra frame or a power-of-two padding assumption.
            constexpr double Roundoff = 16. * std::numeric_limits<double>::epsilon();
            const double Complete = std::floor(Ratio + Roundoff * FMath::Max(1., Ratio));
            if (Requested == 0)
            {
                if (Complete < 1. || Complete > static_cast<double>(std::numeric_limits<int32>::max())) return false;
                Out = static_cast<int32>(Complete);
            }
            else Out = Requested;
            const double Used = FrameSize * Out;
            return FMath::IsFinite(Used) && Used <= SheetSize + Roundoff * FMath::Max(SheetSize, Used);
        }
    }

    bool MakeLayout(double FrameWidth, double FrameHeight, double TextureWidth, double TextureHeight,
        int32 RequestedColumns, int32 RequestedRows, int32 RequestedFrames, FLayout& Out)
    {
        if (RequestedFrames < 0) return false;
        FLayout Candidate;
        if (!ResolveAxis(FrameWidth, TextureWidth, RequestedColumns, Candidate.Columns)
            || !ResolveAxis(FrameHeight, TextureHeight, RequestedRows, Candidate.Rows)) return false;
        // The renderer indexes frames with int32. Guard multiplication before
        // evaluating it, including unreasonable or corrupted metadata counts.
        if (Candidate.Columns > std::numeric_limits<int32>::max() / Candidate.Rows) return false;
        const int32 GridFrames = Candidate.Columns * Candidate.Rows;
        if (RequestedFrames > GridFrames) return false;
        Candidate.Frames = RequestedFrames == 0 ? GridFrames : RequestedFrames;
        Candidate.Stride = FVector2D(FrameWidth / TextureWidth, FrameHeight / TextureHeight);
        Candidate.Inset = FVector2D(.5 / TextureWidth, .5 / TextureHeight);
        Candidate.Scale = FVector2D(FMath::Max(FrameWidth - 1., 0.) / TextureWidth,
            FMath::Max(FrameHeight - 1., 0.) / TextureHeight);
        if (!FMath::IsFinite(Candidate.Stride.X) || !FMath::IsFinite(Candidate.Stride.Y)
            || !FMath::IsFinite(Candidate.Inset.X) || !FMath::IsFinite(Candidate.Inset.Y)
            || !FMath::IsFinite(Candidate.Scale.X) || !FMath::IsFinite(Candidate.Scale.Y)) return false;
        Out = Candidate;
        return true;
    }

    FVector2D FrameOffset(const FLayout& Layout, int32 Column, int32 Row)
    {
        const int32 SafeColumn = FMath::Clamp(Column, 0, FMath::Max(1, Layout.Columns) - 1);
        const int32 SafeRow = FMath::Clamp(Row, 0, FMath::Max(1, Layout.Rows) - 1);
        return FVector2D(SafeColumn * Layout.Stride.X + Layout.Inset.X,
            SafeRow * Layout.Stride.Y + Layout.Inset.Y);
    }
}

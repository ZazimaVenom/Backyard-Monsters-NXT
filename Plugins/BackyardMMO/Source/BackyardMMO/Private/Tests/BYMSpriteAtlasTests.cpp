#include "BYMSpriteAtlas.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>

namespace BYMSpriteAtlasTests
{
    using namespace BYMSpriteAtlas;

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSpriteAtlasLayoutTest, "BackyardMMO.Presentation.SpriteAtlasLayout",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMSpriteAtlasLayoutTest::RunTest(const FString& Parameters)
    {
        FLayout Pokey;
        if (!TestTrue(TEXT("Original C1 authored sheet creates its complete facing grid"),
            MakeLayout(24., 21., 720., 21., 30, 1, 30, Pokey))) return false;
        TestEqual(TEXT("C1 has thirty facing columns, not a streamed placeholder's columns"), Pokey.Columns, 30);
        TestEqual(TEXT("C1 contains a single animation row"), Pokey.Rows, 1);
        TestEqual(TEXT("C1 retains all thirty source poses"), Pokey.Frames, 30);
        TestTrue(TEXT("The first C1 pixel is sampled at its centre"), FrameOffset(Pokey, 0, 0).Equals(FVector2D(.5 / 720., .5 / 21.), 1.e-12));
        TestTrue(TEXT("C1's last facing begins inside its own 24-pixel cell"), FMath::IsNearlyEqual(FrameOffset(Pokey, 29, 0).X, 696.5 / 720., 1.e-12));
        TestTrue(TEXT("C1's last facing cannot sample its neighbour or wrap the sheet"), FMath::IsNearlyEqual(FrameOffset(Pokey, 29, 0).X + Pokey.Scale.X, 719.5 / 720., 1.e-12));

        // zagnoid.png is 1933 pixels wide; the thirty authored IC2 cells use
        // 1932 pixels. UVs must retain its 64.4-pixel stride and trailing padding.
        FLayout Zagnoid;
        if (!TestTrue(TEXT("Fractional IC2 cells fit the padded original PNG"),
            MakeLayout(64.4, 46., 1933., 46., 30, 1, 30, Zagnoid))) return false;
        TestTrue(TEXT("IC2 retains its fractional cell stride"), FMath::IsNearlyEqual(Zagnoid.Stride.X, 64.4 / 1933., 1.e-12));
        const FVector2D Last = FrameOffset(Zagnoid, 29, 0);
        TestTrue(TEXT("IC2 last facing starts at the authored fractional offset"), FMath::IsNearlyEqual(Last.X, (29. * 64.4 + .5) / 1933., 1.e-12));
        TestTrue(TEXT("IC2 last sample stays before the padding"), FMath::IsNearlyEqual(Last.X + Zagnoid.Scale.X, 1931.5 / 1933., 1.e-12));
        TestTrue(TEXT("Building a second atlas does not alter the first atlas"), FMath::IsNearlyEqual(Pokey.Stride.X, 24. / 720., 1.e-12));
        TestTrue(TEXT("Separate species have independent atlas geometry"), !Pokey.Stride.Equals(Zagnoid.Stride, 1.e-8));

        FLayout Derived;
        TestTrue(TEXT("Zero counts derive all complete padded cells"), MakeLayout(64.4, 46., 1933., 93., 0, 0, 0, Derived));
        TestEqual(TEXT("Derived padded columns"), Derived.Columns, 30);
        TestEqual(TEXT("Derived padded rows"), Derived.Rows, 2);
        TestEqual(TEXT("Derived complete grid frame count"), Derived.Frames, 60);
        TestTrue(TEXT("Rows use their actual pixel stride"), FMath::IsNearlyEqual(FrameOffset(Derived, 29, 1).Y + Derived.Scale.Y, 91.5 / 93., 1.e-12));
        TestTrue(TEXT("Negative frame coordinates clamp to the first cell"), FrameOffset(Derived, -1, -7).Equals(Derived.Inset, 1.e-12));
        TestTrue(TEXT("Oversized frame coordinates clamp to the final cell"), FrameOffset(Derived, 1000, 1000).Equals(FrameOffset(Derived, 29, 1), 1.e-12));

        FLayout PartialLastRow;
        TestTrue(TEXT("An animation may use fewer frames than the sheet's cell capacity"), MakeLayout(16., 16., 128., 64., 8, 4, 29, PartialLastRow));
        TestEqual(TEXT("Unused final cells are not invented animation frames"), PartialLastRow.Frames, 29);
        TestEqual(TEXT("A partially used last row retains its actual sheet row count"), PartialLastRow.Rows, 4);

        FLayout Pixel;
        TestTrue(TEXT("The original one-pixel beam texture is a valid atlas"), MakeLayout(1., 1., 1., 1., 1, 1, 1, Pixel));
        TestTrue(TEXT("One-pixel UVs sample exactly the single texel centre"), FrameOffset(Pixel, 0, 0).Equals(FVector2D(.5, .5), 0.));
        TestTrue(TEXT("One-pixel frames have zero UV extent without division by zero"), Pixel.Scale.Equals(FVector2D::ZeroVector, 0.));
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSpriteAtlasValidationTest, "BackyardMMO.Presentation.SpriteAtlasValidation",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    bool FBYMSpriteAtlasValidationTest::RunTest(const FString& Parameters)
    {
        FLayout Stable;
        MakeLayout(24., 21., 720., 21., 30, 1, 30, Stable);
        const FLayout Before = Stable;
        auto Unchanged = [&]()
        {
            return Stable.Columns == Before.Columns && Stable.Rows == Before.Rows && Stable.Frames == Before.Frames
                && Stable.Stride.Equals(Before.Stride, 0.) && Stable.Scale.Equals(Before.Scale, 0.) && Stable.Inset.Equals(Before.Inset, 0.);
        };
        const double NaN = std::numeric_limits<double>::quiet_NaN();
        const double Infinity = std::numeric_limits<double>::infinity();
        const double Invalid[] = { 0., -1., NaN, Infinity };
        for (double Value : Invalid)
        {
            TestFalse(TEXT("Invalid frame width is rejected"), MakeLayout(Value, 21., 720., 21., 30, 1, 30, Stable));
            TestFalse(TEXT("Invalid frame height is rejected"), MakeLayout(24., Value, 720., 21., 30, 1, 30, Stable));
            TestFalse(TEXT("Invalid sheet width is rejected"), MakeLayout(24., 21., Value, 21., 30, 1, 30, Stable));
            TestFalse(TEXT("Invalid sheet height is rejected"), MakeLayout(24., 21., 720., Value, 30, 1, 30, Stable));
            TestTrue(TEXT("A failed atlas leaves all prior geometry untouched"), Unchanged());
        }
        TestFalse(TEXT("Subpixel frame width cannot contain the texel-centre inset"), MakeLayout(.5, 21., 720., 21., 30, 1, 30, Stable));
        TestFalse(TEXT("Subpixel frame height cannot contain the texel-centre inset"), MakeLayout(24., .5, 720., 21., 30, 1, 30, Stable));
        TestFalse(TEXT("A temporary texture smaller than one frame cannot become cached atlas geometry"), MakeLayout(24., 21., 1., 1., 30, 1, 30, Stable));
        TestFalse(TEXT("Oversized requested columns do not cross source frames"), MakeLayout(24., 21., 720., 21., 31, 1, 30, Stable));
        TestFalse(TEXT("Oversized requested rows do not cross the sheet edge"), MakeLayout(24., 21., 720., 21., 30, 2, 30, Stable));
        TestFalse(TEXT("Requested frames cannot exceed the complete grid"), MakeLayout(24., 21., 720., 21., 30, 1, 31, Stable));
        TestFalse(TEXT("Negative column metadata is invalid"), MakeLayout(24., 21., 720., 21., -1, 1, 30, Stable));
        TestFalse(TEXT("Negative row metadata is invalid"), MakeLayout(24., 21., 720., 21., 30, -1, 30, Stable));
        TestFalse(TEXT("Negative frame metadata is invalid"), MakeLayout(24., 21., 720., 21., 30, 1, -1, Stable));
        TestFalse(TEXT("An overflowing derived axis cannot become an integer count"), MakeLayout(1., 1., 1.e30, 1., 0, 1, 1, Stable));
        TestFalse(TEXT("An overflowing frame product is rejected before multiplication"), MakeLayout(1., 1., 65536., 65536., 65536, 65536, 1, Stable));
        TestTrue(TEXT("Every validation failure preserves the previous successful atlas"), Unchanged());
        TestTrue(TEXT("The same output can retry successfully once authored dimensions are available"), MakeLayout(39., 28., 1170., 28., 30, 1, 30, Stable));
        TestTrue(TEXT("A successful retry replaces both frame axes"), Stable.Stride.Equals(FVector2D(39. / 1170., 1.), 1.e-12));
        TestTrue(TEXT("A successful retry updates the actual texel inset"), Stable.Inset.Equals(FVector2D(.5 / 1170., .5 / 28.), 1.e-12));
        return true;
    }
}

#endif

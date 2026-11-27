#include "BYMUILayoutTestFixture.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"
#include "Types/PaintArgs.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

// Requires the real Slate renderer/font service in an Unreal Editor automation
// session. These tests intentionally are not part of the non-Unreal adapters.
// Manual Paint updates layout caches and draw elements; it is not a GPU image
// comparison or a substitute for the final interactive visual review.
namespace BYMUILayoutTests
{
    using FGeometryMap = TMap<const SWidget*, FGeometry>;

    bool Ready(FAutomationTestBase& Test, const BYMUIAutomation::FFixture& Fixture)
    {
        return Test.TestTrue(TEXT("An initialized Unreal Slate application is required"), FSlateApplication::IsInitialized())
            && Test.TestTrue(TEXT("The fixture retains its actual production widget owner"), Fixture.OwnerLifetime.IsValid())
            && Test.TestTrue(TEXT("The fixture supplies an actual production root"), Fixture.Root.IsValid());
    }

    void Arrange(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, double Time, FGeometryMap& Result, int32 Depth = 0)
    {
        if (Depth > 128) return;
        Widget->Tick(Geometry, Time, 1.f / 60.f);
        Result.Add(&Widget.Get(), Geometry);
        FArrangedChildren Children(EVisibility::Visible);
        Widget->ArrangeChildren(Geometry, Children, true);
        for (int32 Index = 0; Index < Children.Num(); ++Index)
            Arrange(Children[Index].Widget, Children[Index].Geometry, Time, Result, Depth + 1);
    }

    FGeometryMap Layout(const BYMUIAutomation::FFixture& Fixture, const FVector2D& ViewportPixels, float DPI, double& Time)
    {
        const FVector2D LocalSize = ViewportPixels / DPI;
        const FGeometry Geometry = FGeometry::MakeRoot(LocalSize, FSlateLayoutTransform(DPI));
        TSharedPtr<SWindow> Window = SNew(SWindow).ClientSize(ViewportPixels);
        Window->SetContent(Fixture.Root.ToSharedRef());
        FHittestGrid HitTestGrid;
        HitTestGrid.SetOwner(Window.Get());
        HitTestGrid.SetHittestArea(FVector2D::ZeroVector, ViewportPixels, FVector2D::ZeroVector);
        FGeometryMap Result;
        // Multiple paints are essential: auto-wrap regressions can appear only
        // after the previous frame's allotted width enters the text cache.
        for (int32 Frame = 0; Frame < 4; ++Frame)
        {
            Time += 1. / 60.;
            Fixture.OwnerLifetime->Tick(Geometry, Time, 1.f / 60.f);
            Fixture.Root->Invalidate(EInvalidateWidgetReason::Layout);
            Fixture.Root->SlatePrepass(DPI);
            Result.Reset(); Arrange(Fixture.Root.ToSharedRef(), Geometry, Time, Result);
            HitTestGrid.Clear();
            FSlateWindowElementList DrawElements(Window);
            const FPaintArgs PaintArgs(Window.Get(), HitTestGrid, FVector2D::ZeroVector, Time, 1.f / 60.f);
            Fixture.Root->Paint(PaintArgs, Geometry, FSlateRect(0, 0, ViewportPixels.X, ViewportPixels.Y),
                DrawElements, 0, FWidgetStyle(), true);
        }
        return Result;
    }

    const FGeometry* ProbeGeometry(FAutomationTestBase& Test, const BYMUIAutomation::FFixture& Fixture,
        const FGeometryMap& Geometry, const TCHAR* Name)
    {
        const TSharedPtr<SWidget>* Widget = Fixture.Probes.Find(FName(Name));
        if (!Test.TestTrue(FString(TEXT("Production probe exists: ")) + Name, Widget && Widget->IsValid())) return nullptr;
        const FGeometry* Found = Geometry.Find(Widget->Get());
        Test.TestTrue(FString(TEXT("Production probe is visibly arranged: ")) + Name, Found != nullptr);
        return Found;
    }

    bool Inside(const FGeometry& Child, const FGeometry& Parent, double Tolerance = 2.)
    {
        const FVector2D ChildMin = Child.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2D ChildMax = Child.LocalToAbsolute(Child.GetLocalSize());
        const FVector2D ParentMin = Parent.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2D ParentMax = Parent.LocalToAbsolute(Parent.GetLocalSize());
        return ChildMin.X >= ParentMin.X - Tolerance && ChildMin.Y >= ParentMin.Y - Tolerance
            && ChildMax.X <= ParentMax.X + Tolerance && ChildMax.Y <= ParentMax.Y + Tolerance;
    }

    void SingleLine(FAutomationTestBase& Test, const BYMUIAutomation::FFixture& Fixture,
        const FGeometryMap& Geometry, const TCHAR* Name, float DPI)
    {
        const FGeometry* Allotted = ProbeGeometry(Test, Fixture, Geometry, Name); if (!Allotted) return;
        const TSharedPtr<SWidget> Widget = Fixture.Probes.FindChecked(FName(Name));
        if (!Test.TestEqual(FString(Name) + TEXT(" is the actual text leaf"), Widget->GetType(), FName(TEXT("STextBlock")))) return;
        const TSharedPtr<STextBlock> Label = StaticCastSharedPtr<STextBlock>(Widget);
        const TSharedRef<STextBlock> Reference = SNew(STextBlock).Text(Label->GetText()).Font(Label->GetFont()).AutoWrapText(false);
        Reference->SlatePrepass(DPI);
        const FVector2D Expected = Reference->GetDesiredSize(), Actual = Label->GetDesiredSize();
        Test.TestTrue(FString(Name) + TEXT(" keeps its natural single-line width"), Actual.X >= Expected.X - 1.);
        Test.TestTrue(FString(Name) + TEXT(" never wraps into a tall narrow column"), Actual.Y <= Expected.Y + 2.);
        Test.TestTrue(FString(Name) + TEXT(" is allotted enough local width for the label"), Allotted->GetLocalSize().X >= Expected.X - 2.);
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMLoginLayoutTest, "BackyardMMO.UI.LoginLayoutAcrossResizeAndDPI",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMLoginLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing UI fixtures"), FSlateApplication::IsInitialized())) return false;
        for (const TCHAR* Variant : {TEXT("Login"), TEXT("LoginLongStatus")})
        {
            for (const float DPI : {1.f, 1.25f, 1.5f})
            {
                const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
                if (!Ready(*this, Fixture)) return false;
                double Time = 1.;
                // Reuse the same widgets while resizing, including narrow -> wide.
                for (const FVector2D Viewport : {FVector2D(800, 600), FVector2D(1920, 1080), FVector2D(1280, 720), FVector2D(2560, 1440)})
                {
                    const FGeometryMap Geometry = Layout(Fixture, Viewport, DPI, Time);
                    const FGeometry Root = FGeometry::MakeRoot(Viewport / DPI, FSlateLayoutTransform(DPI));
                    const FGeometry* Frame = ProbeGeometry(*this, Fixture, Geometry, TEXT("Frame"));
                    const FGeometry* Body = ProbeGeometry(*this, Fixture, Geometry, TEXT("Body"));
                    const FGeometry* Actions = ProbeGeometry(*this, Fixture, Geometry, TEXT("Actions"));
                    const FGeometry* Status = ProbeGeometry(*this, Fixture, Geometry, TEXT("Status"));
                    if (Frame) TestTrue(TEXT("The entire login frame fits the viewport"), Inside(*Frame, Root));
                    if (Body && Frame) TestTrue(TEXT("The login form remains inside its frame"), Inside(*Body, *Frame));
                    if (Actions && Body) TestTrue(TEXT("Login actions remain inside the form"), Inside(*Actions, *Body));
                    if (Status && Body) TestTrue(TEXT("Long host status stays in its bounded region inside the form"), Inside(*Status, *Body));
                    if (FString(Variant) == TEXT("LoginLongStatus"))
                    {
                        const FGeometry* Paragraph = ProbeGeometry(*this, Fixture, Geometry, TEXT("Paragraph"));
                        const TSharedPtr<SWidget>* ParagraphWidget = Fixture.Probes.Find(FName(TEXT("Paragraph")));
                        if (Paragraph && ParagraphWidget && (*ParagraphWidget)->GetType() == FName(TEXT("STextBlock")))
                        {
                            const TSharedPtr<STextBlock> Message = StaticCastSharedPtr<STextBlock>(*ParagraphWidget);
                            TestTrue(TEXT("The actual long-status fixture includes an unbroken host-error token"), Message->GetText().ToString().Contains(FString::ChrN(80, TEXT('W'))));
                            if (Status) TestTrue(TEXT("An unbroken host-error token wraps within the status width"), Message->GetDesiredSize().X <= Status->GetLocalSize().X + 2.);
                        }
                    }
                    SingleLine(*this, Fixture, Geometry, TEXT("Title"), DPI);
                    SingleLine(*this, Fixture, Geometry, TEXT("Caption"), DPI);
                }
            }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMSharedDialogLayoutTest, "BackyardMMO.UI.SharedDialogAndHUDContainment",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMSharedDialogLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing UI fixtures"), FSlateApplication::IsInitialized())) return false;
        for (const TCHAR* Variant : {TEXT("SharedDialog"), TEXT("HUD")})
        {
            for (const float DPI : {1.f, 1.25f, 1.5f})
            {
                const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
                if (!Ready(*this, Fixture)) return false;
                double Time = 1.;
                for (const FVector2D Viewport : {FVector2D(800, 600), FVector2D(1920, 1080), FVector2D(1280, 720)})
                {
                    const FGeometryMap Geometry = Layout(Fixture, Viewport, DPI, Time);
                    const FGeometry Root = FGeometry::MakeRoot(Viewport / DPI, FSlateLayoutTransform(DPI));
                    SingleLine(*this, Fixture, Geometry, TEXT("Title"), DPI);
                    SingleLine(*this, Fixture, Geometry, TEXT("Caption"), DPI);
                    const bool bHUD = FString(Variant) == TEXT("HUD");
                    for (const TCHAR* Name : {bHUD ? TEXT("StatusPanel") : TEXT("Frame"), TEXT("Actions"), TEXT("Status")})
                    {
                        const FGeometry* Item = ProbeGeometry(*this, Fixture, Geometry, Name);
                        if (Item) TestTrue(FString(Variant) + TEXT(" keeps ") + Name + TEXT(" within the viewport"), Inside(*Item, Root));
                    }
                    const FGeometry* Paragraph = ProbeGeometry(*this, Fixture, Geometry, TEXT("Paragraph"));
                    const FGeometry* Body = ProbeGeometry(*this, Fixture, Geometry, bHUD ? TEXT("StatusPanel") : TEXT("Body"));
                    if (Paragraph && Body) TestTrue(TEXT("Wrapped explanatory text stays inside its allotted body"), Inside(*Paragraph, *Body));
                }
            }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMCanonicalUIImageLayoutTest, "BackyardMMO.UI.CanonicalImageAspectDuringTextureReadiness",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMCanonicalUIImageLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing UI fixtures"), FSlateApplication::IsInitialized())) return false;
        for (const TCHAR* Variant : {TEXT("Login"), TEXT("ColdAssets")})
        {
            const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
            if (!Ready(*this, Fixture)) return false;
            double Time = 1.;
            for (const FVector2D Viewport : {FVector2D(800, 600), FVector2D(1920, 1080)})
            {
                const FGeometryMap Geometry = Layout(Fixture, Viewport, 1.25f, Time);
                const FGeometry* Logo = ProbeGeometry(*this, Fixture, Geometry, TEXT("Logo"));
                const FGeometry* Body = ProbeGeometry(*this, Fixture, Geometry, TEXT("Body"));
                if (!Logo) continue;
                const FVector2D Desired = Fixture.Probes.FindChecked(FName(TEXT("Logo")))->GetDesiredSize();
                TestEqual(TEXT("Logo desired width comes from its original PNG"), Desired.X, 518.);
                TestEqual(TEXT("Logo desired height comes from its original PNG"), Desired.Y, 160.);
                const FVector2D Min = Logo->LocalToAbsolute(FVector2D::ZeroVector), Max = Logo->LocalToAbsolute(Logo->GetLocalSize());
                const FVector2D Size = Max - Min;
                TestTrue(TEXT("The arranged logo preserves the original 518:160 aspect"), Size.Y > 0 && FMath::IsNearlyEqual(Size.X / Size.Y, 518. / 160., .001));
                if (Body) TestTrue(TEXT("The original-aspect logo fits inside its login body"), Inside(*Logo, *Body));
            }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMEconomyDialogLayoutTest, "BackyardMMO.UI.StoreMushroomAndDecorationContainment",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMEconomyDialogLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing UI fixtures"), FSlateApplication::IsInitialized())) return false;
        for (const TCHAR* Variant : {TEXT("Store"), TEXT("StoreLongText"), TEXT("StoreSpeedups"), TEXT("Mushroom"), TEXT("Decoration")})
        {
            for (const float DPI : {1.f, 1.25f, 1.5f, 2.f})
            {
                const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
                if (!Ready(*this, Fixture)) return false;
                double Time = 1.;
                for (const FVector2D Viewport : {FVector2D(800, 600), FVector2D(1920, 1080), FVector2D(1024, 768), FVector2D(2560, 1440)})
                {
                    const FGeometryMap Geometry = Layout(Fixture, Viewport, DPI, Time);
                    const FGeometry Root = FGeometry::MakeRoot(Viewport / DPI, FSlateLayoutTransform(DPI));
                    const FGeometry* Frame = ProbeGeometry(*this, Fixture, Geometry, TEXT("Frame"));
                    const FGeometry* Body = ProbeGeometry(*this, Fixture, Geometry, TEXT("Body"));
                    const FGeometry* Actions = ProbeGeometry(*this, Fixture, Geometry, TEXT("Actions"));
                    const FGeometry* Status = ProbeGeometry(*this, Fixture, Geometry, TEXT("Status"));
                    if (Frame) TestTrue(FString(Variant) + TEXT(" keeps its full original frame in the viewport"), Inside(*Frame, Root));
                    if (Body && Frame) TestTrue(FString(Variant) + TEXT(" keeps its form inside the frame"), Inside(*Body, *Frame));
                    if (Actions && Body)
                    {
                        TestTrue(FString(Variant) + TEXT(" retains usable actions inside the form"), Inside(*Actions, *Body));
                        TestTrue(TEXT("Purchase and pick actions retain a positive arranged area"), Actions->GetLocalSize().X > 0 && Actions->GetLocalSize().Y > 0);
                    }
                    if (Status && Body) TestTrue(FString(Variant) + TEXT(" bounds status text within the form"), Inside(*Status, *Body));
                    SingleLine(*this, Fixture, Geometry, TEXT("Title"), DPI);
                }
            }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMWorkerStripLayoutTest, "BackyardMMO.UI.WorkerStripAcrossResizeAndDPI",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMWorkerStripLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing UI fixtures"), FSlateApplication::IsInitialized())) return false;
        for (const float DPI : {1.f, 1.25f, 1.5f, 2.f})
        {
          for (const TCHAR* Variant : {TEXT("Workers"), TEXT("HUDWorkers")})
          {
            const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
            if (!Ready(*this, Fixture)) return false;
            double Time = 1.;
            for (const FVector2D Viewport : {FVector2D(640, 360), FVector2D(800, 450), FVector2D(800, 600), FVector2D(1920, 1080), FVector2D(600, 900)})
            {
                const FGeometryMap Geometry = Layout(Fixture, Viewport, DPI, Time);
                const FGeometry Root = FGeometry::MakeRoot(Viewport / DPI, FSlateLayoutTransform(DPI));
                const FGeometry* Strip = ProbeGeometry(*this, Fixture, Geometry, TEXT("Strip"));
                if (Strip) TestTrue(TEXT("All five worker slots fit the viewport"), Inside(*Strip, Root));
                double PreviousBottom = -1.e9;
                for (int32 Index = 0; Index < 5; ++Index)
                {
                    const FString Name = TEXT("Worker") + FString::FromInt(Index);
                    const FGeometry* Worker = ProbeGeometry(*this, Fixture, Geometry, *Name);
                    if (!Worker) continue;
                    if (Strip) TestTrue(TEXT("Worker button stays inside its actual strip"), Inside(*Worker, *Strip));
                    const FVector2D Minimum = Worker->LocalToAbsolute(FVector2D::ZeroVector);
                    const FVector2D Maximum = Worker->LocalToAbsolute(Worker->GetLocalSize());
                    TestTrue(TEXT("Worker buttons have visible nonzero clickable dimensions"), Maximum.X > Minimum.X && Maximum.Y > Minimum.Y);
                    TestTrue(TEXT("Worker slots remain vertically ordered without overlap"), Minimum.Y >= PreviousBottom - 1.);
                    PreviousBottom = Maximum.Y;
                }
                if (FString(Variant) == TEXT("HUDWorkers"))
                {
                    const FGeometry* Dock = ProbeGeometry(*this, Fixture, Geometry, TEXT("Actions"));
                    if (Strip && Dock)
                    {
                        const double WorkerBottom = Strip->LocalToAbsolute(Strip->GetLocalSize()).Y;
                        const double DockTop = Dock->LocalToAbsolute(FVector2D::ZeroVector).Y;
                        TestTrue(TEXT("Worker strip stays above the real gameplay dock at small sizes"), WorkerBottom <= DockTop + 1.);
                    }
                }
            }
          }
        }
        return true;
    }

    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBYMMapRoomLayoutTest, "BackyardMMO.UI.MapRoomCanvasActionsAndDetailsAcrossResizeAndDPI",
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    bool FBYMMapRoomLayoutTest::RunTest(const FString& Parameters)
    {
        if (!TestTrue(TEXT("Slate must be initialized before constructing the real map UI"), FSlateApplication::IsInitialized())) return false;
        for (const TCHAR* Variant : {TEXT("MapRoom"), TEXT("MapRoomLongText"), TEXT("MapRoomLocked"), TEXT("MapRoomPaged"), TEXT("MapAttack")})
        {
            for (const float DPI : {1.f, 1.25f, 1.5f, 2.f})
            {
                const BYMUIAutomation::FFixture Fixture = BYMUIAutomation::BuildLayoutFixture(FName(Variant));
                if (!Ready(*this, Fixture)) return false;
                double Time = 1.;
                for (const FVector2D Viewport : {FVector2D(640, 360), FVector2D(1920, 1080), FVector2D(800, 600), FVector2D(2560, 1440)})
                {
                    const FGeometryMap Geometry = Layout(Fixture, Viewport, DPI, Time);
                    const FGeometry Root = FGeometry::MakeRoot(Viewport / DPI, FSlateLayoutTransform(DPI));
                    const FGeometry* Frame = ProbeGeometry(*this, Fixture, Geometry, TEXT("Frame"));
                    const FGeometry* Body = ProbeGeometry(*this, Fixture, Geometry, TEXT("Body"));
                    const FGeometry* Actions = ProbeGeometry(*this, Fixture, Geometry, TEXT("Actions"));
                    if (Frame) TestTrue(FString(Variant) + TEXT(" keeps the complete map frame on-screen"), Inside(*Frame, Root));
                    if (Frame && Body) TestTrue(TEXT("The map body remains inside its source frame"), Inside(*Body, *Frame));
                    if (Actions && Body)
                    {
                        TestTrue(TEXT("Navigation and attack confirmation actions remain inside the form"), Inside(*Actions, *Body));
                        TestTrue(TEXT("Actions retain nonzero clickable dimensions"), Actions->GetLocalSize().X > 0 && Actions->GetLocalSize().Y > 0);
                    }
                    SingleLine(*this, Fixture, Geometry, TEXT("Title"), DPI);
                    if (FString(Variant) == TEXT("MapAttack")) continue;
                    for (const TCHAR* Name : {TEXT("MapCanvas"), TEXT("MapNavigation"), TEXT("MapDetails"), TEXT("MapPlaces")})
                    {
                        const FGeometry* Region = ProbeGeometry(*this, Fixture, Geometry, Name);
                        if (Region && Body)
                        {
                            TestTrue(FString(Name) + TEXT(" stays inside the resized map form"), Inside(*Region, *Body));
                            TestTrue(FString(Name) + TEXT(" retains positive usable area"), Region->GetLocalSize().X > 0 && Region->GetLocalSize().Y > 0);
                        }
                    }
                    const FGeometry* Canvas = ProbeGeometry(*this, Fixture, Geometry, TEXT("MapCanvas"));
                    const FGeometry* Details = ProbeGeometry(*this, Fixture, Geometry, TEXT("MapDetails"));
                    if (Canvas && Details)
                    {
                        const double CanvasRight = Canvas->LocalToAbsolute(Canvas->GetLocalSize()).X;
                        const double DetailsLeft = Details->LocalToAbsolute(FVector2D::ZeroVector).X;
                        TestTrue(TEXT("The map and selected-cell details do not overlap"), CanvasRight <= DetailsLeft + 2.);
                    }
                }
            }
        }
        return true;
    }
}
#endif

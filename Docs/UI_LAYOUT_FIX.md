# Shared native UI layout correction — 0.1.8.1

The supplied screenshot shows the login logo compressed horizontally, the title and account checkbox caption wrapped into narrow columns, and the action buttons and footer outside the decorative frame. This update corrects the shared UI sizing paths used by login, HUD and gameplay dialogs. It retains the original artwork and the v0.1.8 gameplay implementation.

## Image dimensions

UI brushes previously captured `UTexture2D::GetSizeX/GetSizeY` once when the widget was constructed. A texture still using its temporary default resource can report dimensions that do not describe the original artwork. Caching those dimensions leaves the image's desired aspect ratio wrong after the texture becomes ready.

The original logo is 518 × 160 pixels. Its nontransparent artwork occupies 437 × 58 pixels. Fitting that image into a 360 × 111 box should produce approximately 303 × 40 visible pixels. Treating its desired size as square produces approximately 94 × 40 visible pixels, consistent with the compressed logo in the report. This is a numerical explanation of the screenshot, not a captured Unreal reproduction.

UI image dimensions now come from the bundled source manifest. Image fitting uses those original dimensions, independently of temporary texture resource sizes. The original PNG files and all previously delivered source assets are unchanged.

## Text and frame sizing

The shared text helpers previously enabled automatic wrapping for every label, heading, paragraph and live value. Centered or intrinsic-width slots then allowed wrapped text to affect the width used for its next wrap measurement. Short labels could settle into narrow columns and increase the height of the surrounding form.

Short labels now use their natural nonwrapping size. Paragraphs and status messages use explicit wrap widths appropriate to their panels. The login title and checkbox caption have stable width constraints. The login form uses bounded space so long status text cannot push its actions through the lower border.

The frame's fixed outer desired size alone was insufficient: its children could request more height and paint outside it. The correction contains the form and preserves access to its controls rather than only hiding overflowing buttons.

## Validation boundary

Four targeted asset checks verify manifest dimensions against original image headers, exact hashes, importability and retained UI provenance. The shared sizing code is reviewed separately. The included native Slate tests exercise real widget measurement and arrangement, not the standalone simulation adapters. They require Unreal and have **not** been executed in this environment. Source checks cannot establish rendered visual correctness, input hit testing, or UHT/UBT compatibility.

On UE 5.8, open `BackyardMonstersNXT.uproject` normally. Inspect both the mainmenu and connected-server sign-in forms, then the construction, building, Hatchery, Locker, Academy, Bunker, rankings and result dialogs. Verify normal and long status text, resize from a small window to a large one, and check desktop DPI scaling. Confirm labels remain legible, images retain their aspect ratios, and every action stays accessible inside its frame. No texture reimport or save reset is required for this source correction.

## Engine reference

Epic's [STextBlock AutoWrapText documentation](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Slate/STextBlock/FArguments/AutoWrapText) describes delayed automatic-wrap measurement and recommends explicit `WrapTextAt` when text size affects surrounding layout.

# Compile correction — 0.2.0.1

This full source package corrects the errors in the supplied `Log(10).txt` from Unreal Engine 5.8.4 / MSVC 14.44.

| Reported issue | Correction |
| --- | --- |
| Ten C2039 math errors in `BYMMapAuthorityTests.cpp` | Include `<cmath>` directly for `std::floor`, `ceil`, `sin` and `cos`. |
| Twelve JSON key conversion errors in three simulation includes | Convert each shared-storage JSON key to an owned `FString` before comparisons, map lookups, inserts or species validation. Covers site armies, retaliation owners, saved armies, Bunker garrisons and Stronghold emitters. |
| C2228 on renderer `.IsValid()` | Check the `FSlateRenderer*` returned by `GetRenderer()` for null. |
| C2039 on `FReply::SetKeyboardFocus` | Use `FReply::SetUserFocus` with the existing mouse focus cause. |

The supplied log completed Unreal Header Tool and then failed C++ compilation. It does not demonstrate a successful full build. The existing HUD font-constructor deprecation is a warning and is not one of the build failures.

The offline harness now models JSON iteration keys without an implicit conversion to `FString`. Focused compiler checks require the math include and exercise the logged JSON and Slate type constraints, including rejection of the old expressions. Current results and exact source hashes are recorded in `VALIDATION.md` and `Tools/OfflineValidation/last-run-manifest.json`. Unreal Engine is unavailable here, so native compilation, editor rendering and multiplayer execution are not claimed.

All previous project files and original SourceAssets are retained. This correction does not change game rules, account/world save formats, bot behavior or UI layout. Extract the full package and open `BackyardMonstersNXT.uproject` normally; no setup script is required.

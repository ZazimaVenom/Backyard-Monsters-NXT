# UE 5.8.4 compile fix — source preview 0.1.4

The supplied `Log(8).txt` reports one blocking diagnostic: MSVC C4456 in `FBYMSimulation::FImpl::LoadCatalog`. The collision-footprint coordinate loop declared `V` inside the building loop, which already declared `V`. Unreal treats this local shadowing diagnostic as an error.

The inner variable is now `CoordinateValue`, with the validation condition expanded for readability. Validation behavior is unchanged. No compiler warning or error setting was disabled. Gameplay, assets, account storage, rival simulation and save formats are unchanged.

The log shows UHT succeeded and the other listed runtime/editor source files compiled. It does not show a successful final build. The separate `FSlateFontInfo` constructor deprecation is a warning, not the blocker.

Validation performed:

- Independently reviewed the corrected loop and nearby scopes for further shadowing.
- Compiled the actual extracted validation loop using C++20 with `-Wall -Wextra -Wshadow -Werror` and minimal type adapters: the original statement fails as expected; the corrected statement compiles.
- Valid coordinates pass; null, non-number, infinite and NaN coordinates remain rejected.
- Rechecked the complete source/asset package and archive integrity.

A complete UE 5.8 rebuild and gameplay execution after the fix were not run in this environment. Open `BackyardMonstersNXT.uproject` normally and let Unreal rebuild the modules.

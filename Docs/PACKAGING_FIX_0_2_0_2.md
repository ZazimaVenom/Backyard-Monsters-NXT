# Windows packaging correction — 0.2.0.2

The supplied `UBA-BackyardMonstersNXT-Win64-Development_2.txt` and `Package-20261127-085238.log` identify one blocking compiler error: `BYMBoard.cpp` calls `UTexture2D::IsDefaultTexture()`, which is unavailable to the packaged game's non-editor target. The editor target succeeds, while the game build fails before cooking and staging complete.

The method is an editor texture-compilation placeholder check. It now runs only inside `#if WITH_EDITOR`. All targets still reject null textures. The editor still hides pending sprites and retries their atlas setup once the real texture becomes ready, preserving the earlier fix for distorted wandering-monster frames. Original sheet dimensions, frame boundaries, animation, gameplay and saves are unchanged.

The focused check in `Tools/OfflineValidation/check_packaging_contracts.py` compiles the actual readiness block for editor and game configurations using a strict game texture stub with no editor-only method. It also tests the old expression as a required compilation failure and exercises readiness outcomes. Both corrected target modes passed 24 behavior assertions. The historical editor control passed its 12 assertions; the historical game call failed compilation as required. Its result and source hashes are recorded beside the script. This is a narrow compiler reproduction, not a native Unreal build or successful Windows package.

All assets and every prior project file are retained in this full archive. Open `BackyardMonstersNXT.uproject` normally, allow the module rebuild if prompted, and package Windows through Unreal Editor. No setup script is required.

The HUD font-constructor deprecation in the log is a nonblocking warning and remains unchanged. Full Unreal compilation, cooking, staging and packaged play cannot be executed in this environment.

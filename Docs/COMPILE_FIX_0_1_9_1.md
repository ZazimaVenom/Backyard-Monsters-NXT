# UE 5.8.4 worker-material test compile correction

The supplied `Log(9).txt` reports one blocking compiler error in `BYMWorkerMaterialTests.cpp`: C2666, ambiguous `FAutomationTestBase::TestEqual` overload. The actual argument is `int64` because `FMath::RoundToInt` receives a double expression; the expected loop index is `int32`.

The corrected code binds the rounded result to `const int64 SelectedDirection` and casts the expected direction to `int64`. Both arguments now select the same integer overload. The original UV calculation, rounding, thirty-direction coverage and assertion remain intact. No tests are disabled, and no warning policy, gameplay, assets, UI or save behavior changes.

## Validation

- The user-provided UE 5.8.4 log shows successful UHT, runtime/game DLL linking and all other listed editor translation units. The HUD font-constructor deprecation is a separate nonblocking warning.
- `python Tools/OfflineValidation/check_automation_overloads.py` reproduces the original ambiguous call using the numeric overload signatures reported by the compiler, then compiles the actual corrected source expression and passes all thirty facing-cell assertions.
- Independent source review covered 31 native test files and 745 equality assertions without finding another unresolved mixed-type comparison.
- The 65 inputs of the recorded 93-body / 74,296-assertion simulation run are byte-identical. Those gameplay tests were not rerun for this editor-test-only correction. Source/asset integrity was rechecked for the final package.

The isolated compiler check uses type and numeric overload adapters. It does not execute Unreal or render the worker material. A full UHT/UBT rebuild after this correction remains unrun in this environment.

Open `BackyardMonstersNXT.uproject` normally and accept Unreal's module rebuild prompt. The complete project, plugin and source assets are included in the updated archive.

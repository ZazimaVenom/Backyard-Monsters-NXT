# Original building animation

Backyard Monsters NXT prepares the original animated building layers into 28 shared atlases covering 62 building levels. Fifty-two levels use an activity loop; ten Storage Silo levels select a resource-fullness pose. Each atlas combines the original animation cell, static top, base, and shadow layers at their original offsets. Repeated source artwork is shared across levels. No generated replacement artwork, procedural wobble, or invented animation frames are used.

The catalog is `Plugins/BackyardMMO/Content/Data/catalog.json`. Per-level provenance is recorded in `building-animation-provenance.json`. The source scripts used for this pass are retained under `Content/Data/Original/BuildingAnimation/`; hatchery queue and housing source references are also retained under `Content/Data/Original/`.

| Source building | Levels | Animation condition | Inactive behavior |
| --- | ---: | --- | --- |
| Twig Snapper, Pebble Shiner, Putty Squisher, Goo Factory (1–4) | 40 | Host reports the collector can produce: built, not upgrading, functional, and not full or blocked | Hold the displayed source pose |
| Storage Silo (6) | 10 | Select the original pose for stored resource balances | Retain the selected fullness pose |
| Monster Locker (8) | 4 | A monster unlock is active and the building has an attained level with no pending construction/upgrade work | Hold the displayed source pose |
| Hatchery (13) | 3 | This Hatchery has active stage-one production and can function | Reset to source pose zero |
| Monster Academy (26) | 5 | This Academy has active research and an attained level with no pending construction/upgrade work | Hold the displayed source pose |

The host sends only the coarse `working` boolean needed for visible activity. Rendering does not advance production, consume resources, or reveal private queue contents. Different buildings retain their own material, frame counter, and activity state. Repeated snapshots update the current state without restarting an initialized animation.

In source preview 0.1.7, construction/upgrade work persists independently of repair. A zero `workReadyAt` while damaged or repairing means that the pending work is paused, not finished. Activity gates use the retained job state rather than treating that zero deadline as permission to animate production. The current attained level continues to select the building's artwork until its upgrade completes.

Locker/Academy research access is distinct from their visible activity gate. The separate source entry points allow a completed Locker or Academy to start and continue unlock/research while damaged, repairing or upgrading. The activity loop still holds during the building upgrade, and damaged artwork takes precedence below half health. A held/static building image therefore does not imply that its independent research countdown has paused. See `MONSTER_PROGRESSION.md` and `BUILDING_LIFECYCLE.md`.

## Original display cadence

The original Flash display clock is 40 Hz. Collectors, the Locker, and the Academy animate on ticks divisible by three, or otherwise by ten, when there are no creeps in the yard. These conditions form a union: tick ten must advance even though it is not divisible by three. With creeps present or during battle, only ticks divisible by ten advance. The Hatchery uses the corresponding divisors two and seven. NXT treats visible resident monsters as creeps for this cadence.

`BYMBuildingAnimation::SelectFrame` counts those qualifying display ticks independently of Unreal's render rate. It copies the current source frame before advancing the next frame counter, matching `BFOUNDATION.AnimFrame`. Paused time continues the display-clock phase without consuming the next source frame. Changing between fast and slow cadence retains that counter; it does not restart or jump to a different precomputed clip. Multiple calls at the same display time do not advance twice.

`sourceFrameCycle` maps the source frame counter to complete available cells. `workingFrames` and `workingSlowFrames` retain the full reference display sequences, including held frames. The catalog also records `fastTickDivisor`, `slowTickDivisor`, and `sourceDisplayHz`.

Initial pose and display phase are separate source properties. `BFOUNDATION` initializes the pose with `int(random * (declaredFrames - 2))`; the prepared `sourceRandomStartFrames` records that exclusive upper bound. Collectors and the Locker start their display counter at an integer from zero through four (`sourceDisplayPhaseCount = 5`). The Hatchery and Academy start their display phase at zero (`sourceDisplayPhaseCount = 1`). A main-yard Hatchery already producing when loaded can start at a source random pose; an inactive Hatchery resets to zero. Stable building seeds provide independent initial presentation without replicating cosmetic randomness.

## Silo selection and source inconsistencies

The Silo follows `BUILDING6.Update`: `floor(26 * sum(resource balances) / sum(resource capacities))`. It refreshes on every third source display tick. The supplied strip contains only frames zero through twenty-five, so full storage clamps to frame twenty-five instead of sampling outside the image. This is a fullness indicator, not a looping animation.

The supplied source has several mismatches between declared and available cells. They are explicit in catalog provenance:

| Source case | Prepared playback |
| --- | --- |
| Hatchery has 31 cells, but its normal counter wraps at 30 | Play cells 0–29; preserve cell 30 without selecting it |
| Locker level 2 declares 20 cells but supplies 21 | Play cells 0–19; preserve cell 20 without selecting it |
| Locker level 4 declares 21 cells but supplies 20 | Preserve the 21-step source cycle; its missing step 20 holds complete cell 19 |
| Full Silo requests cell 26 from a 26-cell strip | Clamp to complete cell 25 |

Atlas metadata retains the exact PNG width and height, cell dimensions, original pivot, layer offsets, and source hashes. Runtime sampling uses these dimensions and a half-texel inset to avoid neighboring frames bleeding into a sprite. It waits for an actual texture resource before committing the layout. These protections also apply to the wandering-monster sprite fix.

## Damaged and static original artwork

These activity buildings have no supplied `animdamaged` layer. Below half health they use the original damaged static composite, and destroyed buildings use the original destroyed artwork. The intact animation is never overlaid on a damaged building.

Housing, Town Hall, Flinger, Map Room, and the ordinary walls do not have an animated source building layer. They retain their original static artwork. Housing's visible movement comes from the resident monsters. Cannon Tower likewise retains its source-static building image; its projectile is animated separately. Existing Sniper, Laser, and Tesla defense presentation remains separate from this activity pass.

Overdrive-specific frame skipping and additional original systems that have not been implemented are not claimed as complete. This pass reproduces the supplied normal production/research animation and the documented state transitions; it does not constitute a complete recreation of every original game system.

## Validation

The original building-animation validation pass ran all 17 then-existing tests in `python3 Tools/test_asset_pipeline.py`. The activity tests check original layer hashes, atlas geometry and pivots, first-frame equality with the original static composite, actual motion in later frames, source cadence sequences, random-start metadata, original frame limits, damaged-art policy, and retained source references. The timing-metadata regeneration was also compared against the existing manifest: every generated building atlas retained its previous SHA-256 hash. Later additions and current suite totals are recorded in the release validation report.

The actual `BYMBuildingAnimation.cpp` and its three native automation test bodies passed 68,875 assertions in an offline C++17 harness with minimal Unreal type shims. This checks 30/40/60/120 Hz rendering, bulk advancement, repeated snapshot calls, paused hold/resume, Hatchery reset, cadence transitions, phase offsets, clock rollback, and invalid inputs. It is not an Unreal Engine compilation.

The native editor test `BackyardMMO.Presentation.BuildingActivityMaterials` is included to exercise an actual `ABYMBoard`, imported textures, and material UVs in an isolated editor world. It checks changing activity frames, stable material bindings during snapshots, held and reset poses, the original Silo frames, and damaged-art transitions. Two collectors share one texture atlas but must retain distinct material instances: one holds its pose while the other advances, then their activity states switch while snapshots continue. This test requires the normal editor asset setup and has not been run in Unreal Engine in this environment. Unreal compilation, visual PIE inspection, packaged multiplayer checks, and engine automation remain necessary before claiming engine validation.

## Monster Bunker door in 0.1.8

All five Bunker levels share one atlas composed from the original 15 door cells and shadow offsets. Its source logical phase reaches 15, while rendering clamps to the final available cell 14. The host supplies opening, held-open, closing and closed states; a held endpoint never drifts between snapshots. The original door sound uses deduplicated battle events. Below half health and when destroyed, the original static damage/wreck composites remain in use. See `MONSTER_BUNKER.md`; native material and defender presentation tests are included but require Unreal.

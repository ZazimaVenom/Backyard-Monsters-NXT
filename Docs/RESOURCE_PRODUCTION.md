# Source collector and repair rules

The main-yard economy helpers are implemented in `BYMEconomy.h/.cpp`; their inputs are host-owned state. They perform no networking, account storage or file access. Source preview 0.1.8 keeps the existing host-local account `.sav` and world JSON storage formats; its simulation subdocument writes schema 5 and still reads schemas 1–4. The independent lifecycle introduced in schema 4 is retained.

## Original rules

| Behavior | Supplied source |
| --- | --- |
| Main-yard collectors function at or above half health, with construction/upgrade/fortification finished | `client/scripts/BRESOURCE.as:305–340` |
| A cycle lasts `baseCycle + ceil(baseCycle * (4 - 4 * health / maxHealth))` seconds | `client/scripts/BRESOURCE.as:384–385` |
| Resource batches arrive at cycle completion; stored resources stop at collector capacity | `client/scripts/BRESOURCE.as:399–420` |
| Damage does not rescale an already-running countdown; the next cycle uses current health | `client/scripts/BRESOURCE.as:291–340, 372–385` |
| Saved collector state includes stock, active production and countdown | `client/scripts/BRESOURCE.as:480–495, 520–526` |
| Banking transfers only available wallet room, retaining unaccepted stock; taking from a stopped/full collector starts a fresh cycle | `client/scripts/BASE.as:4485–4565` |
| Player collection and Bank All require full health and no construction/upgrade/fortification countdown | `client/scripts/BUILDINGINFO.as:96–141, 465` |
| Completed silos retain their current-level storage contribution while damaged, repairing or upgrading | `client/scripts/BASE.as:4785–4793` |
| Free repair heals `ceil(maxHealth / min(3600, repairTime))` HP per source second | `client/scripts/BFOUNDATION.as:1368–1375` |
| Ordinary post-tutorial collection awards `ceil(accepted transfer / 2)` experience separately for each collector | `client/scripts/BRESOURCE.as:444–457` |

For a ten-second base cycle, production takes ten seconds at full health, twenty at 75% health and thirty at 50%. Below half health, the existing countdown pauses. At capacity it stops; banking does not discard resources that fail to fit into the wallet.

## Native timing and configured overrides

The helper advances repairs and production using continuous host time, splitting at health thresholds and cycle-penalty boundaries. Inside a constant-health band it skips complete offline batches in one calculation. This preserves the same result across different update sizes without looping once per offline second.

This is an intentional deterministic adaptation of Flash timing. The original `BRESOURCE.Tick(seconds)` first advances repair through `super.Tick(seconds)` and then processes production using the resulting health, so different original catch-up batch sizes can produce different intermediate results. The native helper retains the source formulas, threshold, batch size and carried countdown, rather than reproducing that batching artifact.

The existing NXT production multiplier scales cycle time while retaining whole source-sized batches. Zero pauses production. Extremely small positive overrides are bounded to the host's one-year timer horizon so they cannot create an invalid or infinite saved countdown. The construction-time multiplier scales the rounded source repair rate. Default multipliers of one apply the original formulas.

`SecondsToHealth` exposes exact half/full-health crossing times for dependent systems. `CanProduce` provides the working state for collector animation and status. `Bank` preserves fractional stock from older saves while transferring whole resources. A level-zero silo contributes nothing until construction completes.

Construction/upgrade work and repair now have independent state. Any damage or active repair pauses paid building work; repair can restore that same unfinished or upgrading building without discarding its job. A collector with pending construction/upgrade remains blocked even when its running work deadline is zero because it is paused. A completed collector with no pending work can resume production at its source health threshold during repair. Attained silo capacity remains available while that silo is damaged, repairing or upgrading. See `BUILDING_LIFECYCLE.md` for cancellation, destruction and migration rules.

The ordinary native collection loop uses the source post-tutorial experience rule. It awards only accepted transfers, rounds separately for each collector, and grants nothing when storage accepts no resources. XP is independent of RP; neither repair nor cancellation awards it. `PLAYER_EXPERIENCE.md` documents the counters, completed-building value and incomplete tutorial integration.

## Validation and limits

Three native tests cover discrete cycle deadlines, damage thresholds, construction pauses, partial/full banking, retained storage, gradual repair, threshold crossings, countdown restoration, long offline catch-up and malformed/extreme inputs. The actual helper and those test bodies also compile in a lightweight offline C++ harness with warnings treated as errors. That harness passed **250,077 assertions**, including **25,000 randomized comparisons** against an independent per-completion reference and differently partitioned elapsed time.

The actual integrated host `Advance` and Hatchery methods, linked to the real economy helper with lightweight Unreal substitutes, additionally passed **20,007 assertions** across **5,000 randomized time-partition cases** at realistic UTC timestamps. Three additional native authority tests cover actual simulation snapshots, save/reload and old-schema migration, selected banking and ownership, source collection gates, silo capacity, and repair thresholds affecting Hatchery progress. These native authority tests are included but were not run here.

The helper validation is not an Unreal build or an end-to-end save/network test. Unreal compilation, native editor automation and runtime checks remain unrun in this workspace. Outpost altitude bonuses, Inferno resource bonuses, original overdrive/buff systems and the full tutorial remain unfinished. The older shared construction/repair deadline has been replaced by the independent schema-4 lifecycle described above. Existing in-progress repair saves retain health; their completion estimate is recomputed from that health using the source rounded repair rate. Migration cannot recover a pending upgrade that an older build had already discarded.

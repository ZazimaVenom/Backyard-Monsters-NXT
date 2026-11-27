# Original player experience and level display

Player experience is separate from the requested RP ladder. The active Flash client combines accumulated action points with a retained base value, then selects a player level from its original table. Town Hall level and monster research levels remain separate progression systems.

## Original rules

| Rule | Native calculation | Original reference |
| --- | --- | --- |
| Completed building value | Sum the current completed-level source time and all four resource costs of eligible buildings, then take `ceil(total / 10)` once | `BASE.as:4839–4869` |
| Retained value | Main-yard base value is a high-water mark; damage, destruction or recycling do not reduce it | `BASE.as:4866–4869` |
| Player points | Accumulated action points plus retained base value | `BASE.as:4885–4886` |
| Construction completion | `floor(first-level source time / 2 + first-level total resource cost / 10)`; Town Hall adds 100 | `BFOUNDATION.as:2893–2918` |
| Upgrade completion | `floor((previous completed-level source time + previous completed-level total resource cost) / 3)` | `BFOUNDATION.as:2431–2459` |
| Collector banking | After the tutorial, `ceil(accepted transfer / 2)` per collector; before tutorial stage 200, accepted transfer one-for-one | `BRESOURCE.as:444–457` |
| Loot | One point per resource credited by the original loot-return process | `BASE.as:1787–1803` |
| Bar and tooltip | Source 200-pixel progress span, displayed level on the star, total points and points remaining in the hover text | `UI_TOP.as:397–404,1363–1379` |

The unusual upgrade calculation deliberately uses the previous level's cost row. The source increments `_lvl` and then reads `costs[_lvl - 2]`. It must not be replaced with an assumed award based on the newly purchased upgrade.

Base value excludes unfinished construction and the source decoration, enemy, immovable and trap classes. A completed building still contributes while damaged, repairing or upgrading. Rounding occurs after summing the whole eligible yard, rather than separately for every building. Source costs and durations determine awards; the NXT construction-speed setting does not multiply or reduce experience.

Collect-all must calculate each accepted collector transfer separately. Two collectors that each transfer one resource award two points, whereas incorrectly rounding their combined transfer would award only one. Full storage accepts no transfer and awards no collection points. The current native game starts beyond the unfinished original tutorial, so normal collector awards use the post-tutorial rule; the helper preserves the tutorial formula for that future system.

The complete original `PointsAdd` call-site audit finds no direct player-experience award for purchasing a training order, hatching, monster unlock/research completion, moving a building or repairing it. Refunds do not subtract previously earned progress. Fortification, quest, gift and premium/autobanking awards belong to their corresponding systems and must not be fabricated as generic action rewards.

## Complete active-client table

`BASE.s_levels` spans three lines and contains **100 thresholds**, not just the first 56 values on its first line. Its `BaseLevel` loop stops before the last entry, so the active Flash client displays levels **1 through 99** and uses the hundredth threshold as the final upper boundary. That boundary is **755,984,118,210,000** points.

The supplied server's `experiencePoints.ts` contains only the first 56 thresholds and its separate helper therefore caps at level 56. This is a discrepancy in the provided project. The native port follows the active Flash client and preserves the entire table. It does not invent a reachable level 100 or silently use the truncated server copy.

`Tools/prepare_experience.py` reads literal integers from the original client, verifies the shorter server prefix, copies the exact source references into `Content/Data/Original/Experience`, writes `Content/Data/experience.json`, and generates `BYMExperienceThresholds.inl`. The regular asset preparer invokes it, so this data remains reproducible without executing original ActionScript or TypeScript.

At the final displayed level, the last original threshold remains the bar's upper boundary. Native visual progress is clamped to 0–1 and remaining points never become negative; this avoids the original bar extending beyond its artwork after the final boundary.

## Host state and migration

The two authoritative counters are accumulated earned experience and the base-value high-water mark. Level, combined points, thresholds, remaining points and the visible fraction are derived. Commands never accept a client-supplied experience award.

Older NXT worlds did not record action experience. Migration starts their earned counter at zero and derives the known current completed-yard value. It does not fabricate rewards for unverifiable historical building, collection or battle actions. Repeated loading or recomputing base value must not award those actions again. The counters remain in the existing host-local world/bot snapshot; account `.sav` serialization and the existing save cadence are unaffected.

Native arithmetic uses signed 64-bit values with an exact-JSON integer ceiling of `2^53 - 1`. Persisted fractional, negative, non-finite and out-of-range values are rejected. Awards saturate safely at the ceiling, instead of reproducing the original ActionScript `uint` overflow on very large individual awards. The complete source threshold table is below that ceiling.

The visible snapshot contract is `yard.experience` / `home.experience` with `level`, `maxLevel`, `earned`, `baseValue`, `points`, `lower`, `upper`, `needed`, `progress` and `atMaxLevel`. This system does not replace RP, Town Hall level or researched monster levels, and does not add new construction or Locker player-level gates.

The native HUD keeps the displayed name, level, bar and experience tooltip together for the currently viewed yard, including a rival yard. This is a presentation extension: the source `UI_TOP.Setup` uses the viewed owner's name outside build mode, but `UI_TOP.SetPoints` updates the star/bar only in `BUILD`, and its experience hover listeners are attached by `setupBuildMode`. The original elastic bar tween and star level-up transition have not been ported. Source artwork and numeric rules are preserved; exact mode-specific animation and screen parity still require an engine comparison.

## Validation and remaining systems

Four source-data tests check the entire multiline table, byte hashes of all preserved references, identical compiled constants, the server/client discrepancy and rejection of executable or malformed table input. Four native helper tests cover all threshold boundaries, source action formulas, per-collector rounding, retained value, migration inputs, exact serialization bounds and overflow.

The actual `BYMExperience.cpp` and the four native test bodies compile in a standalone C++17 harness with lightweight Unreal type adapters. They passed **1,200,542 assertions**, including **100,000 randomized comparisons** against independent binary-search level selection and 128-bit integer award arithmetic. Compiler checks include `-Wall -Wextra -Wshadow -Werror`; the engine's unused test-parameter warning is suppressed in the harness. These checks do not compile the Unreal project or execute networking, saves, UI or actor behavior.

Four additional native authority tests cover schema migration, strict counters, real building commands, cancellation, completion/reload idempotence, partial collection and invasion loot. Those engine tests are included but unrun. The merged offline integration harness executes exact current Advance/Hatchery/base-value methods and accepted collection/loot loops with the real helpers: **100,049 assertions across 25,000 randomized cases passed**. Its restart checks copy state; they do not exercise the real JSON serializer, network transport or full battle termination.

The original tutorial, quest and achievement rewards, gifts, premium resource purchases, autobanking, fortification rewards, empire/outpost accounting, player-level matchmaking and low-level loot bonuses remain separate systems unless explicitly implemented elsewhere. Existing NXT invasion loot and RP rules remain documented adaptations. Engine automation, rendered bar comparison, host/client command checks and restart continuity require testing in Unreal Engine 5.8.

# Monster Bunker: ordinary overworld rules

This port uses the supplied game's ordinary, count-based overworld Bunker branch. Map Room 3 uses a different persistent-creature system with individual health, recovery rules and different capacity. Those branches must not be combined. NXT's existing world-map and army systems are not a complete Map Room 3 implementation.

Eleven exact source files are retained under `Plugins/BackyardMMO/Content/Data/Original/Bunker`. `provenance.json` records their original paths, byte counts and SHA-256 values. The source-data test checks those references and the active branch selection. Source citations below refer to those unmodified files.

## Source capacities and construction

The raw `YARD_PROPS.as` table is not the final ordinary-mode capacity. `GLOBAL.changeNotMaproom3SpecificBuildings` replaces it at line 692; `GLOBAL.SetBuildingProps` selects that modified copy outside Map Room 3 at lines 734–748. Reading only the raw table would halve the ordinary garrison capacity.

| Bunker level | Ordinary capacity | Raw Map Room 3 capacity | Trigger range, logical pixels | Required Town Hall | Required Housing |
| --- | --- | --- | --- | --- | --- |
| 1 | 380 | 190 | 300 | 3 | 1 |
| 2 | 450 | 225 | 350 | 4 | 2 |
| 3 | 540 | 270 | 400 | 5 | 3 |
| 4 | 660 | 330 | 450 | 9 | 3 |
| 5 | 800 | 400 | 500 | 10 | 3 |

The original costs, build times, health, repairs and quantity limits are in `YARD_PROPS.as:2411–2486`. A first Bunker costs 250,000 Twigs, 187,500 Pebbles and 62,500 Putty, takes six hours at the source speed, and requires Town Hall 3 and completed Housing. Its original classification is a defense tower. The Bunker stores defensive monsters; it does not fire a generic tower projectile itself.

`BYMBunkerRules::Capacity` applies the ordinary runtime override without rewriting the preserved raw source catalog. Source quantities allow one Bunker at Town Hall 3, two at Town Hall 5, three at Town Hall 7 and four at Town Hall 8 onward.

## Housing transfers and removal

| Rule | Original evidence |
| --- | --- |
| Eligible ordinary species are C1 through C13 plus C17 | `MONSTERBUNKERPOPUP.as:48–70,195–200` |
| C14 flyers, C15/C16 healers and dependent offspring are not ordinary transfer choices | Same explicit whitelist |
| Transfer uses ready Housing monsters and removes them from the attacking army | `MONSTERBUNKERPOPUP.as:641–685` |
| Putty charge is `floor(current Goo hatch cost × selected count / 2)` for each species batch, then the batches are summed | `MONSTERBUNKERPOPUP.as:464–469,511–526` |
| Capacity uses the current species housing cost | `MONSTERBUNKERPOPUP.as:595–625` |
| Ordinary access requires full Bunker health and no construction, building upgrade or fortification | The sole `MONSTERBUNKER.Show` caller is `BUILDINGINFO.as:507`; its button is gated at `:99–128,214–215` |
| Plain removal discards one stored monster; it does not return it to Housing or refund the transfer fee | `MONSTERBUNKERPOPUP.as:698–743` |

The rounding is deliberately per species batch. A hypothetical Goo cost of 251 gives 125 Putty for one monster, 251 for two of the same species, or 250 for one each of two different species with that cost. Charging each individual separately or rounding the mixed total once would change the source rule.

C17 remains eligible for an already owned inventory; this does not make its source-blocked Locker purchase available. The source Juicer branch and premium-store purchases are distinct flows. A normal remove command must not silently grant their rewards or invent a free withdrawal feature.

## Defensive creatures and timing

`BUILDING22.FindTargets` (`:80–164`) considers a completed, living Bunker and caches up to three nearest non-retreating ground attackers within its level's range. Without Monster Lab powers, normal overworld garrisons do not acquire flying attackers. The source specifically enables C5/C12 anti-air only with their power-up; Inferno cases are another separate branch.

The door opens or closes one source frame per 40 Hz display step (`BUILDING22.TickFast:433–454`). The simulation releases at most one creature on each 30-step boundary of the 72 Hz attack clock, once the door reaches phase 15 (`TickAttack:340–428`). The display and release clocks are separate: a fully opened door takes 0.375 seconds, while successive release opportunities are 30/72 seconds apart. The release clock phase continues even when there is no current target. The source door sound is triggered at the opening/closing transition. The authored animation sheet has only 15 cells; display sampling must never read a nonexistent sixteenth cell when simulation phase is 15.

An ordinary dispatched creature remains counted in its Bunker's garrison. Its dispatch count prevents it being spawned twice. It uses the owner's current researched monster statistics and begins at full health. A living defender searches within 200 logical pixels, can keep pursuing an existing living target outside the trigger radius, then falls back to its Bunker's targets. If no target remains and the Bunker survives, it returns home (`CreepBase.findDefenseTargets:660–698`). Defense and return motion use the original 1.5× movement multiplier (`CreepBase.move:1460–1464`).

The 200-pixel search is a reacquisition rule, not a command to switch opponents every simulation step. A defender retains a living opponent while attacking, searches immediately when that opponent dies, and periodically searches while moving on the source's 150-display-frame boundary (3.75 seconds at 40 Hz; `CreepBase.tickBDefend:1049–1077`). Returning creatures can reacquire before reaching home (`tickBBunker:1427–1429`).

Trigger range, local acquisition, nearby retaliation and defensive blast selection use the logical/cartesian distance obtained by `PATHING.FromISO`; they are not distances measured directly on the rendered isometric image (`Targeting.getCreepsInRange:192–246`, `PATHING.as:677–680`). Conversely, ordinary defender contact uses projected distance below 30 pixels or an eligible ranged shot (`CreepBase.move:1597–1619`). Ranged shots also check line of sight.

Ordinary death removes the creature from both garrison and dispatch counts once. Returning alive reduces only the dispatch count, leaving that stored creature available for a later release (`CreepBase.tickBDefend:996–1045`, `tickBBunker:1399–1444`). The original ordinary branch stores counts rather than individual injury, so a later spawn is fresh; Map Room 3's persistent injury must not be added implicitly.

## Retaliation and Eye-Ra

Defenders fight invading monsters. When a defender makes contact or attacks, eligible attacking monsters switch to that defender. The source also alerts up to five nearby ground attackers within 50 logical pixels after a non-explosive hit; exploding creatures retain their own attack behavior (`CreepBase.as:1106–1120,1177–1217,1597–1619`). Hunter target group 6 can proactively select defending/returning creatures and occupied Bunkers, with its original melee damage multiplier (`MonsterBase.as:943–981,1041–1068`; `CreepBase.as:1169–1173`). Empty Bunkers are not preferred defense targets for group 4, although they remain ordinary fallback buildings (`MonsterBase.as:1027–1037`).

The offensive and defensive Eye-Ra formulas differ:

| Context | Source formula |
| --- | --- |
| Defensive Eye-Ra attack or death | Ground attackers within 90 logical pixels; truncate distance, use linear falloff, and retain a 20% minimum. The attack consumes the defender once. |
| Offensive Eye-Ra against buildings | Existing 60-logical-pixel quadratic building blast remains in use. |
| Offensive Eye-Ra against its primary defender | Full damage only within 30 projected pixels; that primary target is excluded from the secondary loop. |
| Offensive Eye-Ra against other ground defenders | 90-projected-pixel quadratic falloff, integer damage, with no 20% minimum. |

References: `CreepBase.as:787–820,1034–1044,1122–1162`; `Targeting.DealLinearAEDamage:340–383`. Blast processing must avoid duplicating both damage and garrison removal when an attack has already killed its own Eye-Ra.

## Destruction, persistence and mode boundaries

Ordinary Bunker destruction removes all undispatched stock. Already dispatched defenders remain active in the battle; the source temporarily keeps only those dispatched counts (`BUILDING22.Destroyed:489–507`). A destroyed Bunker exports no garrison (`Export:683–704`). This prevents a later repair or reload from recreating the monsters that were lost with the building. Living defenders and dispatch state in an ongoing host battle require explicit native save state so a mid-battle reload cannot duplicate a release or casualty.

Map Room 3 instead links individual `CreepInfo` records, applies a health threshold for release, and halves stored creature health on Bunker destruction. That mode, its ordinary-capacity differences, recovery, premium purchases, Juicer rewards, Inferno, Monster Lab anti-air powers and siege decoys remain distinct systems. This update must not claim those systems from the existence of the ordinary Bunker.

## Native implementation and migration

Select a completed, healthy Bunker in your own yard to open its native two-column management screen. The Housing column shows ready monsters and a mixed-species transfer selection; the Bunker column shows stored monsters. Capacity and Putty cost update from current researched stats. A submitted selection is checked atomically by the host: stale counts, insufficient Putty, unavailable species, a busy yard or insufficient capacity reject the entire selection without a partial debit. Permanent dismissal has a confirmation and expected-count check. The building-details action retains upgrade and repair access.

Garrisons belong to the host's building state. They are separate from the ready attacking army, cosmetic yard residents and invasion troop count. Private stored rosters and management fields are sent only to their owner. Opponents see released defenders and the public door state. Rival bots manage Bunkers through the same host command validation.

Simulation schema 5 saves garrison counts, released and returned defenders, one-time death handling, door/release progress, cached target identities and target-refresh countdowns. Schemas 1–4 remain readable, with empty Bunker stock when no garrison was previously saved. Destroyed Bunkers retain no persistent stock; already released survivors can finish the current battle. Pruning returned records also clears references held by attackers. Reload validation rejects impossible counts, duplicate identities, unsupported species, invalid timers and dangling references rather than manufacturing replacements. Account records continue using the existing host-only Unreal `.sav`; this change does not replace the existing world/bot snapshot format.

The port retains several explicit adaptations. Transfers commit immediately; the original cosmetic walk from Housing into the Bunker is not yet reproduced. Species and selection among the nearest three threats use deterministic ordering instead of Flash Dictionary iteration and random choice. Source timing intervals run on the host's fixed simulation clock instead of Flash's global frame phase. Ground travel uses the existing native grid pathfinder with original projection, doorway offsets, contact and range rules. These are not claims of complete Flash behavior or pixel parity.

## Validation

Three source-data tests verify the eleven exact references, runtime capacity override and whitelist/batch-price branches. Three native helper tests cover species, source capacities, transfer rounding/overflow, defensive/offensive Eye-Ra differences and the separate door/release clocks.

The actual `BYMBunkerRules.cpp` and those three native helper test bodies passed **590,178 assertions across 100,000 randomized cases** in a standalone C++17 harness with minimal Unreal type adapters. Transfer arithmetic is compared against independent 128-bit integer calculations. Blast comparisons use independent long-double formulas with a one-integer allowance for the original double-precision truncation order. Compiler flags include `-Wall -Wextra -Wshadow -Werror`; only the engine test method's unused parameter warning is suppressed.

This is isolated algorithm evidence. It does not compile Unreal, execute host commands or save serialization, or establish rendered UI/combat correctness. Final integrated validation and remaining engine gates are recorded in `VALIDATION.md` and `ALGORITHM_CHECKS.json`.

The complete current host simulation and ten Bunker authority/combat test bodies also pass in the portable regression runner, together with the existing gameplay suites: **77 native runtime test bodies, 71,632 assertions, zero failures**. This exercises actual command and save/load code with JSON and engine adapters; it is not native Unreal serialization or engine execution. The reproducible runner, exact input hashes, test log and exclusions are under `Tools/OfflineValidation`.

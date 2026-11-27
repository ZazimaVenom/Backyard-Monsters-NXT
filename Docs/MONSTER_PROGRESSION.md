# Monster Locker, Academy and level-dependent training

Updated for source preview 0.1.8. This is a source implementation; Unreal compilation, rendered UI and multiplayer execution have not been verified in this workspace.

## Original data and rules

The catalog now contains 162 per-level stat rows and 134 research transitions across the 28 preserved source creatures. `CREATURELOCKER.as` supplies unlock prices/timers, required Locker levels, research prices/timers and per-level arrays. `CREATURES.GetProperty` clamps a short array to its last value independently for each property. The data preparer preserves that behavior and the original raw arrays. Seven additional mechanics/UI ActionScript files are retained under `Content/Data/Original`.

| System | Implemented behavior |
| --- | --- |
| Starting unlock | New player and rival yards start with Pokey (C1) unlocked at level one. |
| Monster Locker, building 8 | Original Putty cost and required completed Locker level; one unlock job at a time. Completion grants the species once. |
| Monster Academy, building 26 | Original construction prerequisites, quantities and upgrades. Each Academy has one research slot; the same species cannot be researched twice concurrently. The original quantity table permits a second Academy at Town Hall level 5. |
| Research gate | To research level N+1, own an unlocked level-N monster and a free research slot in a completed Academy at least level N. The requirement uses its attained level, not a pending upgrade's target level. |
| Research completion | Updates that species' current level and existing home army's stats and housing. Future hatching uses the newly researched price/time. Deployed troops retain their deployment level. |
| Cancellation | Refunds the Putty actually paid, limited to available storage space. Duplicate cancellation is rejected. Existing over-cap wallet balances are not reduced. |
| Building interaction | Completed Lockers and Academies retain their attained levels while damaged, repairing or upgrading. Their separate source entry points allow unlock/research to start and continue in those states. In the reverse direction, an active unlock blocks starting a Locker upgrade, and active research blocks starting that Academy's upgrade. Stable building identities survive a permitted move or repair. |
| Authoritative state | Clients request operations; the host chooses the Academy, checks prerequisites, charges resources, sets completion timestamps and calculates troop stats. |
| Rival behavior | Uses the same unlock/research/train commands and costs. No free monster levels or unlocks are granted to bots. |
| Time settings | `MonsterProgressionTimeMultiplier` applies to unlock/research; `TrainingTimeMultiplier` remains the hatch-time setting. Both default to 1. |

For example, Pokey level 1 has 200 HP, 60 damage, a 250-Goo cost, 15-second hatch time and 10 housing. Level 2 research costs 4,000 Putty and takes 7,200 seconds in an Academy of level 1 or higher. Level 2 has 220 HP, 65 damage and a 450-Goo, 10-second hatch time. At level 6, Pokey has 300 HP, 85 damage, a 1,250-Goo cost, 5-second hatch time and 7 housing.

Octo-Ooze's source hatch array is `[15, 16]`; its later levels retain 16 seconds. This unusual value is preserved rather than replaced with an assumed decreasing timer. Healer damage values remain negative in the catalog; importing their levels does not implement healer combat.

## Native interface

The Monsters menu contains Hatchery, Locker and Academy tabs. The Hatchery uses the source's nine-column icon arrangement. The Locker retains source page ordering; the Academy compares current and next stats. Original source portraits, icons, fonts and frame artwork are used. Costs, counts, requirements, research availability and countdowns read current host snapshots rather than cached level-one UI values. A pending command waits for the host's response before the next action.

Clicking a Hatchery or Housing structure opens training, a Locker opens unlocking, and an Academy opens research. The Academy UI reports both active jobs when two buildings are working. Selecting research automatically uses an eligible free Academy; the specific Academy identity is included in host state. Exact Flash layout and in-engine rendering remain comparison gates.

The source's ordinary building-information controls require full health for operations such as starting an upgrade. That gate does not govern the separate Locker/Academy entry points. Damage and repair pause construction/upgrade work, but do not pause the independent monster unlock/research countdown. While a building upgrade is pending, its visible activity loop holds even if that research countdown continues; damaged artwork also remains separate from logical research progress. See `BUILDING_LIFECYCLE.md` and `BUILDING_ANIMATION.md`.

## Existing world preservation

The simulation subdocument now writes schema 5 and accepts schemas 1, 2, 3 and 4. Schema 3 introduced the independent Hatchery groups described in `HATCHERY_PRODUCTION.md`; schema 4 retains them and adds independent building work identities/countdowns and player-experience counters. Schema 5 additionally preserves ordinary Bunker garrisons, released defenders and door/release state. Its surrounding host world JSON files, account `.sav` serialization and save cadence are unchanged. Schema-1 migration infers level-one access for species already present in armies, training queues or active battles; it does not discard these inventories. Saves preserve monster levels, unlock/research jobs and deployed troop levels. Old saves are not rewritten merely by opening the editor.

New progression fields are validated on load: valid species and levels, paid costs, timestamps, ownership and unique species/Academy jobs. Invalid state fails loading instead of silently granting progress. The normal hourly/graceful-shutdown save lifecycle records migrated state after hosting.

Player experience is separate from researched monster levels and RP. Unlocking, research and hatching award no direct player experience in the supplied source. The schema-4 counters and source level display are documented in `PLAYER_EXPERIENCE.md`.

Ordinary Bunker assignments and combat are implemented separately in `MONSTER_BUNKER.md`; they use researched housing and Goo costs without changing the underlying monster unlock/research tables.

## Remaining source differences

- The normal direct-combat roster still excludes healers C15/C16. Original event-locked C17/C19 and dependent offspring C18 are not ordinary Locker purchases; existing inventories remain usable where the prior generic combat supported them. Event unlock rewards and splitting behavior are separate missing systems.
- Independent per-building queues, damage/upgrade pauses, housing-blocked output, one-creature cancellation and destruction refunds are implemented. Hatchery Control Center, premium modifiers, Inferno and full tutorial flows remain outside this update; see `HATCHERY_PRODUCTION.md`.
- Special monster abilities, original projectile/attack timing, Map Room 3 persistent garrisons, healing/juicing and Inferno rules are not implemented by these progression tables. The later v0.2.0 map update applies MR3's active threefold hatch price/time to new orders after entry, preserving already-paid queues; see `MAP_ROOM.md`.
- Deployed troops keep their captured level for deterministic battles. The original client updates active creep levels on research completion; matching that interaction fully remains a combat-parity task.
- Starting yards/resources and the requested RP ladder, bots and one-hour shields retain this project's explicit rules. They are not relabeled as original mechanics.

## Verification

The offline asset suite compares every normalized level's core stats and each research transition with the preserved original catalog, including array clamping and blocked/fake flags. Native `BackyardMMO.Progression` tests exercise host command rejection, costs, cancellation, timed completion, research stats, migration and rival rules. They are included for the target UE editor and have not been executed here. The complete validation record distinguishes offline checks from engine tests.

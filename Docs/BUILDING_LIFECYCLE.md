# Building lifecycle and paid work

This update separates construction/upgrade work from repair. A damaged building keeps its attained level, target level, paid job identity and remaining work. Repair can run on that same building. Construction and upgrading resume only after health is full and repair has finished.

The lifecycle fields introduced in schema 4 and retained in schema 5 are stored inside the existing host-local JSON world snapshots used for the world and bots. Account storage remains in the existing native Unreal `.sav` files. This update preserves those separate storage formats and does not add an external account database, account service or credential dependency.

## Original rules and references

References below are to the supplied `backyard-monsters-refitted/client/scripts` source, not inferred from a screenshot.

| Behavior | Original reference | Native implementation |
| --- | --- | --- |
| Repair and paid work are independent states | `BFOUNDATION.as:1360` Tick, `:2970` Export | `repairing`/health and `workRemaining`/job identity persist independently. |
| Any damage pauses construction/upgrading | `BFOUNDATION.as:1368–1395` | Work consumes time only at exactly full health, outside repair. |
| Lethal damage ends an active repair before destruction handling | `BFOUNDATION.as:499–518` modifyHealth | Damage clears repair before applying an upgrade setback. |
| Destroyed level-zero construction keeps its countdown | `BFOUNDATION.as:1966–1984` | No automatic level promotion, cancellation or refund. |
| Destroyed upgrade loses half its completed progress, at most eight hours | `BFOUNDATION.as:1985–2005` | Duration, half-progress and resulting countdown follow the original integer truncation order. The job survives. |
| Repair is free and restores attained-level health | `BFOUNDATION.as:2019–2034` | Repair does not charge building costs again or change the target level. |
| Upgrade cancellation refunds the next level's full resource cost | `BFOUNDATION.as:2412–2434`, `:2670–2703` | Current wallet capacity limits the credited amount. Progress is discarded; the attained building remains. |
| Construction cancellation refunds the full first-level resource cost | `BFOUNDATION.as:2627–2646` | The unfinished building is removed after an owned, current job is confirmed. |
| Prerequisites and quantity limits use completed levels | `BASE.as:3705–3789`, `:3903–3915` | Damage, repair and a pending upgrade do not erase previously attained prerequisite levels. Level-zero construction grants none. |
| Housing uses attained capacity while upgrading/repairing, above ten health | `HOUSING.as:55–85` | Completed Housing contributes capacity when HP is greater than ten. Existing army retention is preserved. |
| A Locker cannot begin a building upgrade while unlocking a monster | `BUILDING8.as:97–103` | The host reports the active unlock as the blocking reason. |
| The Town Hall cannot be removed | `BUILDING14.as:41–51` | Construction cancellation cannot remove it. |

The existing single construction worker remains in use. Paused construction or upgrading still occupies that worker. Starting a building upgrade, moving a building and cancelling its work retain the ordinary building-information full-health controls. Monster Locker and Academy access have separate source entry points; their access rules are documented with monster progression rather than generalized from building controls.

Housing capacity still uses all ten catalog levels already supported by NXT. The supplied source distinguishes Map Room modes: `BFOUNDATION.as:831–859` caps Housing at level six and Flinger at level four outside Map Room 3, including their maximum upgradeable levels; `HOUSING.as:67–69` also allows destroyed Housing to contribute capacity in Map Room 3. The later v0.2.0 update implements MR3 territory participation, but retains the existing count-based Housing/Bunker army model; see `MAP_ROOM.md`. Applying only one of those restrictions would change existing capacity without the corresponding progression and army rules, so mode-specific capacity parity remains deferred and is not claimed by this update.

The timestamp simulation deliberately splits elapsed time at repair and work-completion boundaries. Time after repair reaches full health can advance work in the same host update. The Flash implementation selected one branch per integer Tick call; splitting the interval is the native adaptation that makes results independent of server update frequency. Time during repair is never credited retroactively to work. The existing one-year bounded accrual history remains unchanged.

## Save migration and authority

Simulation schemas 4 and 5 store `jobId` and `workRemaining` for every building. Pending work has a nonempty identity and a target exactly one level above the attained level. An idle building has an empty identity, zero remaining work and matching target/attained levels. A pending job may have zero remaining work while damaged; it cannot complete until full health is restored. Values and identities are validated before the world is accepted.

Schemas 1, 2 and 3 remain readable. Their absolute `readyAt` job deadline is converted once into remaining work at the same bounded `lastEconomy` accrual origin used by the simulation. This avoids subtracting offline time twice and allows health to decide whether that time advances the job. A stable identity is then assigned and saved in the current schema. Schema-4 work identities/countdowns are retained unchanged during schema-5 loading. Old repair-only records stay repair-only: migration cannot reconstruct an upgrade that an older version had already discarded.

`readyAt` remains a compatibility/display field, derived from the independent states. It is not the authority for new construction, upgrading or repair. Job identity prevents a delayed confirmation from cancelling a replacement job, and repeating a completed cancellation cannot repeat a refund. Cancellation rechecks ownership, current state, battle restrictions and wallet capacity on the host.

A migrated paid Hatchery queue must be cancelled through its existing refund flow before removing unfinished Hatchery construction. This preserves paid entries instead of orphaning them. Generic completed-building recycling remains deferred until its original Housing, Locker, Academy and other building-specific side effects are implemented. The two work-cancellation commands are not a general sell command.

## Native UI contract

The building details panel presents construction/upgrade time and repair time separately. A paused work countdown is held at its authoritative remaining value; the HUD does not count it down while damaged or repairing. A running work deadline is shown only when the host reports it as eligible.

| Snapshot field | Meaning |
| --- | --- |
| `jobId`, `jobKind` | Owner-visible current identity and `construction`, `upgrade` or `none`. |
| `workRemaining`, `workReadyAt` | Remaining paid work and an eligible running deadline; zero deadline while paused. |
| `workPausedReason` | Empty, `damaged` or `repairing`. |
| `repairRemaining`, `repairReadyAt` | Independent repair time and deadline. |
| `canRepair`, `repairReason` | Host eligibility and explanation. |
| `canCancelConstruction`, `canCancelUpgrade`, `cancelReason` | Host cancellation eligibility. |
| `cancelRefund` | Current wallet-capped amount by resource, not an uncapped promise. |
| `canMove`, `moveReason` | Host movement eligibility and explanation. |

Native confirmation captures the building and job identity, displays the refund and loss of progress, and sends `cancelConstruction` or `cancelUpgrade` with `{building, job}`. A changed job invalidates that confirmation. The details panel scrolls when controls exceed the available height, and removed construction clears the selection.

## Validation and remaining engine checks

`BYMBuildingLifecycleTests.cpp` adds native automation coverage for damage/repair/work boundaries, exact save reload behavior, cancellation ownership and stale identities, refund caps, attained prerequisites, Housing's ten-health boundary, schemas 1–3 migration, invalid schema-4 states, and real invasion/deployment lethal damage preserving construction and upgrade jobs. Its pure-rule case also checks integer setback rounding, the eight-hour cap and migration clock boundaries.

An offline C++ harness compiled the actual extracted `Advance`, Hatchery methods and collector adapters together with the real `BYMEconomy.cpp` and `BYMBuildingLifecycle.cpp`. The lifecycle pass completed 60,017 assertions, including 15,000 randomized interval-partition cases covering repair, production, hatching and building work. This is algorithm evidence, not an Unreal build or a run of the native authority tests. A separate merged lifecycle/XP harness passed 100,049 assertions across 25,000 randomized cases using the current methods. Current combat damage/destruction methods also passed 137 offline assertions, including repair interruption, retained construction, upgrade setbacks and duplicate-refund prevention. The latter harness substitutes Advance; neither harness runs the real JSON loader or Unreal engine.

Unreal Engine is unavailable in the build environment used for this change. Native Unreal compilation, editor automation and visual confirmation of the details/cancellation panels remain unrun. Open the normal `.uproject` and run the `BackyardMMO.BuildingLifecycle` automation group along with the existing simulation/economy/hatchery/combat groups when the engine is available.

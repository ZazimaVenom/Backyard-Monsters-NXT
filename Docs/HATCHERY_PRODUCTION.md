# Original per-Hatchery production

The independent Hatchery incubators and waiting groups introduced in source preview 0.1.6 are retained in 0.1.8. The host owns queue admission, production, housing release, cancellation and refunds. The existing host-local account and world save files and hourly save cadence are unchanged. Native account `.sav` records and the existing world/bot JSON format are preserved; the nested simulation now uses schema 5, retaining the prior independent lifecycle and experience fields.

## Source rules implemented

| Behavior | Supplied source | Native implementation |
| --- | --- | --- |
| Independent queues | `HatcheryBase.as:18`, `BUILDING13.as:219–235`, `BUILDING13.as:477–509` | Each stable Hatchery building ID owns one active creature and its own ordered waiting groups. Multiple Hatcheries produce in parallel. |
| Waiting capacity | `HATCHERYPOPUP.as:241–285` | `1 + Hatchery level` waiting groups; each group holds at most 20. The active creature is separate. Orders merge into the first same-species group with space. |
| Housing | `BUILDING13.as:326–355`, `HOUSING.as:92–109` | Ordering does not reserve housing. A completed creature remains in its incubator until enough housing is available, then the next creature starts. Waiting time is not credited to the next creature. |
| Attained Housing capacity | `HOUSING.as:55–85` | A completed Housing building retains its attained-level capacity during repair or upgrade when health is greater than ten. Existing monsters are retained when capacity falls; releasing newly hatched monsters still requires available room. Map Room-mode differences remain deferred as described below. |
| Damage and upgrades | `BUILDING13.as:250–259,275–355` | Incubation pauses below half maximum health, at health 10 or less, and during construction/upgrades. Existing queues continue while repairs are above the operational threshold. |
| New-order access | `BUILDINGINFO.as:98–144,192–197,491–492` | The original building menu exposes Hatchery access only after construction/upgrade and at full health. New orders use that gate; the native screen can still display paused queues and cancellation controls. |
| Queue cancellation | `HATCHERYPOPUP.as:300–323` | Cancel one active or waiting creature. The host returns its **current researched Goo price**, capped by available storage, matching `CREATURES.GetProperty` and `BASE.Fund`. Cancelling the active creature starts the next eligible one. |
| Destruction refund | `BUILDING13.as:67–104` | Clear that Hatchery's active and waiting creatures and return `ceil(0.75 × current total Goo value)`, capped by storage. Cleared entries cannot be refunded twice. |
| Research interaction | `BUILDING13.as:353` | A newly started creature takes the current researched hatch time; an already running countdown keeps its remaining time. Refund values use the current researched price as the original source does. |
| Offline time | `BUILDING13.as:238–259`, `BASE.as:1870–1928` | Events are advanced chronologically through construction, upgrade, repair and research boundaries. All operational Hatcheries compete for housing at their actual completion times. |

The original popup has no waiting-group reorder operation. This port includes its one-creature cancellation behavior rather than inventing a reorder rule. Batch quantity entry remains a native-interface convenience; every requested creature is validated and charged by the host.

Pending building work is identified independently of its display deadline. Damage/repair can pause an upgrade and set its running deadline to zero without allowing that Hatchery to incubate. Repair restores health while preserving its paid building job; incubation resumes only after the construction/upgrade itself completes. Monster research has a separate source access rule and is not subject to this Hatchery production gate.

## Native screen and command contract

`home.hatcheries` identifies each building and reports its level, waiting-group capacity, active creature, grouped queue, remaining time, blocked/paused status, and per-species admission options. Housing-used counts include ready monsters only; queued housing is informational. The old `training` array remains a per-monster compatibility projection, not the authoritative grouped save state used by schemas 3–5.

`train` includes a selected `building`, `type` and `count`. If an older client or rival omits the building, the host chooses an eligible Hatchery with the shortest owned queue. `cancelTraining` includes the building, queue-entry ID and displayed `expectedCount`. A stale count or missing identity is rejected before any refund. Existing rival identities and their progression policy remain; they use the same authoritative commands.

## Existing world migration

Simulation schema 5 retains each Hatchery's active countdown and ordered groups introduced in schema 3, along with schema 4's independent building work and player-experience state. Schemas 1, 2, 3 and 4 remain readable. Schema-1/2 global paid queues are attached to the first existing Hatchery, retaining the original intervals between completion deadlines. An old multi-creature batch with one completion timestamp remains a simultaneous legacy batch. Every creature is retained; an oversized legacy queue is allowed to drain instead of being truncated. New admissions follow the original waiting-group capacity. Legacy deadlines use the same bounded accrual origin as the host's one-year catch-up window so old paid work is not delayed by subtracting elapsed time twice.

The loader validates ownership, unique Hatchery/entry IDs, species access, active count exactly one, group counts, finite nonnegative remaining times and queue limits. Malformed state is rejected without overwriting the original file. Account serialization is not changed and no new external persistence service is introduced.

Purchasing training, hatching and monster research grant no direct player-experience award. The separate schema-4 player-experience counters do not change queue prices, housing or researched monster levels; see `PLAYER_EXPERIENCE.md`.

## Validation and limits

The actual queue/advance/refund C++ methods are exercised in an offline harness using small Unreal type substitutes. Bulk elapsed-time advances are compared with individual one-second updates over 10,000 randomized cases, alongside parallel production, blocked housing, damage/upgrade pauses, healthy repair, queue capacity invariants, source refund rounding and grandfathered batches. This is algorithm execution, not an Unreal build.

Five native `BackyardMMO.Hatchery` tests are included for group capacity/parallel production/cancellation, housing/restart/upgrade timing, legacy migration/invalid saves, paid legacy deadlines older than the one-year catch-up horizon, and destruction/refund through an actual simulation invasion. These tests have not been executed in Unreal in this workspace.

Map Room modes affect original Housing capacity: `BFOUNDATION.as:831–859` caps Housing at level six and Flinger at level four outside Map Room 3, while `HOUSING.as:67–69` also permits destroyed Housing capacity in Map Room 3. The later v0.2.0 update implements MR3 territory participation, while retaining existing Housing levels 1–10 and count-based army inventories; see `MAP_ROOM.md`. This update does not apply an isolated level-six cap or remove existing monsters; complete mode-specific parity remains deferred. See `BUILDING_LIFECYCLE.md`.

Hatchery Control Center, premium Finish Now/overdrive, Map Room terrain hatching boosts, Inferno incubators and full original training tutorial are separate remaining systems. They are not represented as completed by these queue mechanics.

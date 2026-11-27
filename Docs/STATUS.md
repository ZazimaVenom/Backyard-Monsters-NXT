# Implementation status — 27 November 2026

**Windows packaging correction 0.2.0.2:** the atlas readiness check no longer references an editor-only texture API in game targets. Editor placeholder hiding/retry and all other sprite layout logic remain unchanged. Both supplied packaging logs identify this one blocking error; their initial editor build reports success. Focused game/editor compile-contract checks are recorded in `PACKAGING_FIX_0_2_0_2.md`. Full native packaging has not been run here.

**Compile fix 0.2.0.1:** corrected all 24 compiler errors reported in the supplied UE 5.8.4 log: ten missing math declarations, twelve JSON key conversions and two Slate API calls. The offline adapter now models the JSON shared-key conversion constraint, and focused compiler checks cover the reported API mistakes. No gameplay, save format, asset or UI layout changes. A full Unreal rebuild remains unrun here. See `COMPILE_FIX_0_2_0_1.md`.

**Source update 0.2.0:** original Map Room 3 world and territory loop, native visual map, source authored sites and special core defenses, host-owned captures and benefits, shared player/bot rules, varied bot layouts and displayed-yard resident isolation. See `MAP_ROOM.md` for precise source scope and intentional host adaptations. The preceding 0.1.9.1 build was user-tested successfully; this update has not been built or rendered in Unreal here.

**Compile fix 0.1.9.1:** corrected the ambiguous numeric `TestEqual` call in the worker-material editor test. No gameplay, assets or UI behavior changed. The attached UE 5.8.4 log confirms UHT and runtime/game linking succeeded before this editor-test error. The corrected assertion passes a focused compiler reproduction; full Unreal rebuild remains unrun here.

**Source update 0.1.9:** workers with original directional artwork and travel, Shiny hiring, mushroom picking and regrowth, the General Store, normal source-priced decorations, packing purchases and six yard expansions. A visible white outline follows the authoritative buildable yard. The previous shared UI fix is retained. See `WORKERS_AND_SHINY.md` for source evidence and explicit adaptations. Unreal compilation, rendering and multiplayer execution remain unrun here.

**UI fix 0.1.8.1:** corrected shared image sizing, text wrapping and login containment. All v0.1.8 gameplay and source assets remain included. Native Slate layout regression tests are included; target-engine compilation and visual execution remain unrun here. See `UI_LAYOUT_FIX.md`.

**Source update 0.1.8:** ordinary count-based Monster Bunkers, native Housing-to-garrison transfers, defensive units and retaliation, source door animation/sound, rival management and schema-5 persistence. The implementation follows the non-Map-Room-3 source branch and preserves its runtime capacity override. Exact scope and adaptations are recorded in `MONSTER_BUNKER.md`. Unreal compilation, rendering and multiplayer execution of this update remain unrun.

**Source update 0.1.7:** independent building work and repair, source destruction setbacks, cancellation/refunds, attained-level prerequisites, original Locker/Academy shortcut eligibility, and player experience with the original level-star/progress bar. Schema 4 preserves remaining work and XP; schemas 1–3 remain readable. Offline evidence and native test coverage are recorded in `VALIDATION.md`; Unreal compilation, rendering and network execution of this update remain unrun.

**Source update 0.1.6:** original activity animation across 62 building levels; independent Hatchery queues, housing-held output, cancellation and destruction refunds; health-dependent discrete production, partial banking and gradual repairs; Eye-Ra's radial suicide blast, strict target preferences and damaged-tower scaling. Offline checks pass; the new Unreal build, rendering and multiplayer tests remain unrun.

**Sprite fix 0.1.5:** original image dimensions now determine frame sampling; pending textures retry instead of caching an invalid layout. Frame edges are inset, padded sheets retain their original spacing, and monster facing respects the source direction count. Original image bytes and host simulation/save behavior are unchanged. Target-engine rendering of this fix remains unverified here.

**Compile fix 0.1.4:** corrected the nested local-variable shadowing reported by UE 5.8.4. The user confirmed that build works and invasions improved; the remaining reported defect was corruption of some wandering sprites.

**Milestone:** source preview 0.1.3 adds up to 50 varied yard residents, source collision bounds, discrete combat events, source-derived animations/effects and a clearer deployment tool, building on the Locker/Academy progression update. The overall request for a 100% accurate, polished and verified MMO is **not complete**.

## Implemented in source; Unreal validation still required

| Area | Current implementation |
| --- | --- |
| Plugin/project | Runtime/editor C++ modules, UE 5.8 host project, build/setup/package scripts. |
| Editor automation | Fixed SourceAssets importer, deterministic plugin paths, incremental cache, automatic bundled-source revision detection on normal project opening, toolbar, commandlet, mainmenu/world map generation and config; final Default/sRGB pass checks cached textures too. |
| Assets | All original image/audio/font hashes preserved; original images plus derived layer composites, sprite descriptors, source data and WAV audio. |
| Native UI | Original resource plates, buttons, fonts, popup frames and construction cards; separate Hatchery/Locker/Academy flows with portraits, live prerequisites/timers and current/next stats; invasion countdown and results. Exact engine visual parity remains unverified. |
| Presentation | Straight-down orthographic camera, flat XY planes with original isometric projection, building previews, roof picking and translucent shadows; original grass swatches blended using MAPBG parameters and repeated at source-pixel scale, custom depth, native HUD, source music. |
| Mainmenu | Native Slate host/join, login/registration, connection status through the native Unreal client/server connection. |
| Accounts | Host-only native `.sav` records, normal owned-controller login/registration RPCs, asynchronous password verification and duplicate session control. |
| Persistence | Exclusive host lock, hourly saves, logout retention, registration commit/rollback, backup and corrupt-save refusal. |
| Economy | Four overworld resources, source-sized production batches and damage penalties, preserved countdowns, partial banking, completed-Silo storage, gradual free repairs, construction/upgrade/move, footprint and prerequisite checks. |
| Building lifecycle | Independent paid work and repair; damage pauses work, destruction retains construction and applies source upgrade setbacks; job-specific cancellation, capped refunds, attained-level prerequisites and schema-4/5 persistence. |
| Workers and Shiny | One free worker, four escalating Shiny hires, nearest-idle assignment and paid-task travel; repairs remain independent. Host-owned balances, worker assignments and mushroom rewards persist in the existing world save. Legacy paid jobs are preserved. |
| General Store and decorations | Original building, operational health gate, extra workers, More Yardage and Improved packing skills; 73 ordinary unblocked decoration products at the authoritative source store prices, including original animation frames for 25 decorations. |
| Construction and upgrade speedups | Original SP1–SP4 eligibility and prices, selected-building shortcuts, stale quote/revision checks, once-only completion/XP and worker release. Repair and research speedups remain outside this implementation. |
| Mushrooms and yard expansion | Twenty initial mushrooms, original five variants, worker clearing and 0/3/8 Shiny distribution; bounded regrowth, six source-sized rectangular expansions and visible isometric yard border. |
| Player experience | Complete active-client level table; earned points plus retained completed-yard value, source completion/collection/loot formulas, original star and progress artwork, separate from RP. |
| Monsters | Source unlock costs/requirements, timed per-Academy research, level-dependent stats/hatch costs/times/housing, independent per-Hatchery active/waiting groups, housing-held output, cancellation/destruction refunds, supported deployment and bounded army sizes. |
| Monster Bunker | Source ordinary-mode capacities and fees; owned ready-army transfers, permanent dismissal, separate garrison/active-defender state, door release, movement, fighting, return, casualty/destruction handling and native two-column management. |
| Battles | Host-side pathfinding using original combat bounds, contact/ranged checks, strict preferred-target tiers, Eye-Ra radial suicide blast, health-scaled tower shots, discrete cooldowns, source-timed movement, bounded events/effects, looting, retreat, win/loss records and one-hour shields. |
| Yard residents | Up to 50 ready monsters selected for variety, stable identities, local wandering/pauses around structures, and cleanup on roster/layout/yard changes. Cosmetic residents are hidden during battles. |
| Rivals | 1,000 seeded persistent bots by default; basic collection/unlock/research/training/building/upgrade/repair/invasion policy through the player command validator. |
| World Map | Separate original MR3 hex map, source terrain/site placement, authored defensive yards, scouting, quoted travel costs, capture/recapture, resource income/storage, stronghold bonuses and same-owner fortification armor. See MAP_ROOM.md. |
| Ladder | All accounts and bots, page-based ranks, initial RP 0 and configurable winner/loser points. |
| Settings | Host/port/player cap, bot scheduling, saves, shields/RP, battle limits, economy modifiers, grid/camera and audio. |

The home construction subset is source IDs **1, 2, 3, 4, 5, 6, 8, 11, 12, 13, 14, 15, 17, 18, 20, 21, 22, 23, 25, 26** plus 73 shop-only decorations. Map sites load their original templates, including source cores **138, 139 and 140**, without making those cores purchasable home buildings. Mushroom ID 7 has its own growth and clearing lifecycle. Event/blocked decorations are retained in the source catalogue without being sold as ordinary shop products. Full source catalog preservation is not the same as all catalog systems being implemented. New yards start with C1 unlocked and unlock normal C2–C14 through the Locker. Source-blocked C17/C19 and dependent offspring C18 are excluded from normal Locker purchases; existing inventories from earlier saves remain usable by the existing generic combat model. Healer IDs C15/C16 and unique original abilities still need their behavior ports.

Monster research applies the original per-level health, damage, speed, hatch price/time and housing arrays. Every Academy has a distinct research slot; cancels refund Putty within storage capacity. Older worlds migrate in memory without discarding armies or queued/deployed monsters. Independent Hatcheries own active/waiting groups, pause with damage/upgrades, hold completed output for housing and refund on cancellation/destruction. New Hatchery orders require the source full-health gate; already paid hatching continues at or above half health. Completed Locker/Academy shortcut access has different source rules and remains available during damage, repair or building upgrades. Details and exact limitations are in `MONSTER_PROGRESSION.md` and `HATCHERY_PRODUCTION.md`.

The construction UI now shows the host's owned/allowed quantities and next Town Hall unlock, plus upgrade costs and rejection reasons even when unaffordable. Invading uses the visible five-minute default deadline without an additional 30-second timeout. Losing a deployed wave does not end a battle while legal reserves remain. Map Room construction opens the prerequisite route for Flinger upgrades; bots can construct it through the same command rules. Map Room 3 now has its own visual territory map; Rankings remains a separate RP screen.

## Substantial original-game work still required

- Full original combat parity, special unit abilities and targeting subtleties. Source clocks, original collision rectangles, projected movement/range and discrete attacks are now used, but the complete original combat system is not ported.
- Hatchery Control Center and special training modifiers; Monster Lab behavior, healers, champions, siege weapons and special defensive weapons. Ordinary Bunker garrisons are now included; Map Room 3 persistent creature health, Inferno garrisons, Lab anti-air powers and Juicer flows remain separate work. Completed-building recycling and Map Room mode-specific Housing/Flinger rules also remain incomplete; see `BUILDING_LIFECYCLE.md`.
- Inferno realm and its economy/units, MR1/MR2 historical mode transitions, per-yard terrain seeds and alternate yard terrain types. MR3 territories are included; social relocation and cross-server migration remain separate work.
- Remaining store boosts, resource top-ups, reward/inventory storage and subscriptions; the implemented host Shiny economy does not connect to real-money payments.
- Original tutorial and quests, achievements, tribes/wild-monster campaigns, alliances/social features, mail/gifts and seasonal events.
- Remaining original building animation/effect/audio cues, full UI/screen parity and visual polish in the engine. Supported activity/defense timelines and available monster rows are ported; source-static structures remain static, and direction-only monsters use added NXT motion. See `BUILDING_ANIMATION.md` and `COMBAT_AND_YARD.md`.
- Better rival strategies, diverse armies, a balanced progression curve, anti-collusion/RP farming controls and long-running world balancing.
- Full network security review and internet deployment testing of the standard Unreal connection. No custom transport encryption is included.
- Real UE 5.8 build/cook validation, two-computer host/join tests, forced-failure persistence tests, and performance/soak tests at the requested 100 online users.

The native `OpenWorld` level renders the selected authoritative yard; its separate native Map Room visualizes the shared territory world. The source project remains an incomplete recreation of all original game systems, not a shipping replacement for every mode.

## Next validation gate

Open `BackyardMonstersNXT.uproject` normally with UE 5.8, accept its module rebuild prompt, allow the plugin setup to finish, run the included engine automation, then test a packaged host and separate client. Resolve compile/runtime issues before expanding parity. The audit includes an explicit scenario checklist; use evidence from these runs to mark items complete instead of assuming that written C++ is working gameplay.

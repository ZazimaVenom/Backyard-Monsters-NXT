# Backyard Monsters NXT — Unreal Engine 5.8 (WIP BETA)

Backyard Monsters NXT brings the classic monster-building strategy game into Unreal Engine 5.8. Build your backyard, grow your monster army, raid rivals and expand your territory across a persistent world shared by players and computer-controlled opponents.

Using original Backyard Monsters artwork, animated sprites and source-based game mechanics, NXT preserves the familiar visual style while introducing a Host & Play multiplayer experience. Start your own server and play alongside friends using the same game package.

## Current features

Build your backyard: Construct, upgrade, move and repair buildings, manage resources and develop your defenses.
Train your monster army: Hatch monsters, unlock additional species through the Monster Locker and improve their abilities through Academy research.
Living yards: Up to 50 wandering monsters bring each backyard to life, displaying a variety of monsters from its owner’s trained army.
Animated action: Monster movement and attacks, building production animations, defensive effects and visible combat bring battles to life.
Backyard invasions: Scout opponents, deploy troops, break through defenses and raid resources. Damage-protection shields help main backyards recover between attacks.
Monster Bunkers: Assign trained monsters to defensive garrisons that emerge to fight invading armies.
Visual Map Room: Explore the hex world, scout backyards, bookmark locations and fight to capture resource outposts, strongholds and fortifications.
1,000 rival bots by default: Persistent opponents build, train, research, invade and participate in territorial competition, with varied backyard layouts.
Shared ranking ladder: Players and bots compete for RP, gaining or losing points through invasion results.
Workers and yard expansion: Workers handle construction and mushroom clearing. Expand your buildable space within a clearly marked yard boundary.
Shiny and the General Store: Gather Shiny from mushrooms and spend it on additional workers, yard expansions, decorations and supported construction speedups.
Persistent multiplayer: Host-owned accounts and progress, automatic saving, configurable server settings and a player limit set to 100 by default.
Classic presentation: Original terrain, building and monster artwork, familiar resource displays and native menus recreate the game’s distinctive look.
Currently in active development, with the core backyard, invasion, progression and territory systems already implemented and further original-game features still being recreated.

Currently in active development, with the core backyard, invasion, progression and territory systems already implemented and further original-game features still being recreated.

## Screenshots

<img width="3046" height="1734" alt="1- RaGEZONE" src="https://github.com/user-attachments/assets/34e4afd8-1c21-4e5a-82ba-f90c8bd5b571" />

<img width="3840" height="2160" alt="2- RaGEZONE" src="https://github.com/user-attachments/assets/43b9b1fc-6a67-440c-9a14-aafbd7e8fe9b" />

<img width="3051" height="1739" alt="3- RaGEZONE" src="https://github.com/user-attachments/assets/5614f6ed-f84c-4ca9-88cd-d00350d23897" />

<img width="3200" height="1854" alt="4- RaGEZONE" src="https://github.com/user-attachments/assets/afb263f6-26ca-4b79-ad2a-2b566da437a2" />

## Controls and gameplay

- **WASD / arrows:** pan. **Mouse wheel:** zoom. **Right or middle drag:** pan. **Home:** center the yard.
- **Left click:** select a structure or confirm placement. **Escape:** cancel the active placement/deploy tool.
- **Selected building:** collect accumulated resources, upgrade, repair, move, or cancel its current construction/upgrade with confirmation. Damaged work retains its countdown while repair restores health; the panel shows both states and the host-approved refund. Construction and upgrade speedups offer the original free finish at five minutes or less, one/two-hour reductions and quoted Finish Now action.
- **Buildings:** choose a supported structure, then place its complete footprint inside the yard. Costs, prerequisites, counts and construction times are checked by the host.
- **Workers:** one worker starts free. Timed construction, upgrades and mushroom picking reserve an available worker; repairs do not. Click a worker icon to focus the camera. Buy up to four additional workers in the General Store.
- **Mushrooms:** select one in your yard or its surrounding terrain and choose Pick. A worker travels to it and clears it; the host awards any Shiny once. Mushrooms grow back over time.
- **General Store:** construct the original building, then select it or use the Shiny control. Buy extra workers, More Yardage and Improved packing skills using the displayed Shiny price. Decorations are bought and placed through their own catalogue.
- **Yard border:** the white outline marks the usable yard. More Yardage expands it; terrain continues beyond it.
- **Monsters → Hatchery:** select a Hatchery and monster, then queue within its waiting-group capacity and available Goo. Each Hatchery works independently. Waiting orders do not reserve housing; a completed creature waits for space. The screen shows active/waiting groups, pause reasons and one-creature cancellation.
- **Monsters → Locker:** unlock new species using the original Putty cost, Locker requirement and timer. Pokey starts unlocked.
- **Monsters → Academy:** research unlocked species using Putty. Each completed Academy has its own research slot; the Academy level gates the next monster level. Inspect before/after stats and cancel active work for a refund within available storage.
- **Monster Bunker:** construct it from Buildings after reaching its source prerequisites, then select it in your own yard. Choose ready monsters in the Housing column and transfer the selection with Putty. They become defensive garrison units and leave your attacking army. Removing a stored monster permanently dismisses it; it does not return to Housing or refund resources.
- **Map Room:** after the original progression requirements, explore the visual hex world, scout backyards and defensive sites, bookmark destinations and confirm host-priced attacks. Capture resource outposts, strongholds and fortifications at 90% health-weighted damage. Inspect owned territory through its paged list.
- **Rankings:** browse the combined player/bot RP ladder. MR3 participants use the same map attack rules when attacking from a scouted yard. Deploy troops along the yard boundary; return home or retreat through the UI.

Pre-MR3 backyard battles retain the existing Town Hall/half-building objective. MR3 battles use the original 90% health-weighted damage objective. Main backyards never transfer ownership; territory types have their own capture or regeneration outcome. These additions do not complete every original monster ability or game mode; see `Docs/MAP_ROOM.md` and the source audit.

The level star and progress bar use the original active client's complete threshold table. Player points combine earned action experience with retained completed-yard value. Construction, upgrades, accepted collector transfers and credited invasion loot award their source-derived amounts. Player level is separate from Town Hall level, monster levels and the requested RP ladder.

## Default host rules

| Setting | Default |
| --- | --- |
| Online player limit, including the host | 100 |
| Persistent rival bots | 1,000 |
| Visible trained yard residents | Up to 50, varied across ready species |
| Starting Shiny | 1,500, matching the supplied source configuration |
| Workers | 1 free; up to 4 extra hires with Shiny |
| Base yard | 50 × 40 cells; 6 More Yardage purchases |
| Mushroom regrowth | One per 4 hours 48 minutes, up to 10 regrown mushrooms |
| Active battle snapshot interval | 0.2 seconds |
| Gameplay autosave | 3,600 seconds |
| Main-backyard post-invasion shield | 3,600 seconds; MR3 territories use their source no-shield rule |
| Starting ranking points | 0 |
| Winner RP / loser RP | +25 / −15 |
| Negative RP | Allowed; optional clamp setting |
| Battle time limit | 300 seconds |
| Concurrent battle limit | 32 |
| Troops per battle limit | 200 |
| Loot fraction on an invasion win | 10% |

Project Settings also exposes bot scheduling, resource starts/storage, yard size, camera width, construction/training time, monster unlock/research time, production, damage, movement, audio and request limits. `MonsterProgressionTimeMultiplier` defaults to 1, preserving the source unlock/research durations. Original item-specific progression is preserved in the local data catalog; not every original system is executable yet. Settings affect the host's rules; remote clients cannot replace them.

Bots are persistent simulated rivals, not 1,000 constantly spawned visible actors. Their yards are displayed when inspected or invaded. Bounded scheduling lets them collect, unlock/research monsters, train, build, upgrade, repair and attack through the same command validation as players. They currently use a basic policy with armies chosen from the supported monster types; this is not human-equivalent strategic AI. Lowering the active-rival setting preserves extra saved rivals as dormant yards rather than deleting progress.

## Host-only saves

Account records use Unreal's native `USaveGame` serialization in `Saved\SaveGames\BackyardMonstersNXT_Accounts.sav`, written only by the host. Passwords are stored as salted verifiers. Bot and world simulation retain their existing host-only `Saved\BackyardMMO` persistence. Clients receive sanitized views. Logout removes the session and leaves the yard in host memory. Hourly autosaves persist gameplay progress. Account creation commits immediately, and orderly host shutdown also saves by default.

This update retains simulation schema 5 and reads schemas 1–4, preserving existing armies, paid training and battles. An optional versioned world-map extension records home positions, participation, territory ownership, modified site state, bookmarks and resource timing; original world geometry remains in bundled content. Versioned economy and worker extensions retain Shiny, purchases, mushrooms and paid assignments. Older yards receive their initial Shiny and mushrooms once when the extension is first introduced. Existing simultaneous paid building jobs retain their workers, including temporary legacy assignments that retire when finished; no paid job is discarded to enforce the new worker limit. Bunker garrisons, active defenders and door/release progress are saved independently of attacking troops. Construction/upgrade work and repair remain independent. Worlds from before experience tracking derive their known completed-yard value and start earned experience at zero; unverifiable historical action rewards are not invented. Oversized legacy queues drain without truncation; their original batch deadlines are preserved. Existing owned, queued or deployed monster species retain level-one access during migration; new yards follow the original Pokey-only starting unlock. Unlocks, researched levels and active jobs are saved with the next world snapshot. See `Docs/MONSTER_PROGRESSION.md` for the implemented rules and remaining source differences.

A process crash before the next successful save can lose unsaved progress. A previous committed snapshot is retained for recovery; corrupt saves are refused instead of silently reset. See `Docs/SERVER_SECURITY_AND_SAVES.md` for file names and recovery.

## Flat 2D presentation

The grass terrain uses the original ordered, noise-blended texture recipe and repeats at source-pixel scale, with continuous ground beyond the yard. Viewport-aware pan and zoom limits keep the terrain border offscreen, including after window resizing. Original logical yard coordinates are projected into the source 2:1 isometric layout on horizontal XY planes under a straight-down orthographic camera. Pointer picking uses the inverse projection; placement shows the selected building and its diamond footprint. The terrain and sprite materials are unlit: image colors are emitted directly and do not require scene lights. The game camera uses fixed exposure without physical-camera darkening, and an explicit clip range covering the board. Building activity atlases retain original layers, pivots, cadence and pause behavior, with translucent soft shadows and explicit depth ordering; monster atlas columns select facing and only recognized animation rows are cycled. Direction-only species receive modest NXT attack motion from the original pose, timed by server attack events; no unavailable original frames are claimed. Invasions show source-derived tower frames, projectiles and hit/death presentation. Peaceful yards show a bounded, varied sample of ready monsters roaming around buildings. The imported art is stored under `/BackyardMMO/Art` and read from plugin content. Opening a generated map displays a transient terrain preview before Play; player buildings and monsters appear from the host snapshot during play. Fresh maps store a downward editor view centered over the terrain.

Unreal textures themselves have no **Render CustomDepth** property. Every generated board sprite/terrain primitive has `SetRenderCustomDepth(true)`, and setup enables the project's custom-depth stencil pass. This is the applicable Unreal implementation of the requested custom-depth setting.

## Validation commands

- `Run-Unreal-Tests.cmd`: builds, runs setup and executes the included native `BackyardMMO` automation tests. This must be run on the target engine; it has not been run here.
- `python Tools/validate_source.py`: offline project/catalog/asset checks; Python standard library only.
- `python Tools/test_asset_pipeline.py`: source artwork, atlas, provenance and data integrity tests; requires Pillow.
- `python Tools/test_experience_pipeline.py`: complete original XP table, compiled constants and exact source-reference hashes; Python standard library only.
- `python Tools/test_bunker_source.py`: ordinary Bunker runtime overrides, transfer rules and exact source-reference hashes; Python standard library only.
- `python Tools/test_ui_layout_assets.py`: original UI image dimensions, hashes, resource bars and manifest importability; Python standard library only.
- `python Tools/test_maproom_source.py`, `test_maproom_art.py`, `test_maproom_monsters.py` and `test_maproom_world.py`: original map rules, artwork, active monster values, deterministic world and provenance checks.
- `python Tools/OfflineValidation/run.py`: optional developer checks that compile the current simulation and execute native runtime test bodies through test-only adapters; requires Python and a C++17 compiler. This is not an Unreal build and is never required for normal project opening.

The offline source and asset tests are not substitutes for Unreal compilation, visual inspection, two-machine playtesting or a 100-player load test. Current evidence and remaining gates are recorded in `Docs/VALIDATION.md`.

## Use the plugin in another project

Copy `Plugins\BackyardMMO` intact into a **UE 5.8 C++ project**, regenerate/build its project files, then run the toolbar's **Setup All**. Setup assigns this plugin's game instance and default maps to make it the complete game flow. Use the included host project for the least integration work.

Original notices are preserved under `Plugins\BackyardMMO\Content\Data\Original`. See `Docs/ASSET_PIPELINE.md` and the original-source audit for provenance and the source archive's conflicting license labels.

# Backyard Monsters NXT — Unreal Engine 5.8 source preview

Current package **0.2.1.1** automatically caps rendering at **60 FPS** in the menu and gameplay, with low shadows, global illumination, reflections and post-processing, and medium effects. No Blueprint setup or console commands are needed. Project Settings > Plugins > Backyard Monsters NXT > Graphics exposes the cap and automatic-preset switch. Texture detail, resolution, Custom Depth, gameplay and the v0.2.1 bandwidth improvements are retained. See `Docs/GRAPHICS_DEFAULTS.md`.

This archive contains a C++ plugin, its bundled original assets, and a ready host project. It is an initial native recreation of the base-building and invasion loop. **It is not a finished 100% recreation, a compiled game, or a production-certified MMO.** Unreal Engine is not available in the development environment used to prepare this archive, so its C++ engine build, generated levels and multiplayer behavior must be verified on UE 5.8 before use.

Current package **0.2.1** substantially reduces repeated yard, Map Room and ranking data using lossless compression and updates containing only changed values. Large transfers wait for acknowledgement between chunks, and each player has a bounded queue with recovery from a lost baseline. Normal yard and combat update rates are retained. See `Docs/NETWORK_BANDWIDTH.md` for measurements, settings and validation limits. **Rebuild the host and every client from this same version** because the native RPC layout changed. Existing host saves keep their current formats.

The retained **0.2.0.2** update fixes the Windows game-target compilation failure during packaging. The sprite atlas readiness check now calls the editor-only `IsDefaultTexture()` API only under `WITH_EDITOR`; the existing editor retry behavior is retained. The full project, plugin and original assets are included. See `Docs/PACKAGING_FIX_0_2_0_2.md`. Open `BackyardMonstersNXT.uproject` normally and use Unreal Editor's Windows packaging controls.

The retained **0.2.0.1** update fixes the compile errors reported in `Log(10).txt`: explicit UE 5.8 JSON key conversions, the math header for the map tests, and the renderer/focus Slate API calls. Gameplay, UI layout and all original assets are retained. See `Docs/COMPILE_FIX_0_2_0_1.md` for the changes and validation limits. Open `BackyardMonstersNXT.uproject` normally.

The retained **0.2.0** update adds the original Map Room 3 world, native visual scouting and territory commands, source defensive layouts, resource outposts, strongholds and fortifications. Rivals use the same host map rules. Bot yard layouts vary by identity and residents are tied to the displayed yard's roster. See `Docs/MAP_ROOM.md` for source rules, explicit NXT adaptations and remaining differences. The user confirmed the preceding **0.1.9.1** build worked well after testing; this new update still requires its own UE 5.8 build and playtest.

The retained **0.1.9** update adds source-based workers, mushroom clearing and regrowth, host-owned Shiny, the General Store, decorations, and paid yard expansions with a visible boundary. See `Docs/WORKERS_AND_SHINY.md` for exact source rules, migration behaviour and remaining differences.

The retained **0.1.8.1** update corrects shared native UI sizing: original image dimensions are read from the source manifest, short labels do not auto-wrap, prose uses explicit widths, and the login form keeps actions within its frame. See `Docs/UI_LAYOUT_FIX.md` for the reported issue and validation limits.

The **0.1.8** update adds the ordinary overworld **Monster Bunker**: its original two-column transfer screen, Putty fees and capacity rules, a saved garrison, released defensive monsters, attacks and retaliation, original door frames and sound, and bot use through the same host commands. Released defenders have separate inventory and battle state from the attacking army. See `Docs/MONSTER_BUNKER.md` for original source references, the specific game-mode branch and remaining differences.

The **0.1.7** experience and building lifecycle changes remain included: original player experience and the native level-star/progress bar, independent construction/upgrade and repair states, retained work after damage, original upgrade destruction setbacks, and cancellation with host-checked refunds. Completed building levels remain valid for prerequisites during damage and upgrades. The original Locker/Academy shortcut access rules are preserved. See `Docs/PLAYER_EXPERIENCE.md` and `Docs/BUILDING_LIFECYCLE.md`.

The **0.1.6** activity animations and gameplay changes remain included: 28 atlases cover 62 building levels, production is discrete and health-dependent, repairs restore health gradually, and each Hatchery has its own active creature and waiting groups. Combat includes Eye-Ra's original radial suicide blast, strict preferred-target categories and health-scaled tower damage. See `Docs/BUILDING_ANIMATION.md`, `Docs/RESOURCE_PRODUCTION.md`, `Docs/HATCHERY_PRODUCTION.md` and `Docs/COMBAT_PARITY.md`.

The 0.1.5 wandering-sprite fix is retained: original-image dimensions, safe frame edges and retryable texture readiness prevent invalid atlas sampling. See `Docs/SPRITE_ATLAS_FIX.md`. All previously bundled original source assets are retained.

Source preview **0.1.3** adds varied roaming yard residents, closer combat approach, state-driven animation and effects, and a clearer deployment marker. See `Docs/COMBAT_AND_YARD.md` for the implemented behavior and source-frame limitations.

The original source was audited instead of treating texture import as a complete port. See `Docs/STATUS.md` for exactly what is implemented and still missing, and `Docs/ORIGINAL_GAME_AUDIT.md` for the source comparison and acceptance plan.

## First run on Windows x64

1. Extract the entire `BackyardMonstersNXT` folder to a writable folder, for example `C:\Games\BackyardMonstersNXT`. Keep `Plugins\BackyardMMO\SourceAssets` in place.
2. Install Unreal Engine **5.8** and its supported Visual Studio C++ toolchain/Windows SDK.
3. Open **`BackyardMonstersNXT.uproject`** normally and accept Unreal's module rebuild prompt if it appears.
4. Once the editor opens, the plugin automatically prepares missing assets, materials and maps from its bundled `SourceAssets` folder. Updated bundled asset/catalog revisions also trigger import automatically. Wait for setup to finish. The toolbar's **Set Up Everything** action can also run this inside the editor.
5. In Unreal, choose **Project Settings → Plugins → Backyard Monsters NXT** to set the host address, port and game rules. Use **Standalone Game** from the Play dropdown for the first test. Tick **Create a new account on this host**, enter a username/password and choose **Host & Play**.

The editor toolbar contains **Backyard Monsters NXT → Setup All, Import / Refresh Assets, Generate Maps, Validate Setup**. These use the fixed plugin `SourceAssets` folder; there is no asset directory picker. Existing maps and the generated material are preserved. Incremental importing resumes without reimporting unchanged files. After all imports finish, a separate **Finalizing color texture defaults** pass checks every source texture, including cached imports, applies **Default** compression with **sRGB enabled**, and saves corrected assets. Source transparency, sprite edge clamping and terrain wrapping are preserved. The final setup report lists how many textures were verified and corrected. Unreal may still display normal-map detection notices during import; these precede the final pass and are not its result.

If compilation fails before the editor opens, Unreal Build Tool records the error in `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.

## Build the shared game package

After setup completes, package the project using Unreal Editor's normal Windows packaging controls. The bundled command scripts remain optional convenience tools.

Distribute the **whole Windows output folder** to every player. The same executable has both **Host & Play** and **Join Server**. Never include an existing host's `Saved` folder in the distributed package.

Players join the host IP/DNS name shown in the menu. The configurable game port defaults to UDP 7777. For a remote internet connection, that port must be reachable on the host; this project does not provide a relay or automatic router traversal. Using the host's LAN IP works for machines on the same reachable LAN. A client on another computer must not use `127.0.0.1`, which points to itself.

Login and account creation use the normal Unreal client/server connection to the selected host. The same game package can host and play or join another host. Accounts belong to the host that created them; hosting on another computer does not copy an account or its progress.

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

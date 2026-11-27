# Independent source review

Reviewed 27 November 2026. This review covers the submitted C++ source, its cross-module JSON contracts, editor setup code, source catalogs and asset manifests. **Unreal Engine was unavailable: no UHT/UBT compilation, editor launch, asset import, cook, packaged session or multiplayer load test was performed.** A source review cannot establish that this is a working or production-ready game.

The project remains the initial core-loop implementation described in `STATUS.md`. The preserved 140 building catalog records and 28 monster catalog records do not imply implementation of all original systems. See `ORIGINAL_GAME_AUDIT.md` for source mechanics, deviations and the larger parity acceptance matrix.

## v0.2.0.2 Windows game-target correction

Both supplied packaging logs report the same single C2039 failure in `BYMBoard.cpp`: `UTexture2D::IsDefaultTexture` is unavailable to the non-editor target. The initial editor target succeeds and the remaining listed runtime translation units compile. This explains why opening the project did not expose this game-target error. The renderer's readiness block now guards only the placeholder API with `WITH_EDITOR`, retains the null check in every target, and retains hide/retry for editor compilation placeholders. Original atlas dimensions and frame validation are unchanged.

A focused check extracts the actual corrected readiness block and builds it with editor and game preprocessor definitions against strict texture stubs. The game stub deliberately omits `IsDefaultTexture`; the previous expression must fail against it. This checks the reported target-boundary defect and readiness behavior, not native Unreal cooking, staging or rendering. No simulation inputs changed, and the prior simulation validation hashes remain applicable.

## v0.2.0.1 compile correction

The supplied `Log(10).txt` shows 24 errors across the map-authority tests, simulation and world-map widget. The map test used standard math without including `<cmath>`. Five newly added JSON loops passed UE 5.8 shared-storage keys to APIs requiring `FString`; each now constructs an owned string explicitly. The map widget treated the raw renderer pointer as a shared pointer and called an unavailable `FReply` focus method. It now checks the raw pointer and uses `SetUserFocus`.

The prior portable harness used ordinary `FString` JSON keys and included math transitively, so it did not expose these engine-compatibility failures. Its JSON iteration adapter is now stricter, and separate compile-contract checks reproduce and reject the former expressions. The affected Slate APIs were checked against Epic's UE 5.8 documentation. This remains bounded compiler/source evidence, not an Unreal build. The font-constructor deprecation in the supplied log is nonblocking and is unchanged.

## v0.2.0 Map Room review

The audit selected the supplied original Map Room 3 branch and checked its world generator, hex geometry, authored site layouts, tier tables, attack pricing, level permissions, capture threshold, bonuses, fortification armor and special stronghold emitters. Original source references and conversion hashes are bundled. The native map uses original art with explicit native sidebar/selection adaptations; it is not presented as a Flash pixel-perfect renderer.

Cross-module review corrected territory battles stopping at the old half-building objective, missing capture finalization, wrong defending-owner result routing, neutral-site RP awards, map-cost bypass through legacy attacks, online-home protection, and omitted retaliation allowances. Training scale is frozen per paid queue group; both current and next-level UI quotes use the active MR3 cost/time scale.

Persistence review replaced full pristine-site copies with source-relative building deltas, retained an explicit ready-army roster across capture and abandonment, removed the arbitrary 10,000-site reload cutoff, preserved fractional income, and validated moved whole footprints while allowing unchanged authored fractional layouts. Scouting caches are bounded; an evicted valid view is reconstructed instead of displaying the visiting player's home. Source cells, owned/modified sites and active combat remain distinct. The existing host save-size bound is retained and failures do not silently truncate progress.

Presentation review checked yard-qualified resident identities, roster ownership, battle cleanup, independent sprite materials, relationship animation clocks and all four stronghold emitter origins. Bot migration changes only eligible positions and a one-time marker; paid tasks and inventory survive. Advanced yards use ordinary move commands.

The Slate/API review checked RPC declarations against implementations, generated-header ordering, numeric assertion types, stable brush ownership, native map response chunking, request coalescing, resized-dialog fixtures and map input isolation. It caught and fixed HUD teardown leaving map input flags latched. Selected cells no longer rebuild the canvas during map interaction. Uncertain Slate drawing and brush signatures were checked against Epic's UE 5.8 API documentation. No native engine build or render pass was available.

The current tests and their exact input hashes are recorded in `VALIDATION.md` and `Tools/OfflineValidation/last-run-manifest.json`. Native editor fixtures cover resident transitions, stronghold/relationship materials and the map's normal, locked, long-text, paged and attack-confirmation layouts. Their inclusion is not an execution result. Long-running host performance, full source-mode army parity and real network/UI behavior remain explicit gates.

## v0.1.9.1 compiler correction

The user-provided UE 5.8.4 build log identifies one blocking diagnostic: the worker material test passes an `int64` and an `int32` to overloaded `TestEqual`. Both operands now explicitly use `int64`, retaining the exact rounding expression and test. A read-only sweep of 31 native test files and 745 equality assertions found no similar unresolved call; all other listed test translation units compiled in the supplied log. An isolated C++ reproduction fails before the fix and passes all 30 source facing cells afterward. The existing font-constructor deprecation warning is nonblocking and remains unchanged. No engine rebuild is claimed.

## v0.1.9 workers, mushrooms and Shiny review

The source audit compared the actual supplied worker, mushroom, store and yard-expansion code, including server price overrides and ordinary-game branch differences. Prices and eligibility are extracted into a reproducible catalog with 21 retained source references. Review corrected the configured starting Shiny to 1,500, retained all original decoration assets, and kept source-blocked products unavailable.

Authority review checked worker admission before payment, nearest available assignment, arrival-gated work, independent repair, temporary legacy assignments, stale purchase tiers, once-only mushroom consumption, private rewards, speedup revisions and shared completion/XP. Save validation now rejects missing or inconsistent mushroom assignments, mismatched arrival/pick deadlines and route endpoints. Zero-time construction completes immediately. Decorations are excluded from combat objectives and blast damage, matching their non-targetable role.

Presentation review checked shared host/client routes, source directional poses rather than frame-cycling the worker sheet, original mushroom variants and decoration frames, and source-projected rectangular yard bounds across expansion, placement, deployment and camera containment. The worker dock scales within shorter viewports. Review corrected misleading repair advice while a worker is travelling, stale mushroom-completion status and missing decoration catalog enrichment; all 73 host-approved products now reach the native decoration UI. Independent review of changed older test fixtures confirmed their original gameplay assertions are retained; synthetic legacy work is explicitly migrated and deployment fixtures use the new source-sized yard edges.

Portable simulation and camera checks are recorded in `VALIDATION.md`. These source and numerical checks do not execute Slate, render materials, run native SaveGame I/O or prove UE 5.8 build compatibility. Native tests for the new dialog layouts and board materials are included for the target engine.

## v0.1.8.1 UI correction

The supplied login screenshot exposed shared layout paths that the earlier simulation tests did not exercise. Review traced the compressed logo to cached resource dimensions instead of original image dimensions, narrow text to automatic wrapping inside intrinsic-width slots, and overflowing actions to form content exceeding a fixed frame. The correction uses canonical manifest sizes and stable brush storage, natural-size labels, explicit paragraph widths with per-character fallback, and bounded scrolling above fixed action slots. The review checked login, HUD and gameplay-dialog changes, scrollbar allowances, the new fixture lifetime and documented Slate API signatures. No remaining concrete blocker was found in the final source. Three real Slate regression suites are included but remain unexecuted here; no rendered or click-through pass is claimed.

## v0.1.8 Monster Bunker review

The Bunker pass was reviewed separately across original source rules, host commands and persistence, native UI, atlas preparation and board presentation. Review corrected held-open doors advancing toward closed, stale attacker references to pruned returned defenders, Eye-Ra attacks against defenders omitting nearby buildings, source target-retention/cache timing and failed path searches ignoring their retry clocks. It also verified atomic mixed-species fees, owner-only stored rosters, separate attacker/defender accounting, once-only casualty debits and destroyed-home save behavior.

The final source review found no remaining blocker in those changes. This is a bounded source review, not proof of complete original-game parity or engine compatibility. The current simulation and native runtime test bodies are also exercised by the optional portable adapter harness; exact results and exclusions are in `VALIDATION.md` and `ALGORITHM_CHECKS.json`.

## Findings corrected during this review

| Finding | Final source behavior / evidence |
| --- | --- |
| Monsters bypassed original unlocking and stayed at level-one stats. | Source Locker/Academy prices, gates and per-level arrays now drive host commands, army housing, hatching and captured deployment stats. Native UI reads host availability and live current/next stats. |
| Progression migration could discard previous inventories or reject a valid repair. | Schema 1 infers known species from existing ready/queued/deployed troops. Schema 2 persists jobs/levels and accepts an Academy repair alongside its research. Review added duplicate-job, cost, timestamp and ownership checks. |
| Cancellation could mishandle storage and repeated commands. | Paid Putty is refunded only into available storage; already-over-cap wallets are preserved and a second cancellation fails. Native tests cover these cases and two simultaneous Academy jobs. |
| Source-hash cache skips let old normal-map settings survive. | A separate final pass runs after all import calls, includes cache hits, finishes texture compilation and enforces Default/sRGB color settings. Corrections are saved, failures remain errors, and the setup report counts verified/corrected textures. Original alpha, sprite clamping and terrain wrapping are preserved. A native regression test is included; it has not been executed in Unreal here. |
| Construction menu concealed quantity locks and unaffordable upgrades. | Host buildOptions expose quantities and reasons; selected-building UI always shows next upgrade costs and requirements. |
| An undisclosed 30-second cutoff ended invasions while choosing troops. | Only the visible battle deadline applies; default duration is the original five minutes. Reserves keep an unfinished battle active. |
| Client move/build tools completed before host acknowledgement. | Reliable command results retain rejected previews and finish accepted operations. |
| Isometric artwork was placed on an unprojected square grid. | Source projection, inverse pointer mapping, original back-corner pivots and diamond building previews now agree. |
| Construction and deployment shared incompatible preview bounds. | Separate methods match the host's one-cell construction inset and outer-three-cell deployment band, including destroyed buildings. |
| Masked artwork made soft black shadows opaque. | New unlit translucent M_SpriteAlpha preserves alpha and custom-depth writes; terrain keeps its masked material. |
| UI looked unrelated to source and replaced buttons each snapshot. | Native HUD uses original artwork/fonts/layouts and live numeric attributes; input blockers and responsive docks are reviewed separately from engine visual validation. |
| Flinger progression required an unavailable Map Room. | Type 11 is constructible under original requirements and included in existing bot construction candidates. |

| Editor map had no visible terrain before Play. | Board registration now creates a transient editor-world terrain preview, excluded from saving and PIE duplication. Fresh maps store a centered downward editor view. |
| Explicit orthographic clip distances could be overridden; physical exposure was unspecified. | Automatic plane adjustment is disabled and manual exposure explicitly excludes physical-camera settings. Camera width and controls are unchanged. |
| Missing artwork was hidden without a visible diagnostic. | Runtime reports the first missing material/mesh/catalog/texture in the status UI and log. Editor setup checks available material shader compilation results. |
| Multiplayer camera pawn was not replicated. | `BYMCameraPawn.cpp` uses owner-only pawn replication and disables movement replication; local camera movement stays local. |
| Mouse-coordinate API received double references. | `GetMousePosition` calls now use float temporaries, then construct `FVector2D`. |
| Editor sound importer referenced `USoundFactory` without linking its module. | `BackyardMMOEditor.Build.cs` now declares `AudioEditor` privately. The supplied UE 5.8.4 log passed C++ compilation and failed only at this editor DLL symbol. |
| Editor used settings classes without declaring their modules. | `BackyardMMOEditor.Build.cs` declares `EngineSettings` and `DeveloperToolSettings`. Runtime remains free of editor-module dependencies. |
| Catalog prerequisites retained unresolved ActionScript constants. | Preparation resolves known constants numerically. The preserved original data records the source; unknown-overworld ID 999 is not made up as a playable building. |
| Original healer negative damage risked being treated as an ordinary attack. | Original values remain intact; C15/C16 are excluded from recruitment until their healing behavior is implemented. |
| Monster facing columns advanced as animation frames. | `BYMBoard.cpp` derives a facing column from movement and uses separate idle/walking rows and the original -90-degree facing offset. |
| Compact UI catalog could discard atlas and level metadata. | `ABYMBoard::BeginPlay` loads the complete local catalog itself. `ApplySnapshot` only adds missing definitions, so compact UI summaries cannot replace its art records. |
| Building and monster overlap used unrelated depth layers. | Both now use the same screen-Y depth convention; building composites and monsters use their source pivots. Exact rendered overlap still requires an engine comparison. |
| Original monster flight/movement information was omitted. | Generated catalogs now preserve movement/pathing/flying and original attack-delay fields. |
| Full immutable catalog inflated replicated snapshots. | The host sends dynamic state and supported IDs; clients load immutable definitions locally. Larger snapshots use bounded ordered chunks. |
| Camera footprint lookup reparsed large JSON each frame. | Board caches catalog/building objects and exposes a direct footprint getter. |
| Moving an existing building previewed a one-cell footprint. | Camera now asks the board for the selected building's actual footprint. Server placement remains authoritative. |
| Client settings could disagree with host grid geometry. | Snapshot `rules.yardCells` and `rules.cellSize` update the board and camera. |
| UI control help and timers disagreed with runtime. | Help describes right/middle drag to pan and Escape to cancel. Countdown display samples host time plus a monotonic elapsed interval. |
| Fullscreen Slate container could intercept board input. | Root widget and overlay use `SelfHitTestInvisible`; successful authentication and button actions restore game-viewport focus. |
| Expired restored raids could simulate additional damage. | Simulation checks an expired battle deadline before advancing combat. |
| Defense cooldowns were lost on a host restart. | Cooldowns are serialized, validated and restored. |
| Saved training queues could silently disappear when malformed. | Loader requires a bounded array and validates each entry. |
| Malformed saved battle troops could silently become an empty army. | Loader now requires a bounded troop array, unique bounded IDs, valid targets and a count equal to the committed deployment count. |
| Account and bot ceilings could exceed the saved-yard loader bound. | Initial bot creation and player registration both enforce the combined 100,000-yard ceiling. |
| Lowering the bot count did not reduce active bots. | The configured prefix of the stable bot list acts; excess saved bots remain preserved and inactive. |
| Zero RP prevented a first defeat from losing points. | Header and packaged defaults now set `bClampRPAtZero=false`; a zero-score loser can reach -15. |
| Automated test launcher could accept an empty or stale result. | Every run uses a fresh timestamp/GUID report directory and requires a recognized report, all native tests discovered from the bundled source succeeding, no failures, and no unfinished selected tests. |
| C11 sprite source metadata exceeded the image by one row. | The untouched original strip is 34 pixels high while source metadata says 35. Preparation preserves `originalFrameHeight=35`, normalizes runtime bounds to 34, and extracts a correctly bounded idle thumbnail. |
| Construction/training menu durations ignored host modifiers. | Client enrichment now uses the host's construction/training multipliers and the same final timer bounds; countdowns continue to use authoritative completion timestamps. |

## Renderer and UI data contract review

The following traces were checked by reading producer and consumer code. They are schema/source evidence, not rendered screenshots or execution results.

| Consumer | Authoritative or local producer | Checked fields |
| --- | --- | --- |
| Building planes | `BYMSimulation::YardJSON`, full local building catalog | Building ID/type/level/cell position/current HP/max HP/ready time/footprint; level normal, damaged and destroyed composite textures; pixel dimensions and pivots. |
| Monster planes | `BattleJSON`, full local monster catalog | Troop ID/type/position/HP; source texture, frame dimensions, rows, columns, directions, idle/moving rows and source pivots. |
| Ground/camera | Snapshot `rules`, local terrain artifact | Host cell count and cell size; 1000×500 original ordered swatch blend, native pixel-scale repetition and extended backdrop; orthographic straight-down camera. |
| Yard dashboard | Snapshot `selfId`, `home`, `yard`, client enrichment | Home wallet/army/training/log/housing; viewed-yard structures; building names/thumbnails/upgrade price and eligibility. Own and rival views keep distinct home state. |
| Construction/army menus | Host `buildableTypes`/`trainableTypes`, local catalog | Only implemented types are offered; source prices, requirements and monster base values are used. Actual requests are revalidated on the host. |
| Battle panel | Snapshot `battle` and `battleObjective` | Attacker/defender IDs, absolute deadline, objective and deployed troops. The client does not calculate or submit a winner. |
| Ladder | `FBYMSimulation::Ladder` | Stable identity, name, RP, rank, bot flag, active-bot flag, shield, wins/losses and pagination. |
| Host status | `UBYMServerSubsystem::GetSnapshot` | Online count/capacity, last save, remaining autosave interval and save failure message. No account password-hash/salt records are included. |

Every generated art object path follows the same sanitized `SourceAssets` relative-path mapping in asset preparation, editor import and `ABYMBoard::AssetObjectPath`. Runtime-generated planes call `SetRenderCustomDepth(true)`; imported textures are data assets and do not themselves have a primitive-component custom-depth flag.

## Authority, authentication and persistence review

`ABYMGameMode::StartPlay` starts authority before `Super::StartPlay` dispatches actor `BeginPlay`. The subsystem's authority guard requires an actual game world, a non-client network mode and the native world game mode. Opening the mainmenu does not initialize an account database. Direct login requests run after the world authority starts.

Authentication binds identity to the server's connection record. The owned PlayerController sends login/registration directly to the host through Unreal RPCs; passwords are stored as salted verifiers in native account `.sav` records. Expensive password work runs on copied request data in a worker; the game-thread completion rechecks a pending ticket, current authority, username/session uniqueness and account identity before committing. Disconnect and shutdown clear pending tickets. The controller callback holds a weak pointer and checks its connection key. These mechanisms still need real concurrent-session testing.

Registration adds the account and yard as one logical change, writes a tagged world snapshot and commits the native account save; failed commits roll back in-memory registration. The registration journal lets startup distinguish uncommitted new yards from existing players. Normal logout removes the session binding while retaining gameplay state in memory. Hourly and orderly-shutdown saving share the host world lock. Shutdown calls are idempotent. The save path writes and flushes a pending file, preserves the previous committed file, then replaces the committed file. Account serialization uses `USaveGame`; world and bot simulation keep their existing JSON format. Invalid initial save data is refused rather than silently reseeded. This is source-level evidence only; forced disk/full-process termination and Windows filesystem behavior have not been exercised here.

The host owns economy, building placement, army counts, combat, shields and RP. A modified client cannot directly submit balances or battle results through the implemented command API. This is not an anti-cheat guarantee: the machine owner controls the host, transport-level gameplay encryption is not added, and a complete network/security review is outstanding.

## Offline validator scope

Run `python Tools/validate_source.py --json TestResults/source-integrity.json` for repeatable standard-library checks. It verifies manifest bytes and SHA-256 hashes; importable extensions; relative paths and case-insensitive Unreal object collisions; source catalog spot-checks; numeric prerequisites; composite dimensions/pivots; directional atlas bounds and image references; original notices; generated-header order; required APIs and RPC definitions; lexical delimiter balance; plugin-local includes; settings/defaults; runtime/editor dependency separation; staged JSON; and accidental private host state/environment-path inclusion.

The JSON report records `unreal_compiled: false` and `runtime_tested: false`. A pass does not validate C++ overloads, UHT-generated code, linking, cooking, rendering, security, gameplay parity or capacity. The included native Unreal automation tests have not run in this environment.

The final artifact's full hash run is recorded in `OFFLINE_VALIDATION.json`: the current check count, covering **6,525 prepared files, 6,413 importable assets, 140 building records and 28 monster records**. The eight building-art warnings are original placeholder/unavailable catalog entries (112, 122–126, 128 and 130); they are not silently replaced with invented artwork.

## Outstanding release gates

| Gate | Required evidence |
| --- | --- |
| UE 5.8 API/build compatibility | Clean non-unity runtime/editor build and UHT pass with the updated native runtime/editor modules. Resolve every actual compiler diagnostic. |
| Editor automation | Fresh project Setup All succeeds with no folder prompt; repeated setup preserves maps/material edits; source edits reimport; custom depth and both maps validate. |
| Packaged same-client hosting | Two machines using the same cooked Windows package can host, register, log in, disconnect and reconnect. Remote client writes no server account/world database. |
| Authentication races | Concurrent same-name registration, duplicate login, disconnect during password verification, shutdown during pending registration and sustained invalid credentials behave predictably. |
| Persistence recovery | Disk-write denial/full disk, process kill before and during replacement, invalid account/world save, second host process and backup restore preserve a recoverable committed world. |
| World/raid restoration | Restart before/after a construction deadline; restart an active/expired raid; verify troop/cooldown integrity, one outcome, RP and shield exactly once. |
| Presentation | Compare representative source buildings, damage states, grass, sprite facing, pivots, overlap, input hit testing and native UI at multiple aspect ratios. |
| Scale | Profile the requested 100 simultaneous clients and 1,000 bots, concurrent combat, saves and a multi-hour soak. Default configuration is not evidence that this load is supported. |
| Original parity | Complete the missing systems and acceptance cases in `ORIGINAL_GAME_AUDIT.md` and `STATUS.md`, then compare behavior against source/reference sessions. |

No unresolved source-review item should be read as proof of a compile failure, and no resolved source-review item should be read as proof that the engine behavior has passed. The next concrete gate is the actual UE 5.8 build and packaged two-machine session.

## Combat and resident update review

- Replaced artwork-sized combat bounds with source rectangles and their coordinate conversion. Creature movement/range uses the projected source metric; tower targeting/range uses its separate source coordinate rules. Ground movement checks traversable edges and ranged flyers approach through an obstructed firing line.
- Host attacks use discrete cooldowns and bounded events. Old save records without monster cooldowns remain readable; current cooldowns are validated and persisted. Final battle events survive briefly in a separate presentation-only payload, without keeping deployment active.
- Resident selection and movement were independently reviewed and exercised using actual C++ helpers in a lightweight standalone harness. Repeated snapshots preserve walker state; layout changes rebuild navigation; yard switches clear identities; ready counts and the hard limit are respected.
- Source animation rows are distinguished from directional columns. Missing source rows are excluded with explicit provenance. Direction-only monster recoil is labeled an NXT presentation enhancement. Defense frames retain original offsets and layered pixels.
- Sprite sorting is constrained to the renderer's signed 16-bit range. Hover overlays use distinct high priorities and visible closed outlines; batch deployment preview checks the same formation points as the host. Wooden panels and resource bars now consume pointer clicks, and cached panel regions suppress yard placement/hover beneath UI.
- Normal project opening notices a changed bundled asset/catalog revision. The importer records the completed revision only after all reached textures pass final normalization, so a canceled/failed import is not marked current. Translucent material opacity now multiplies source alpha by tint alpha; the exact previous generated connection is upgraded without replacing custom opacity graphs.
- The native test launcher derives its required test count from bundled test sources rather than retaining an outdated fixed count.

These are code, data and isolated algorithm checks. They do not replace an Unreal compiler, renderer, cooked build or two-machine test.

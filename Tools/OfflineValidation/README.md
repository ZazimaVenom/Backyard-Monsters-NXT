# Optional source regression validation

**v0.2.1 delivery status:** current network checks are recorded in `network-transport-results.json` (6 tests/238 assertions, 394 exact snapshot comparisons) and `network-controller-results.json` (71 assertions). The broader rerun was interrupted for the user's requested delivery deadline. `last-run.txt` and `last-run-manifest.json` retain the historical complete v0.2.0.1 run; they are not a current full-suite pass. No native Unreal build or live network test is claimed.

The game opens normally through `BackyardMonstersNXT.uproject`. This directory is an optional developer test tool; it is not part of the game's build, launch, account system or server persistence.

From the project directory, with Python 3.9+, a C++17 compiler and system zlib development headers/library available:

```sh
python Tools/OfflineValidation/run.py
```

Use `--filter Bunker` to execute only Bunker tests, or `--cxx /path/to/compiler` to select the compiler. Add `--record` to save the run log and exact input hashes in this directory; a run fails validation if any recorded dependency changes while it is running. The tool creates its executable in an automatically removed temporary directory. It does not modify the project, source assets or `.sav` files. No Python package is required. The optional transport adapters link system zlib (`-lz`) to exercise the real codec's compression paths. The game itself uses Unreal Core's built-in compression and needs no separate zlib installation. The recorded run used GCC 14.2.0 on Linux.

## What is executed

The runner compiles the **current, complete `BYMSimulation.cpp`** and the actual economy, experience, combat rules, building lifecycle, Bunker rules, resident, sprite atlas, building animation, worker route, rival layout and original Map Room rule implementations. The Shiny, mushroom, store, map territory, four-emitter stronghold combat and host persistence implementations are included directly by the actual simulation source. It includes the **unchanged native runtime automation test bodies** directly from the plugin's `Private/Tests` directory. It reads the project's actual catalog, experience, original store rules, all 26 original map-yard templates and the complete 166,325-row original map data. Host settings defaults are extracted from the current production header during each run.

The recorded result is **114 test bodies, 155,840 assertions, zero failures**. `last-run.txt` lists each test; `last-run-manifest.json` records the exact input hashes. The suite covers command ownership and validation, construction, progression, parallel Hatchery queues, production and repair, XP, combat and targeting, 1,000-rival persistence, residents and sprite/animation arithmetic. The current native source contains 130 automation tests in total; this runner executes 114 of their actual bodies.

Ten Bunker authority/combat tests cover:

- Atomic mixed-species transfer, researched Goo prices and batch rounding, ordinary capacity, no duplication, ownership, busy/damaged/work gates, and dismissal without refunds.
- Historical world schemas 1–4, current schema 5, strict garrison and active battle validation, private owner inventory and public yard privacy.
- Real host invasion/deployment, door/release clocks, separate defender records, unchanged attacker deployment and Flinger accounting.
- Return and repeated record reuse, full health on the next ordinary sortie, death debited once, active reload, surviving released defenders after home destruction, and no lost-stock resurrection.
- Removal of returned defenders with dead attackers retaining retaliation references, followed by successful reload.
- Defensive Eye-Ra suicide and lethal damage, exact source linear ground blast and minimum damage, no repeated explosion after reload, and offensive Eye-Ra damage to defenders and nearby buildings.
- Contact target retention, movement before retarget expiry, persisted retarget clocks and invalid acquisition-cache clearing.

Sixteen additional worker/economy regressions cover:

- One free worker, nearest-worker travel clocks, arrival-gated paid work, ordinary repair without worker consumption, exact job assignment and release, mid-route reload, temporary migration workers for already-paid jobs, and strict assignment validation.
- Actual worker route calculation, source obstacle hops, exact small-distance endpoints, negative coordinates and expanded rectangular resident movement.
- General Store construction and its half-health gate, initial configured Shiny, original worker/expansion/packing prices and caps, stale-tier rejection, untrusted prices, ownership and private balances.
- All 73 normal priced decoration products, blocked and reward-only exclusions, source store-price overrides, instant placement while workers are occupied, atomic charging and persisted identity.
- The source mushroom seed population, regrowth interval and cap, persisted random state, private 0/3/8-Shiny rewards, worker-bound picking, save/reload, exact one-time reward and rejected orphan/tampered picking clocks.
- SP1–SP4 eligibility boundaries and price branches, partial-reduction revisions, stale replay after reload, maximum quoted charge, and source free completion before the first General Store's worker arrives.
- Decorations excluded from combat victory and health denominators, untouched by nearby Eye-Ra splash, and unchanged mushroom/worker records after battle.

The synthetic monster-entrypoint Academy now occupies the mushroom-free interior of its 100-cell fixture; its former (1,1) position could collide with the original random mushroom collar. All original access, health and payment assertions remain. The earlier geometry fixtures deploy at the actual rectangular right border. Contact/cadence fixtures retain their original controlled approach distance after deployment. Synthetic pre-worker paid-job fixtures explicitly omit the new worker extension so the compatibility path is tested; actual paid-job tests advance to the host's arrival-adjusted completion deadline. Independent worker tests cover the new strict current-save assignment path. Existing gameplay assertions remain in place.

Map Room regressions exercise the actual authoritative command path and full original world, including:

- Source odd-row hex topology against an independent breadth-first reference, original bounds, range, price, capacity, production, fortification reductions and exact capture thresholds.
- Progression gates, private bookmarks, bounded map windows, full bookmark/ownership-page payload limits, stale quotes, replay prevention, priced admission and online main-yard protection.
- Real deployment and contested 89/90-percent outcomes across resource outposts, strongholds and defenders; recapture, ranked owners, durable damage, repairs, nontransferable main backyards and one-hour main-yard shields.
- Fractional resource credit across short ticks and reload, ownership changes, abandonment, source-delta site saves, strict moved footprints and immutable core positions, malformed ownership and missing owned damage state.
- Four independent source stronghold emitters, distinct origins, source damage at half health, one-second clocks, target retention outside acquisition range, reload and invalid saved targets/clocks.
- Rival admission/deployment and safe migration of old identical starter layouts without altering paid queues, identities, army or resources.
- Pristine scout cache eviction/reconstruction without displaying the viewer's army, cached tribe replacement during new-player allocation, atomic random relocation and retained distant holdings.
- Original level protection and persisted retaliation counters, with credit consumed only by accepted attacks.

The engine-only additions cover yard-qualified resident sprite transitions, the original stronghold relationship lights and emitter origins, and native Map Room layout containment across resize and DPI. They remain unexecuted here.

The runner also checks the concrete API contracts reported by the user's UE 5.8.4 build log. Its JSON adapter exposes a distinct shared-string key during deduced iteration; passing that key directly to an FString gameplay map or comparison must fail compilation. Explicit `FString(*Entry.Key)` conversion and the existing explicitly typed pair-copy form remain supported. The actual map renderer guard and mouse-focus return expression are extracted from `SBYMWorldMap.cpp` and compiled against narrow signatures: a raw renderer pointer and `FReply::SetUserFocus`. Three corrected snippets must compile, five old invalid expressions must fail, and the math-using map test must directly include `<cmath>`. These checks catch the reported regressions; they do not compile Slate or establish general Unreal compatibility.

The separate sprite-texture packaging check is run with:

```sh
python Tools/OfflineValidation/check_packaging_contracts.py --record
```

It extracts the actual texture-readiness block from `ABYMBoard::TryConfigureAtlas`, including its preprocessor guards, and compiles and executes it with `WITH_EDITOR=0` and `WITH_EDITOR=1`. Its minimal packaged-game texture stub deliberately has no `IsDefaultTexture` method. Both current-source variants pass 12 behavior assertions each, covering missing textures, safe missing meshes, editor placeholder retry/hiding, ready textures and retry completion. The historical unguarded call passes the same 12 assertions in editor mode but must fail compilation in game mode. This gives three successful compile/run controls and one required compile failure; the current production block accounts for 24 of the 36 behavior assertions.

`packaging-contract-results.json` records this focused result and the exact board/checker hashes separately. The checker uses no game adapters or engine installation and does not establish Windows packaging, cooking or rendered behavior. The unchanged simulation suite was not rerun for this editor-only API guard; its previously recorded results and input hashes remain separate.

## What this does not validate

This is **not an Unreal Engine build or Unreal automation run**. The adapters substitute engine containers, smart pointers, strings, vectors, math helpers, JSON parsing/serialization, settings defaults, plugin discovery, GUID generation, clock and random stream. The clock is deterministic; the random stream uses the C++ standard library instead of Unreal's exact RNG sequence. The actual resident gameplay helper is compiled, not replaced.

The run cannot establish UHT/UBT compatibility, Unreal JSON behavior in all edge cases, materials/rendering, input/UI, packaged asset cooking, multiplayer replication or transport, platform `.sav` I/O, or production server performance. It excludes the two runtime tests requiring native account/SaveGame services and all fourteen editor tests requiring Unreal objects, import/material services or actual Slate layout. Those sixteen tests remain in the source and require Unreal. Six native Slate suites cover login, shared dialogs/HUD, original image aspect, store/mushroom/decoration panels, the worker strip and Map Room canvas/details/paging/attack confirmation across resize and DPI, including short and portrait viewports; none has been executed by this runner.

The recorded dependency manifest includes every simulation `.inl`, the worker/rival/map rule implementations, the actual store and map JSON data, native test bodies, quoted local header dependencies and the isolated API adapters. It identifies the source compiled by this run; it does not establish engine ABI or Unreal rendering compatibility.

The portable build uses `-O1`; this keeps repeated parsing of the complete source map practical across independent fixtures and is not a production performance measurement. Compiler warnings are treated as errors for the compiled production source. Test adapters are marked system headers. Unused native test parameters and existing compact multi-statement indentation warnings are excluded. GCC's temporary shared-pointer reference warning is excluded because existing tests retain the owning save object while referencing its child arrays. GCC also leaves `-Wrange-loop-construct` visible but nonfatal: its warning on the adapter's JSON pair-copy conversion is specific to this emulation, while the supplied UE 5.8.4 log accepts those existing typed loops. Other enabled warnings remain errors. The test-only map adapter uses hashed key lookup with Key/Value iteration; it is not an implementation of Unreal allocation or pointer-stability guarantees. Numeric TestEqual assertions reject mixed arithmetic types so the adapter cannot conceal Unreal numeric-overload errors. These adapters are isolated from the plugin build and do not add game runtime dependencies.

## Bandwidth transport checks and measurements

```sh
python Tools/OfflineValidation/check_network_transport.py --record
```

This separate check compiles the actual snapshot codec, bounded stream helper and native network test bodies with the current host simulation. It verifies full/delta reconstruction, unchanged suppression, original JSON type/array/null values, Unicode, invalid data and decompression limits, semantic baseline identity, atomic rejection, partial-chunk credits, early/duplicate/wrong acknowledgements, ordered terminal-state barriers, bounded queues, sequence wrap and full recovery after rejected frames. Real snapshot fixtures exercise idle yards, production/hatching, invasion and the complete Map Room response at its two-second polling cadence.

Every real decoded snapshot is independently parsed and compared with its pre-encoding JSON by Python. `network-transport-results.json` records exact source hashes, native test counts and before/after **application payload bytes**. These figures exclude Unreal RPC/packet headers, acknowledgements, retransmissions and other applications' traffic. They are not measured socket throughput or a prediction for 100 clients. The portable adapter uses system zlib at level 1 for Unreal's speed-biased compression option and represents TCHAR strings as UTF-8 bytes; Unreal's UTF-16 conversion and its native networking still require engine execution.

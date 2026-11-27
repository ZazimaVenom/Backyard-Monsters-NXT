# Backyard Monsters Refitted: source audit and parity ledger

Audit date: 26 November 2026. Input: the supplied two-part `backyard-monsters-refitted` RAR. Paths below are relative to its extracted root. This audit describes original source, not completed Unreal functionality.

Earlier update, 27 November 2026: source preview 0.1.2 implements the normal Locker/Academy progression flow and per-level training/combat stats. `MONSTER_PROGRESSION.md` records the implementation and remaining differences. Source preview 0.1.3 additionally implements the resident/combat presentation changes recorded in `COMBAT_AND_YARD.md`. The initial-contract gap table below remains a historical baseline; it is not the latest implementation status.

Earlier addition, 27 November 2026: source preview 0.1.8 implements ordinary count-based Monster Bunkers with the source runtime capacity override, separate defensive units, original door presentation and native transfers. See `MONSTER_BUNKER.md` and `STATUS.md` for current implementation boundaries; engine validation remains unrun.

Current addition, 27 November 2026: source preview 0.1.9 adds worker assignments, mushrooms and Shiny, the General Store, ordinary decorations and source-priced yard expansions. `WORKERS_AND_SHINY.md` and the reproducible `store-rules.json` describe the exact supplied-source branch and adaptations. The initial gap table remains historical.

## Source identity and size

The README identifies client version **128.5690**: decompiled ActionScript 3 Flash client plus a reverse-engineered Bun/Koa TypeScript server, MikroORM/PostgreSQL and Redis. Extraction contains **8,951 files / 297,169,261 bytes**. Main types: **1,508 ActionScript, 429 TypeScript, 5,265 PNG, 894 JPG, 102 MP3, 70 SWF, 11 TTF**. Raster image counts include UI, spritesheets, backgrounds, portraits and decorations, not merely placeable buildings. SWFs and vector clips cannot be treated as directly importable Unreal textures; fidelity requires exporting timelines, vector graphics, pivots and frame semantics where no raster equivalent exists.

Catalogs contain **140 overworld records**, **135 Inferno records** and **135 outpost records**. These are data slots including decorations, disabled entries and placeholders, not 410 independent gameplay buildings. `CREATURELOCKER.as` has explicit C1–C19, IC1–IC8 and C200 definitions, but exposed CreepType classes augment/replace them; enum constants alone undercount content.

## Source license and attribution

`LICENSE` is **PolyForm Noncommercial License 1.0.0**. The README's bottom section instead displays GPLv3 and `Copyright (C) 2025 | The Backyard Monsters Refitted team`. This is an unresolved discrepancy in the supplied source. Preserve the complete LICENSE and original attribution in distributions; do not relabel the source MIT or claim all assets are unrestricted. The README attributes the original 2010 game to **Kixeye**. This report does not resolve ownership of the original game's art/music separately from the restoration project's code. No source line beginning `Required Notice:` beyond the illustrative line in the license has been confirmed by this audit.

## Canonical data sources

| System | Source of truth | Why it matters |
|---|---|---|
| Overworld building progression | `client/scripts/YARD_PROPS.as` | Per-level resource costs, prerequisites, build time, hitpoints, repair time, quantities, render offsets and sprite-sheet regions. |
| Inferno progression | `client/scripts/INFERNOYARDPROPS.as` | Alternate resource economy and Inferno variants. |
| Outpost progression | `client/scripts/OUTPOST_YARD_PROPS.as` | Alternate quantities, capacity and terrain mechanics. |
| Monster catalog | `client/scripts/CREATURELOCKER.as`, `server/src/game-data/stats/monsterStats.ts` | Unlock resource/time, research costs/timers, production costs/timers, housing, speed, health, damage, target preference. |
| Extra creep definitions | `client/scripts/com/monsters/creep_types/`, `client/scripts/com/monsters/monsters/creeps/` | Dynamic CreepType registration and behavior overrides; array import alone does not reproduce all monsters. |
| Building lifecycle | `client/scripts/BFOUNDATION.as`, `BUILDINGS.as`, `BUILDINGOPTIONS.as` | Place/move/sell, construction, upgrade, fortification, collision footprint, damaged and destroyed states. |
| Production and banking | `client/scripts/BRESOURCE.as`, `BASE.as` | Damage-dependent production, silo limits, collection, XP, terrain/boost effects. |
| Defense | `client/scripts/BTOWER.as`, `BUILDING20.as`, `BUILDING21.as`, `BUILDING23.as`, `BUILDING25.as`, `MONSTERBUNKER.as` | Ranges, cooldowns, target filtering, splash, projectile speed, bunker garrison, health damage multipliers. |
| Combat and pathing | `client/scripts/ATTACK.as`, `Targeting.as`, `PROJECTILES.as`, `com/monsters/pathing/`, `com/monsters/ai/` | Deployment, attack logs, targets, walls, navigation, healing, siege and special monster rules. |
| Player progression | `server/src/game-data/stats/experiencePoints.ts`, `services/base/calculateEmpirePoints.ts`, `client/scripts/QUESTS.as`, `ACHIEVEMENTS.as`, `TUTORIAL.as` | 56 level thresholds, empire points, missions, unlocks, tutorial sequencing. |
| World map | `server/src/controllers/maproom/`, `services/maproom/`, `config/MapRoom2Config.ts`, `config/MapRoom3Config.ts` | Three map generations, neighbors, hex terrain, range, outposts, takeover and relocation. |
| AI tribes | `server/src/game-data/tribes/` | Abunaki/Abunakki, Kozu, Dreadnaught, Legionnaire, Moloch and event base layouts. These are not autonomous player-equivalent rivals. |
| Save model | `server/src/database/models/save.model.ts`, `user.model.ts`, `controllers/base/save/` | Large persisted schema, offline timers and cross-system state. |
| Social | `server/src/controllers/friends/`, `controllers/mail/`, `chat/`, `services/alliance/` | Friends, gifts, inbox, truces, alliances, chat, ignore lists and history. |
| Events and late-game content | `client/scripts/com/monsters/replayableEvents/`, `siege/`, `halloween/`, `kingOfTheHill/`, `server/src/config/EventCalendarConfig.ts` | Replayable events, event rewards, siege, special monsters and competitive modes. |

## Confirmed numerical rules

### Resources, production, building examples

Overworld resources are indexed **r1 Twigs, r2 Pebbles, r3 Putty, r4 Goo** (`GLOBAL.as:899`). Inferno substitutes **Bone, Coal, Sulfur, Magma**; Shiny and Time follow in the resource label array. `getDefaultBaseData.ts` starts all four resource amounts at zero, maxima 10,000 each, and seven days' starter protection; configured Shiny is 1,500 (`GameConfig.ts`, fallback 1,000). A recreation which gives starter stock to skip the tutorial must label that as changed balance.

**Twigsnapper (ID 1)** has ten levels. Build costs in Pebbles: 750, 1,575, 3,300, 6,950, 14,500, 30,600, 64,300, 135,000, 283,600, 600,000. Build/upgrade times in seconds: 15, 300, 1,200, 3,600, 7,200, 18,000, 43,200, 86,400, 172,800, 259,200. Production per ten-second cycle: 2, 4, 7, 11, 16, 22, 29, 37, 46, 56. Harvester capacities: 720, 2,160, 5,670, 13,365, 29,160, 60,142, 118,918, 227,584, 424,414, 775,018. HP: 500, 950, 1,800, 3,400, 6,500, 12,000, 24,000, 45,000, 85,000, 165,000. Town Hall quantity table: 0, 1, 2, 4, 5, 6, 6, 6, 6, 6, 6. First prerequisite `[[14,1,1]]` means the relevant required Town Hall condition must be decoded faithfully with BUILDINGS rather than ignored.

`BRESOURCE.productionTimeout` is **baseCycle + ceil(baseCycle × (4 − 4 × health/maxHealth))**. Stored production is capped at the harvester's own capacity; collection is constrained by `BASE.Fund` and storage. Harvesting XP is collected amount before tutorial stage 200, then **ceil(collected × 0.5)**. Terrain and overdrive modify production. The original description says production is halted below half health; runtime `_canFunction` and damaged handling need to be ported together, not approximated from this getter alone.

**Town Hall (ID 14)** starts at zero resource cost and 10 seconds. Level 2 costs 7,000 Twigs + 7,000 Pebbles / 600 seconds. Level 3: 42,000 each / 14,400 seconds. Level 4: 240,000 each / 57,600 seconds. These are real-time construction timers, not instant level changes.

**Cannon Tower (ID 20)** HP per level: 6,000, 9,000, 12,600, 17,640, 26,460, 34,400, 45,000, 58,000, 75,500, 98,200. Firing damage in `BUILDING20.Fire` scales by **0.5 + 0.5 × health/maxHealth**; active tower overdrive multiplies by **1.25**. Range/rate/splash/projectile speed are catalog stats. Ground/flying targeting is explicitly differentiated in `BTOWER`.

**Monster Bunker (ID 22)** raw Map Room 3 capacity: 190, 225, 270, 330, 400. The ordinary runtime override in `GLOBAL.changeNotMaproom3SpecificBuildings` doubles these to 380, 450, 540, 660, 800; source preview 0.1.8 implements that ordinary branch (see `MONSTER_BUNKER.md`). HP: 10,000, 24,500, 52,000, 75,000, 105,000. The garrison participates in actual combat; a flat defensive score cannot reproduce original bunker behavior.

### Monster examples

**Pokey (C1)** unlock data: 4,000 resource / 600 seconds; first creature is marked unlocked by `CREATURELOCKER.Data`. Six research levels have HP 200/220/240/260/280/300, damage 60/65/70/75/80/85, speed 1.2, hatch times 15/10/8/7/6/5 seconds, Goo costs 250/450/675/800/1,000/1,250 and housing 10/10/10/9/8/7. Research costs/times: 4,000/7,200; 8,000/10,800; 12,000/18,000; 16,000/28,800; 22,000/43,200.

**Octo-Ooze (C2)** unlock: 8,000 / 3,600 seconds; health 1,000/1,100/1,300/1,450/1,600/1,800, damage 15/15/20/25/30/35, speed 1.4, target group 4. **Bolt (C3)** unlock: 16,000 / 7,200 seconds; speed rises 2.5 to 3.2, base HP 150, target group 3. These differ in target priorities, so generic identical combat behavior loses fidelity.

Special files include healers, flying units, splitting Slimeattikus, explosive Eye-Ra, Wormzer tunneling, DAVE, Project X, resurrection and Inferno units. `CREATURELOCKER.k_USE_REBALANCED_MONSTERS` is **false** in supplied source; the separate rebalance subtree should not silently replace the active stats.

### Attacks, protection, progression, world

Original server attack lock timeout is **7 × 60 = 420 seconds** (`services/base/isAttackActive.ts`). Main-yard attack validation checks protection, concurrent attacks, map membership, neighbor relationship, level gap, online defender last-seen within 60 seconds, and truce (`controllers/base/load/modes/baseModeAttack.ts`).

Original damage protection (`services/maproom/v2/damageProtection.ts`) is **1 hour after 4 attacks in an hour**, **36 hours after ≥50% damage** for main/Inferno yards, **8 hours after ≥25% damage** for outposts. Starting an attack clears the attacker's existing protection. These are source mechanics; the user's requested universal one-hour shield supersedes them.

`NeighbourConfig.ts`: matching level range 10, removal range 13; normal downward attack gap 7, world map gap 11; players both ≥40 exempt; target neighbor count 40, soft cap 50, display cap 180, max friend neighbors 10; inactive after 30 days; refresh every 24 hours; 10 attacks against a player in a day hides the pair for 14 days; revenge window 14 days.

Empire points = accumulated points + base value. Level thresholds begin **0, 900, 3,500, 5,000, 7,500, 10,500**, level 10 is 40,337, level 40 is 976,208,337, level 56 is 212,613,620,467 (`experiencePoints.ts`). Use 64-bit values for points/costs in UE.

MapRoom3 main-yard attack radius is 10 hex cells; minimum player distance 7. Stronghold levels 30/40/50, resource outpost levels 10/20/30/40/50, tribe outposts 45/50; stronghold ranges 10/15/20, resource ranges 2/3/4/5/6 (`config/MapRoom3Config.ts`). This is a strategy map topology, not a freely roaming terrain level.

## Explicit requested changes

| User requirement | Difference from supplied game | Completion test |
|---|---|---|
| Native UE 5.8 plugin and native UI | ActionScript/Flash runtime replaced | Compile plugin with UE 5.8; package and launch without Flash, Bun, database or browser. |
| Same package hosts and joins | Original backend is separate web stack | Two Windows PCs: one launches listen server; second joins same build by configured address. |
| Server-local account creation and saves | Original has PostgreSQL/Redis/HTTP auth and recovery paths | Remote registration changes only host files; clients never author balances, account records or saved worlds. |
| Automatic toolbar asset import and maps | New editor tooling | Fresh project, plugin copied, one action imports from fixed plugin SourceAssets path and creates maps/gamemodes without folder chooser. |
| Flat horizontal art under downward camera | Original Flash display list | Ground, buildings and units have correct scale, ordering, pivots, animation and selection. UE custom depth belongs to primitive render components, not UTexture2D. |
| 1,000 autonomous rival bots | Original tribe bases are authored enemies | 1,000 persistent identities independently collect/build/train/attack/defend using player-authoritative rules; count alone is insufficient. |
| 1-hour post-invasion shields | Replaces original damage protection rules | Failed/won invasion outcome applied once; shield expiry persists/reloads correctly and blocks both bots and humans. |
| Hourly autosave with logout retained in memory | New host save lifecycle | Player logout does not discard state; all bots and players included in one consistent snapshot; host shutdown behavior explicit. |
| Default 100 concurrent players | New listen server scale target | Cap enforced at login/connection; 100-client load/soak test, latency, bandwidth, actor count and save stalls measured. |
| Separate RP ladder from zero | New competitive progression | Stable server-authoritative ordering for players and bots; idempotent award/deduct on resolved PvP; floor policy configurable/documented. |

## Parity work that data import alone cannot complete

Full parity requires tested implementation of: building prerequisites/quantities/footprints, construction/upgrade/repair/fortification queues, resource caps and boosts, bank-all and terrain effects; monster unlock/research/hatch/housing/heal/bunker/juicer; targeting/pathfinding/wall destruction and unit-specific abilities; siege, catapult, champions and Inferno; all map variants, tribes, outposts and relocation; tutorial, quests, achievements, events, rewards, social systems and player communication; original animation states, SFX, music, tooltips, overlays and localization.

An import of texture files plus a generic building/attack loop is an initial implementation, not a 100% recreation. Every catalog record must be classified implemented/unsupported/disabled with a reason, and each special creature must have behavior tests. Final acceptance needs comparison against the original client, packaged UE tests, recovery tests and 100-player plus 1,000-bot soak tests.

## Trust-model findings for the remake

The supplied original `services/base/updateResources.ts` applies deltas sent by a client and even accepts resource maximum fields; combat and production are substantially simulated in ActionScript and sent through save handlers. **Do not reproduce that trust boundary.** UE clients should request actions only; server calculates/validates costs, prerequisite state, placement, elapsed time, loot, damage, wins, RP and shield expiry. Private password verifier/account data must never replicate. Retain disconnected yards in host memory until snapshot, and use atomic versioned writes with recoverable backup rather than overwriting the only valid save. Account registration may be saved immediately for durability while the gameplay world follows the requested one-hour cadence; document that distinction if used.

## Overworld record index

The following is an inventory of all 140 source slots. Names are localization keys, not necessarily display labels. Presence here does not imply availability or that a UE runtime has implemented its special behavior.

| ID | Source name/key | Type | HP levels |
|---:|---|---|---:|
| 1 | #b_twigsnapper# | resource | 10 |
| 2 | #b_pebbleshiner# | resource | 10 |
| 3 | #b_puttysquisher# | resource | 10 |
| 4 | #b_goofactory# | resource | 10 |
| 5 | #b_flinger# | special | 5 |
| 6 | #b_storagesilo# | special | 10 |
| 7 | #b_mushroom# | mushroom | 1 |
| 8 | #b_monsterlocker# | special | 4 |
| 9 | #b_monsterjuicer# | special | 3 |
| 10 | #b_yardplanner# | special | 1 |
| 11 | #b_maproom# | special | 3 |
| 12 | #b_generalstore# | special | 1 |
| 13 | #b_hatchery# | special | 3 |
| 14 | #b_townhall# | special | 10 |
| 15 | #b_housing# | special | 10 |
| 16 | #b_hcc# | special | 1 |
| 17 | #b_woodenblock# | wall | 5 |
| 18 | #b_stoneblock# | wall | 1 |
| 19 | #b_wildmonsterbaiter# | special | 7 |
| 20 | #b_cannontower# | tower | 10 |
| 21 | #b_snipertower# | tower | 10 |
| 22 | #b_monsterbunker# | tower | 5 |
| 23 | #b_lasertower# | tower | 8 |
| 24 | #b_boobytrap# | trap | 1 |
| 25 | #b_teslatower# | tower | 8 |
| 26 | #b_monsteracademy# | special | 5 |
| 27 | #b_trojanhorse# | enemy | 1 |
| 28 | #b_americanflag# | decoration | 1 |
| 29 | #b_britishflag# | decoration | 1 |
| 30 | #b_australianflag# | decoration | 1 |
| 31 | #b_brazilianflag# | decoration | 1 |
| 32 | #b_europeanflag# | decoration | 1 |
| 33 | #b_frenchflag# | decoration | 1 |
| 34 | #b_indonesianflag# | decoration | 1 |
| 35 | #b_italianflag# | decoration | 1 |
| 36 | #b_malaysianflag# | decoration | 1 |
| 37 | #b_dutchflag# | decoration | 1 |
| 38 | #b_newzealandflag# | decoration | 1 |
| 39 | #b_norwegianflag# | decoration | 1 |
| 40 | #b_polishflag# | decoration | 1 |
| 41 | #b_swedishflag# | decoration | 1 |
| 42 | #b_turkishflag# | decoration | 1 |
| 43 | #b_canadianflag# | decoration | 1 |
| 44 | #b_danishflag# | decoration | 1 |
| 45 | #b_germanflag# | decoration | 1 |
| 46 | #b_filipinoflag# | decoration | 1 |
| 47 | #b_singaporeanflag# | decoration | 1 |
| 48 | #b_austrianflag# | decoration | 1 |
| 49 | #b_pirateflag# | decoration | 1 |
| 50 | #b_peaceflag# | decoration | 1 |
| 51 | #b_catapult# | special | 4 |
| 52 | Simple Sign | taunt | 1 |
| 53 | hwn_pumpkin | immovable | 1 |
| 54 | hwn_massivepumpkin | immovable | 1 |
| 55 | bdg_acorn | decoration | 1 |
| 56 | bdg_beehive | decoration | 1 |
| 57 | bdg_birdhous | decoration | 1 |
| 58 | bdg_tent | decoration | 1 |
| 59 | bdg_jax | decoration | 1 |
| 60 | bdg_redgnome | decoration | 1 |
| 61 | bdg_bluegnome | decoration | 1 |
| 62 | bdg_greengnome | decoration | 1 |
| 63 | bdg_hammock | decoration | 1 |
| 64 | bdg_lawnchair | decoration | 1 |
| 65 | bdg_outhouse | decoration | 1 |
| 66 | bdg_pinecone | decoration | 1 |
| 67 | bdg_rock | decoration | 1 |
| 68 | bdg_scaleelectric | decoration | 1 |
| 69 | bdg_scarecrow | decoration | 1 |
| 70 | bdg_sundial | decoration | 1 |
| 71 | bdg_tikitorch | decoration | 1 |
| 72 | bdg_walnut | decoration | 1 |
| 73 | bdg_tombstone | decoration | 1 |
| 74 | bdg_pokeyhead | decoration | 1 |
| 75 | bdg_octohead | decoration | 1 |
| 76 | bdg_bolthead | decoration | 1 |
| 77 | bdg_banditohead | decoration | 1 |
| 78 | bdg_brainhead | decoration | 1 |
| 79 | bdg_crabhead | decoration | 1 |
| 80 | bdg_davehead | decoration | 1 |
| 81 | bdg_eyerahead | decoration | 1 |
| 82 | bdg_fanghead | decoration | 1 |
| 83 | bdg_finkhead | decoration | 1 |
| 84 | bdg_ichihead | decoration | 1 |
| 85 | bdg_projectxhead | decoration | 1 |
| 86 | bdg_blackberrybush | decoration | 1 |
| 87 | bdg_bonsaitree | decoration | 1 |
| 88 | bdg_cactus | decoration | 1 |
| 89 | bdg_flytrap | decoration | 1 |
| 90 | bdg_thorns | decoration | 1 |
| 91 | bdg_pinkflowers | decoration | 1 |
| 92 | bdg_purpleflowers | decoration | 1 |
| 93 | bdg_redflowers | decoration | 1 |
| 94 | bdg_whiteflowers | decoration | 1 |
| 95 | bdg_yellowflowers | decoration | 1 |
| 96 | bdg_baseballstatue | decoration | 1 |
| 97 | bdg_footballstatue | decoration | 1 |
| 98 | bdg_soccerstatue | decoration | 1 |
| 99 | bdg_libertystatue | decoration | 1 |
| 100 | bdg_eiffelstatue | decoration | 1 |
| 101 | bdg_bigben | decoration | 1 |
| 102 | bdg_pool | decoration | 1 |
| 103 | bdg_pond | decoration | 1 |
| 104 | bdg_zengarden | decoration | 1 |
| 105 | bdg_fountain | decoration | 1 |
| 106 | bdg_teagarden | decoration | 1 |
| 107 | bdg_monsterskull | decoration | 1 |
| 108 | bdg_rubikunsolved | decoration | 1 |
| 109 | bdg_rubiksolved | decoration | 1 |
| 110 | bdg_halloween | decoration | 1 |
| 111 | bdg_halloween_small | decoration | 1 |
| 112 | #b_outpost# | special | 0 |
| 113 | #b_radio# | special | 1 |
| 114 | #b_monstercage# | cage | 1 |
| 115 | #b_flaktower# | tower | 8 |
| 116 | #b_monsterlab# | special | 4 |
| 117 | #b_heavytrap# | trap | 1 |
| 118 | #b_railguntower# | tower | 8 |
| 119 | #b_championchamber# | special | 1 |
| 120 | bdg_biggulp | decoration | 1 |
| 121 | bdg_wmitotem1 | decoration | 6 |
| 122 | placeholder | placeholder | 1 |
| 123 | placeholder | placeholder | 1 |
| 124 | placeholder | placeholder | 1 |
| 125 | placeholder | placeholder | 1 |
| 126 | placeholder | placeholder | 1 |
| 127 | #b_infernoentrance# | enemy | 5 |
| 128 | #b_housingbunker# | tower | 0 |
| 129 | #bi_quaketower# | tower | 8 |
| 130 | #b_icannontower# | tower | 0 |
| 131 | bdg_wmi2totem | decoration | 6 |
| 132 | #bi_magmatower# | tower | 6 |
| 133 | #b_siegefactory# | special | 1 |
| 134 | #b_siegeworks# | special | 10 |
| 135 | bdg_dave_trophy | decoration | 1 |
| 136 | #bi_spurtzcannon# | tower | 5 |
| 137 | #bi_blackspurtzcannon# | tower | 5 |
| 138 | #b_stronghold# | tower | 3 |
| 139 | #b_resourceop# | cage | 1 |
| 140 | #b_opdefender# | special | 5 |

A machine-readable source summary is in `source-building-summary.json`; it is a review aid, not the canonical parser for Unreal import.

## Initial Unreal implementation boundary (contract review)

The initial architecture plan introduced editor import/setup, a native main menu, host-account operations, server-authoritative yards, a basic resource/build/train/invasion loop, server-local snapshots, bots and an RP ladder. That initial plan described intent, not proof that these are compiled, running or complete. Unreal Engine is not available in this execution workspace; packaged runtime and editor acceptance remain required on UE 5.8.

The contract deliberately selects an initial core loop. The following original behavior is outside its current schema or operations and **must remain listed as missing until implemented and tested**:

| Original behavior | Contract gap | Why a generic substitute is insufficient |
|---|---|---|
| Per-level creature research and unlock trees | Monster catalog projects a single HP/damage/speed/hatch cost; commands omit research/unlock. | Six-level research balance, unlock dependencies and the academy/locker lifecycle are lost. |
| Full build/upgrade/fortification/repair state machines | Snapshot has level and readyAt; no explicit fortify queue/property. | Damage, interruption, cancellation, refunds, repair progress and fortification modifiers need separate transitions. |
| Precise placement footprint and pathing costs | Basic x/y/cell size/size fields. | Original classes use rectangles and nested pathing costs; large structures and walls have different occupied areas. |
| All resource mechanics | Simplified production/capacity fields. | Exact cycle time, local harvester storage, storage silos, bank XP, damage penalties, terrain, boosts and Shiny need distinct rules. |
| Original monster roles and combat | Basic damage/range/movement simulation. | Flying, tunneling, splitting, healing, resurrection, target groups, shields, splash, attack rate and projectiles cannot be inferred from damage alone. |
| Monster housing, healing, bunker assignment and juicing | Train command and army counts only. | Housing space differs by monster level; bunker garrisons and wounded monster recovery are separate inventories. |
| Champions and heroes | No champion model/commands. | Selection, naming, feeding/evolution, cages/chambers, guardian states and abilities are a different progression system. |
| Siege/catapult/defensive powers | No command families. | Resource launches, siege factory/lab, Jars/Vacuum and tower interactions have unique combat rules. |
| Inferno/descent and Inferno outposts | Initial yard catalog is overworld. | Separate resources/building/monster variants, map and descent content require complete alternate state. |
| Three map generations, outposts, range/takeover/relocation | A single OpenWorld level plus inspectable yards. | A generated UE level is not equivalent to the original MapRoom campaign/hex map progression. |
| Authored PvE tribes and special events | Autonomous rivals are new player-like bots. | Tribe/event layouts, objectives, wave scripts and rewards are distinct from player raids. |
| Tutorial, missions, achievements, empire XP | RP and starter resources supplied; no quest/profile state. | RP is requested additional competitive scoring, not replacement for source progression. |
| Alliances, friends, chat, mail, gifts, truces | No social commands/state. | These are visible original game systems with durable histories and permissions. |
| Store, Shiny, subscriptions, timed boosts and reward inventory | Four regular resources and combat rewards only. | Original economy has multiple inventories and expiring effects; payment integrations should not be reintroduced implicitly. |
| Full visual/audio parity | Imported textures displayed as flat planes. | Sprite-sheet UVs, directional animation, pivots, state transitions, shadows, effects, MP3/SWF media and localization need explicit conversion/runtime support. |
| Complete native UI parity | Main menu/dashboard/ladder/build/train flow. | Full catalog categories, tooltips, prerequisites, timers, controls, selection modes and all original dialogs remain work. |

The new default starter resource setting of 5,000 per regular resource is a convenience balance change from the original's zero-plus-tutorial startup. The new RP win/loss defaults of +25/−15 and clamping at zero are implementation choices because the request specifies gain/loss but no quantities. They should be visible settings rather than represented as original rules.

## Independent acceptance matrix

Status for all Unreal-only rows is **not run in this workspace**. Code review, schema checks, parser tests or file presence never substitute for these observed tests. Use an isolated test profile; retain host and remote client logs with credentials redacted. Test the same packaged Windows x64 game on separate PCs, as well as editor behavior. Include both loopback/LAN and the intended WAN path.

| ID | Test | Procedure and expected result | Required evidence |
|---|---|---|---|
| E01 | Clean UE 5.8 compile | Build Development Editor and Shipping Game from a clean extracted package; no stale Binaries/Intermediate. | UBT/UHT logs, exact engine version and compiler version. |
| E02 | Fresh automatic setup | Create a new C++ project, copy plugin, open editor; setup runs with fixed plugin SourceAssets path and no directory prompt. | Editor log and actual maps/material/assets in Content Browser. |
| E03 | Toolbar completeness | Invoke Import Assets, Generate Maps, Validate Setup individually; each reports count/errors and is independently usable. | Logs of actions and generated assets. |
| E04 | Idempotence | Run setup twice; edit existing map and rerun; paths stable, no duplicate assets, edited map preserved. | Before/after package inventory and edited marker. |
| E05 | Asset failure visibility | Remove one required texture, corrupt one image; validator reports exact missing/corrupt paths and does not claim success. | Validation report; runtime error state. |
| E06 | Packaging content | Cook Shipping without SourceAssets directory beside EXE; all referenced art and JSON catalog resolve from packaged content. | Cook log, launch log and representative rendered buildings/monsters. |
| E07 | Custom depth correctness | Inspect every generated render component template and a newly spawned building/monster at runtime; Render CustomDepth true and stencil enabled. | Component inspection and custom-depth buffer capture. |
| E08 | No external server stack | Host and join without Node/Bun, PostgreSQL, Redis, XAMPP, browser runtime or online account service installed. | Process/network inspection and working session. |
| N01 | Host-and-play same package | Host from native main menu, create account and enter own yard; second machine joins identical package and creates different account. | Both screens, same package hash, host-only records. |
| N02 | Server address and port | Change project settings, package, connect to that address/port; invalid host yields actionable error and return-to-menu. | Connection logs and recovered UI. |
| N03 | Capacity | Set cap 2; two authenticated users permitted, third rejected; disconnect one, new one accepted. Repeat 100-default configuration. | Connection count and rejection messages. |
| N04 | Unauthorized RPC | Connect but do not authenticate; send build/train/invade/ladder/snapshot requests; no private state or mutation. | Host state digest before/after and error response. |
| N05 | Identity spoofing | Supply another account's UUID in JSON and modified RPC fields; server derives identity exclusively from authenticated connection. | Attacker denied; victim save unchanged. |
| N06 | RPC bounds | Burst malformed/oversized/unknown operations and invalid JSON; throttle without server crash or stalled legitimate users. | Rate logs, memory use, second client responsiveness. |
| A01 | Registration durability | Register account then immediately disconnect/restart host before hourly gameplay save; account remains usable. | Account exists once; no plaintext password; no client account file. |
| A02 | Duplicate and canonical usernames | Register mixed-case duplicate, empty, very long, whitespace/control-character names; apply documented normalization and limits. | Rejections with no partial yard/account creation. |
| A03 | Password verification | Correct password succeeds; incorrect fails; salted verifier differs for equal passwords on two accounts. | Redacted verifier metadata, no passwords in logs. |
| A04 | Owned-controller login | Attempt login through an unowned controller; disconnect during pending verification; reconnect and retry. | No session assigned to another connection; no stale callback commits. |
| A05 | Host-local accounts | Create an account on one host, then join a fresh independent host with the same credentials. | The second host has no account until explicitly registered there. |
| A06 | Concurrent login and disconnect | Same account logs in twice; policy enforced; disconnect removes session but preserves yard. | Active session table and retained yard. |
| P01 | Hourly save | Configure short interval for test, make player and bot changes, wait snapshot; restore default 3,600 seconds. | Snapshot timestamps and reloaded state equality. |
| P02 | Logout memory retention | Modify player yard, logout before interval, inspect host memory, reconnect; changes retained until snapshot. | Before/logout/reconnect state and hourly snapshot. |
| P03 | Atomic save recovery | Terminate host during temporary write/replace; restart loads last valid primary or backup without silent reset. | Fault-injection logs and matched checkpoint. |
| P04 | Corrupt save handling | Truncate primary and backup; host refuses authoritative startup with repair instructions instead of overwriting. | Original bytes unchanged after failed launch. |
| P05 | Shutdown behavior | Graceful host exit writes full state if SaveOnShutdown enabled; forced kill respects last successful snapshot. | Save timestamps; documented maximum one-hour exposure. |
| P06 | Clock handling | Host wall clock rollback/forward, save/restart near timer expiry; no negative rewards, duplicate completions or unlimited production. | Timer, balance and shield logs. |
| G01 | Costs/prerequisites | Try legal and illegal builds at insufficient resources/Town Hall levels/quantity limits; only valid request changes state. | Exact catalog-derived expected balances. |
| G02 | Placement/move | Bounds, overlaps, edge cells, fractional/huge coordinates and malicious IDs rejected; legal move persists. | Layout snapshot and unchanged balances on rejection. |
| G03 | Queue lifecycle | Start build/upgrade/train, disconnect and reconnect before/after due time; one completion only and correct deduction. | Before/after inventory and timer states. |
| G04 | Resource caps | Run elapsed production at cap and near integer limits; collect repeatedly; no duplication or overflow. | Per-resource total production accounting. |
| G05 | Server combat result | Client submits fake win/damage/RP/loot fields; rejected or ignored; actual server battle determines outcome. | Server combat trace and before/after totals. |
| G06 | Concurrent invasion | Two attackers target same yard simultaneously; at most one acquires raid lock; no double loot or shield overwrite. | Both response traces and defender state. |
| G07 | Shield coverage | Human/bot defender on win, loss, retreat and disconnect gets documented one-hour post-invasion shield; blocked until exact expiry. | Persisted shield timestamp and boundary test. |
| G08 | RP accounting | Every human-vs-human, human-vs-bot, bot-vs-human and bot-vs-bot result grants/deducts once; all start zero; floor setting honored. | Transaction traces and ladder snapshots. |
| G09 | Ladder pagination | More than 1,050 entries, ties, renamed account and offline accounts; no duplicates/omissions, stable tie-breaking and correct ranks. | Whole paged ladder compared to host sort. |
| B01 | Bot identities | New server creates exactly configured 1,000 rivals once; save/restart does not duplicate or rename them. | ID set and count before/after restart. |
| B02 | Bot rules equality | Bots cannot buy without resources, exceed storage/quantity/housing caps, bypass shields or invent wins. | Bot command audit through same validation paths as humans. |
| B03 | Bot progress | Over time rival population visibly collects/builds/trains/attacks/defends; decisions distributed across identities, no starved tail. | Per-bot action counters and state deltas. |
| B04 | Bot persistence | Stop/restart with queues, damaged structures and active shields; simulation resumes consistently. | Full world checkpoint comparison. |
| V01 | Flat-world orientation | Ground/buildings/monsters lie in XY plane; orthographic camera looks straight down; no mirrored/upside-down or missing face. | Rendered representative yard. |
| V02 | Asset source fidelity | Compare original and UE images for Town Hall, collectors, each defense type, Pokey and larger units; correct transparency and pivot. | Side-by-side reference captures and reviewed deltas. |
| V03 | Interaction | Pan/zoom, click placement, selection, UI focus, attack deployment and return-home work at multiple resolutions/DPI. | Input test recording and readable UI. |
| V04 | Error/loading states | Connecting, failed login, missing art, invalid command and server disconnect show clear native UI state; no black screen. | Captures for each state. |
| S01 | 100-client load | 100 authenticated simultaneous players plus 1,000 bots; representative builds/combat/ladder requests. | CPU/RAM, frame/server tick percentiles, bandwidth and latency. |
| S02 | Multi-hour soak | At least four hourly snapshots with active battles/joins/logouts; no monotonic leak, stalls, lost state or duplicated rewards. | Time-series metrics and checkpoint audits. |
| S03 | Network impairment | Test packet loss/latency/disconnect during authentication, command response and battle; actions exactly once or safely rejected. | Network emulation parameters and invariant checks. |

## Original parity sign-off process

1. Create a catalog crosswalk keyed by yard variant/building ID/level and creature ID/research level. Every original cost, timer, stat, prerequisite and render variant has an implemented, intentionally changed, disabled or missing disposition.
2. Use the original client as behavioral reference. Record construction, harvesting, training, targeting and special abilities on deterministic yard layouts. Reproduce identical starting state in Unreal; compare state transitions and visual timing, not only final damage totals.
3. For each subsystem absent from the initial contract, implement it behind explicit server commands and save-version migration before presenting it as available. Never use a cosmetic menu item as evidence of working gameplay.
4. Validate data-driven rules with offline unit tests, then verify server ownership and persistence in engine, then package and repeat on separate computers. Keep these test levels distinct in release notes.
5. Claim full parity only when the crosswalk is complete and all relevant acceptance rows have observed evidence. Claim production readiness only after engine build, security review, recovery tests and actual target-load soak pass.

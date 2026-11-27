# Yard residents and combat presentation — source preview 0.1.6

The resident and combat work introduced in 0.1.3 addresses trained monsters roaming the yard, troops stopping too far from buildings, missing attack presentation and the hard-to-see deployment cursor. Version 0.1.5 corrected sprite-atlas initialization and sampling. Version 0.1.6 adds the original unpowered Eye-Ra blast, preferred-target categories and damaged-tower firepower described below. These are source implementations with offline checks; Unreal Engine compilation, rendering and multiplayer execution of 0.1.6 have not been run here.

## Yard residents

The host selects up to 50 residents from the viewed yard's ready army. Selection takes one of each available species before assigning further residents, never exceeds an owned ready count, excludes monsters still hatching and retains stable species/ordinal identities. This is a visual sample of the army, not extra gameplay units. It does not consume, duplicate, train or save monsters.

The client animates the sample locally using bounded pathfinding around the yard's buildings. Residents pause between walks, remain inside the yard and are rebuilt when changing yards. Layout changes invalidate paths; a newly placed building cannot leave a resident trapped inside its footprint. Residents are hidden while that yard is an active battlefield, so the cosmetic sample cannot be mistaken for deployed combat troops. Ordinary snapshots retain their identities and movement instead of resetting every update.

Project Settings → Plugins → Backyard Monsters NXT → World exposes `MaxVisibleYardMonsters`, default 50, with an enforced maximum of 50. This setting is independent of the persistent rival bot population.

## Combat authority and timing

Combat uses the original building collision rectangles rather than the larger artwork dimensions. Construction keeps its separate placement footprint. Ground troops approach through navigable cells, and melee attacks require contact with the combat bounds. Ranged monsters retain their configured range. Damage is applied on individual server attack cooldowns, with authoritative target, facing, motion state and attack events sent for presentation. Clients cannot submit damage or outcomes.

The supplied refitted ActionScript uses a 72 Hz simulation clock and a 40 Hz display clock. The asset pipeline records these separately. Monster cooldowns, movement conversion and tower fire intervals derive from the supplied source; visual timelines use their corresponding source clock. These values describe the supplied refitted source, not an independent measurement of historical Flash gameplay.

Active battles update more frequently than peaceful yards:

| Setting | Default | Meaning |
| --- | --- | --- |
| `CombatSimulationStepSeconds` | 0.1 s | Host battle update interval, clamped to 0.05–0.1 s. |
| `BattleSnapshotIntervalSeconds` | 0.2 s | Snapshot interval for a player viewing an active battle, clamped to 0.1–1 s. |
| `SimulationStepSeconds` | 1 s | Existing ordinary simulation/bot scheduling setting. |

Peaceful snapshots remain at two seconds. Combat timestamps retain milliseconds, and the renderer interpolates toward received positions over a bounded interval. Smaller snapshot intervals increase network traffic; the requested 100-player capacity has not been profiled or verified.

## Original combat rules added in 0.1.6

Eye-Ra now reaches a target, applies one area blast and consumes itself. The unpowered source blast has a 60-source-pixel radius and squared-distance damage falloff, with the original rounding and anchor offsets. It damages each eligible nearby building once, including nearby structures other than the selected target. The normal wall-specialist double-damage bonus does not multiply this explosion. A saved dead Eye-Ra cannot repeat the blast after reload. Building destruction uses the same host-side cleanup for direct hits and explosions, including destruction of a Hatchery's queue.

Monster target preferences are now searched as separate categories. A wall specialist selects a wall while one remains, a resource specialist prioritizes lootable building types, and a tower specialist prioritizes defenses. Only after its preferred category is exhausted does the monster consider ordinary fallback buildings. Ground-space distance uses the original building-middle rule. Pathfinding can still redirect a blocked ground attacker to an obstacle. The supplied game's two-candidate path arbitration and partially looted target flags remain incomplete; this is not a claim of complete original targeting parity.

Cannon, Sniper, Laser and Tesla damage now falls with tower health according to the original per-shot multiplier `0.5 + 0.5 × health / maxHealth`, followed by integer truncation. The requested host damage setting then scales that shot. At full health a level-one Cannon deals its original 20 damage; at half health it deals 15. Living towers can fire during repairs, while construction and upgrade countdowns prevent firing. Existing targeting filters, source cooldowns and Tesla charge/burst/cooling state remain in use.

When an Eye-Ra blast actually damages something, the renderer places the original `ParticleScorch1` image on the ground. Marks remain after temporary battle effects end, expire after the source two-day interval, and are limited to 128 per local board. They are cosmetic records of events observed in the current yard: changing yards clears them, and revisiting or reloading a yard does not replay or restore them. See `COMBAT_PARITY.md` for source references, tests and remaining differences.

## Original frames and added motion

Facing columns are directions, not animation frames. The pipeline distinguishes explicit idle/walk/attack/death row selections and records when a source sequence is unavailable. Monsters with real row animations use those source rows and their frame divisors. Several early monsters, including Pokey, have only a single pose for each direction in the supplied art. Their visible attack motion is an NXT presentation enhancement using that original pose; it is not a claim that additional original attack frames were recovered.

Frame geometry uses exact original image dimensions, including padded sheets, and samples within the selected frame's texel centres. A texture that is still preparing retries initialization instead of caching a bad layout; each monster retains its own material state. These 0.1.5 fixes remain in place. See `SPRITE_ATLAS_FIX.md`.

Tower presentation is assembled from original shadow, top and animated layers using the source offsets and pivots. Directional tower frames and timed firing sequences have distinct metadata. Server attack/death/destruction events drive the corresponding presentation; short-lived completed-battle presentation preserves the final hit even after gameplay resolves the battle. These effects do not keep a finished invasion open for deployment. Tesla uses separate charge, bounded pulse burst and cooling phases, including save/reload continuity for remaining shots and cooldowns. A resumed visual cycle can restart within its current phase. Original defense sounds are cached and rate limited; `bCombatSoundsEnabled` and `CombatSoundVolume` control them in Project Settings. Projectile travel is presentation driven by server events; damage remains authoritative at attack resolution. Full original flying altitude/shadows and glow filters are still incomplete.

## Deployment cursor

The cursor follows the same logical cell used by deployment requests. Its diamond projection matches the yard, with a strong contrasting outline and a colored fill. Valid and invalid cells have different colors, and the invasion perimeter is shown while the deployment tool is active. Rendering uses explicit overlay ordering so the marker stays visible over grass and units. Host validation remains authoritative if the yard changes between preview and click.

Opening the `.uproject` normally detects a changed bundled asset/catalog revision and runs the existing automatic importer. New animation and marker textures therefore join the same plugin content paths. The final Default-compression/sRGB texture pass still covers both new and cached imports.

## Verification and remaining limits

The package includes native tests for resident selection and wandering, melee approach, blocked paths, individual attacks, cooldown save compatibility, tower range/targeting, terminal events and the public resident snapshot boundary. These tests still require Unreal Editor execution. The actual resident helper was separately compiled and exercised in a small C++ math/container harness; that checks its algorithm, not Unreal API or rendering compatibility. Asset pipeline tests verify source-derived atlas bounds, animation metadata and selection masks.

Six additional native tests cover original target categories, blast/tower formulas, Eye-Ra area damage and reload continuity, preferred-target fallback, damaged-tower cooldowns, repair/upgrade firing gates and elapsed repair settlement at battle end. The actual combat-rule helper and two of those test bodies passed 220,112 assertions in an offline C++ adapter. The extracted final battle method also passed the existing combat regression cases and new explosion, destruction-hook, targeting, damaged-tower and repair/upgrade scenarios. That combat harness stubs economy accrual; economy algorithms have separate integration checks. The four new full-simulation tests have not been run in Unreal.

Required engine checks include a clean UE 5.8 build, fresh automatic import, two clients watching the same battle, all available monster types, tower facing/firing, final-hit presentation, deployment at every edge and zoom level, and reload during a battle. Validate the visible result at multiple resolutions before calling this production-ready.

This update does not finish every original ability or game system. Healers, champions, Map Room 3 persistent garrisons, siege mechanics and Inferno still have outstanding behavior work documented in `STATUS.md`. The later v0.2.0 territory implementation is described separately in `MAP_ROOM.md`. Source frame availability and generic attack presentation are distinct from full original combat parity.

# Original combat rules — source preview 0.1.8

The combat rules introduced in source preview 0.1.6 remain in the host simulation. Source preview 0.1.7 adds independent building work/repair state and player experience to those interactions. It preserves the flat original artwork, resident sprite fixes, source clocks, path/line-of-sight checks, individual attack cooldowns and Tesla phase persistence. It does not establish complete original-game parity or a verified Unreal build.

Source preview 0.1.8 adds real ordinary Bunker defenders, troop retaliation, original dispatch/door timing, and distinct offensive/defensive Eye-Ra creature interactions. See `MONSTER_BUNKER.md` for exact branches and remaining adaptations.

## Implemented rules

| Rule | Current behavior | Supplied source |
| --- | --- | --- |
| Unpowered Eye-Ra | One 60-pixel radial blast at the ready attack, squared-distance falloff, rounded positive damage, then self-destruction. Every eligible nearby building is processed once. | `CREATURELOCKER.as:200–225`; `com/monsters/monsters/creeps/CreepBase.as:761–835,883–899` |
| Preferred building targets | Search walls, lootable building types or defenses first for the corresponding source target group; use ordinary fallback buildings only after that category is exhausted. | `com/monsters/monsters/MonsterBase.as:991–1090` |
| Target distance | Ground-space distance to the building's source middle, minus its half-height. Direct general targeting excludes walls; pathfinding may still attack an obstruction. | `MonsterBase.as:995–1008`; `BFOUNDATION.as:1927–1948` |
| Ordinary specialist damage | Wall specialists retain the original 2× damage against walls; tower specialists retain 2× damage against towers. Eye-Ra's explosion returns before this multiplier is applied. | `CreepBase.as:883–899` |
| Damaged tower firepower | Source shot damage is `floor(base × (0.5 + 0.5 × health / maxHealth))`. The existing NXT host damage setting scales that result. Dead towers do not fire. | `BUILDING20.as:27–43`, `BUILDING21.as:42–57`, `BUILDING23.as:47–65`, `BUILDING25.as:125–145` |
| Tower repair gate | Living, completed towers can fire during repairs if no construction/upgrade job remains. A paused upgrade still prevents firing even though its running deadline is zero. | `BTOWER.as:159–160`; `BUILDING_LIFECYCLE.md`. |
| Destruction cleanup | Direct and explosion kills share cleanup; destroying a Hatchery invokes its queue-destruction/refund rule once. | Integrated with the Hatchery lifecycle port; see `HATCHERY_PRODUCTION.md`. |
| Damaged paid work | Any damage pauses construction/upgrading without discarding its target level, identity or remaining work. Repair runs independently; work resumes at full health after repair ends. | `BFOUNDATION.as:1360–1396`; `BUILDING_LIFECYCLE.md`. |
| Destroyed paid work | Lethal damage ends active repair and preserves construction. A pending upgrade keeps its identity and loses half its completed progress, capped at eight hours, using source integer rounding. Destruction never promotes an unfinished building. | `BFOUNDATION.as:499–518,1966–2005`; `BUILDING_LIFECYCLE.md`. |
| Eye-Ra ground mark | A blast with nonzero damage places the original 200 × 100 `ParticleScorch1` artwork under buildings and units. | `CreepBase.explode`; `EFFECTS.Scorch`; `source_client/images/1820_ParticleScorch1_ParticleScorch1.png` |

Source paths in this table are relative to the supplied original `client/scripts` tree, except for the imported image path. The disabled rebalance subtree is not used as the authority for these changes.

The original unpowered explosion compares `PATHING.FromISO(monster) - (5,5)` with each building's anchor. Its `Point.add(middle,middle)` expression discards the returned point. This port preserves that literal active-source behavior rather than silently substituting a centred blast. Radius checks use original ground-space source pixels, not the projected sprite width or the construction placement box.

## Authority and persistence

Only the host chooses targets, applies damage, consumes Eye-Ra and resolves building destruction. Clients receive presentation events. An explosion emits one `monsterExplode`, an `explosionHit` for each damaged building, and the existing destruction/death events as applicable. These events cannot be submitted as damage commands by a client.

The monster's committed zero health is part of the existing host world JSON save state. Reloading an active battle therefore cannot trigger a second explosion from that dead monster. The saved attack cooldowns and Tesla phase fields remain unchanged. Schema 4 adds persistent building job identities/remaining work and player-experience counters; schemas 1–3 remain readable. Account records continue to use the existing native Unreal `.sav` files. No external save or account service is introduced.

Each combat step advances the defender's economy to its damage timestamp before changing building health. Battle completion, expiry and scouting cancellation also settle elapsed repair and hatching work before removing invasion authority. Collectors remain blocked for that interval, so finishing an invasion neither loses eligible progress nor grants production retroactively.

Accepted invasion loot now awards source player experience one-for-one with the resource amounts actually credited to the attacker. Full storage cannot award experience for unaccepted loot. These points are separate from the existing NXT RP result and do not change its win/loss or shield rules. Construction/upgrade completion awards follow the retained job exactly once; repair and destruction themselves grant no direct experience award. See `PLAYER_EXPERIENCE.md`.

Scorch marks are local presentation only. Each board keeps at most 128, with a 172,800-second lifetime, and retains them when temporary battle effects end. Changing yards clears them. They are not included in the host save, reconstructed on a later yard visit, or guaranteed to appear for a client that did not observe the event. Full persistent ground-effect parity remains outstanding.

## Validation

The native combat tests introduced with 0.1.6 are:

- `BackyardMMO.Combat.OriginalTargetTiers`
- `BackyardMMO.Combat.OriginalBlastAndTowerDamage`
- `BackyardMMO.Combat.EyeRaSingleBlastAndReload`
- `BackyardMMO.Combat.PreferredTargetAuthority`
- `BackyardMMO.Combat.DamagedTowerAuthority`
- `BackyardMMO.Combat.BattleEndAccrual`

In the 0.1.6 combat validation pass, the actual pure helper and its two native test bodies compiled with C++17 and passed **220,112 assertions**, including 50,000 randomized cases, using lightweight Unreal type adapters. That release's extracted merged `TickBattle` method also compiled and passed the previous contact/path/flyer/Tesla checks plus new area-blast, self-death, event-count, persisted-dead-state, Hatchery destruction-hook, preferred-target, damaged-Cannon, repair-firing and upgrade-gate cases. This combat adapter stubs the economy call; the actual economy algorithms have separate integration checks. The adapters do not reproduce the engine, actual disk saves or networking. Current reruns and additional lifecycle/experience checks are recorded in the release validation report.

The four native authority integration tests exercise the real simulation and bundled catalog when run in Unreal, including elapsed repair and blocked collector production when a contested or scouting invasion ends. The 0.1.7 lifecycle suite adds real invasion/deployment lethal-damage cases that preserve pending construction/upgrades; the experience suite covers source award formulas. See `BUILDING_LIFECYCLE.md`, `PLAYER_EXPERIENCE.md` and the release validation report for that additional coverage. Native tests are included but have **not** been executed here. Neither UHT/Unreal Build Tool nor target-engine rendering or multiplayer verification was available for this update.

## Remaining original combat differences

- Monster Lab power-ups, including Eye-Ra's jumping airburst, are not implemented by this update.
- Eye-Ra damage against bunker defenders and guardians awaits those systems; the implemented blast currently resolves against buildings.
- The source's path comparison between two preferred candidates, partial-loot target invalidation, and all retargeting subtleties remain incomplete.
- Full healer, splitting, burrowing, resurrection, champion, siege and other species-specific behavior remains outstanding.
- Projectile flight is still presentation after authoritative attack resolution. Original delayed impact, projectile interception and exact splash falloff are not yet reproduced.
- Tower overdrive, fortification/armor effects and every specialized defense rule remain outstanding.
- Complete source visual effects, sounds, altitude/shadows, scorch persistence and screen parity still require work and engine comparison.

A target-editor regression should include several Eye-Ra units, mixed walls and nearby structures, a blast that causes no damage, destruction of an occupied Hatchery, an active-battle save/reload, preferred targets at different distances, damaged towers, and two clients viewing the same invasion. Confirm the gameplay results and rendered effects before treating this as production-ready.

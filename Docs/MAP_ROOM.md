# Map Room 3 — source recreation and host adaptations

This update follows the supplied original **Map Room 3 overworld** source. It does not combine Map Room 2's paid takeover system with Map Room 3's automatic territory capture. The native map is separate from the RP Rankings screen. This is source implementation; Unreal build, Slate rendering and remote-client execution require target-engine verification.

## Original world and artwork

The bundled world comes from executing the supplied original `getGeneratedCells` TypeScript dependency graph with its original Alea 1.0.1 and Simplex Noise 4.0.3 versions. Its 500 × 500 odd-row hex grid contains 10,625 resource sites, 765 strongholds, 68,340 surrounding fortifications and 23,464 tribal sites before player-home placement. The original generator also supplies 63,131 non-empty terrain cells. Empty coordinates remain valid map ground; the map has no exploration fog.

The native Slate map uses the original terrain, home/outpost/stronghold/fortification icons, relationship lights and range geometry, with a native selection outline and details sidebar. Hex artwork is 104 × 68 source pixels, with a 50-pixel row pitch and a 52-pixel offset on odd rows. The world data is already bundled: Node and the archived Flash server are optional developer conversion inputs, never game dependencies.

Twenty-six original defensive yard templates retain their 8,845 authored buildings, levels and fractional source positions. They are not replaced by the player's starting-yard layout. Building 138 uses its original static stronghold hull and four independently aimed lightning emitters; it has no usable original hull animation sheet. Building 140 uses the original 32-frame relationship-specific top animations.

## Territory rules

| Cell | Original behavior |
| --- | --- |
| Main backyard | Remains its account's home; can be raided but never captured. |
| Resource outpost | Captured or recaptured at 90% damage; adds resource production, storage and attack range. |
| Stronghold | Captured or recaptured at 90% damage; adds attack range and in-range monster/tower damage bonuses. |
| Fortification | Captured or recaptured at 90% damage; protects an adjacent parent owned by the same account. |
| Tribal outpost | Destroyed at 90% damage, never owned; regenerates after three days. |

Damage for territory capture is **health weighted**, truncated to a whole percentage, rather than the number of buildings destroyed. Walls and buildings under initial construction are excluded. The neutral Resource Outpost core is untargetable but its original one health still participates in the denominator. Capturing does not heal every structure or invent a trained army.

| Resource tier | Attack range | Each resource per second | Extra storage per resource |
| --- | ---: | ---: | ---: |
| 10 | 2 | 1 | 175,000 |
| 20 | 3 | 2 | 350,000 |
| 30 | 4 | 5 | 750,000 |
| 40 | 5 | 11 | 2,000,000 |
| 50 | 6 | 23 | 4,000,000 |

Stronghold tiers 30/40/50 provide ranges 10/15/20 and additive 10/20/30% damage bonuses. Those bonuses apply to the owner's attacking monsters and defending towers, not its defensive Bunker monsters. Zero through six same-owner adjacent fortifications give 0/7/13/23/35/50/75% building damage reduction. Neutral sites follow the supplied server's no-neutral-owner armor rule.

Attacks within the union of owned ranges have no travel fee. Beyond it, the host computes the minimum uncovered hex distance and multiplies by the original Town Hall cost. That amount is charged separately in Twigs, Pebbles and Putty; Goo is not charged. Confirmation quotes include the current owner/revision and maximum accepted charge. Sending a client-supplied lower price cannot change the host's calculation.

The main-yard level rule blocks attacks more than eleven experience levels downward, except when both accounts are level 40 or above or the attacker has an original-rule retaliation allowance. A lower-level attack creates one allowance for the stronger defender; an accepted retaliation consumes one. Failed requests do not spend it, and these allowances persist without an invented expiry.

The requested one-hour shield remains for invaded main backyards. Map Room 3 territory does not receive that home shield. Online-player protection concerns the main backyard; an owner's online session does not protect all their outposts.

Captured map sites use their authored structures. They do not permit new building construction or upgrades. Existing movable structures can be repositioned; immobile cores remain fixed. Repairs retain source behavior, including the repair-all quarter-health start. Home yards keep their existing construction, worker, shop, expansion and army systems.

## Entering and using the map

The original MR3 progression route requires Town Hall 6 and an operational level-2 Map Room, with no pending Map Room upgrade. NXT enters this world directly instead of running the original online game's historical MR1-to-MR2 migration service. Once an account enters MR3, damaging its Map Room does not remove its participation or allow it to bypass map attack costs through Rankings.

Drag or use the arrow/WASD keys to scroll the map. The map has the source's two zoom scales, Find Base, coordinate Jump, scouting, bookmarks, selected-site details and a paged owned-territory list. Attacks use a host-priced confirmation. The yard camera and placement controls are suspended while the map or its confirmation is open.

Random relocation on this host has no source fee or cooldown. It finds a legal destination before changing the old home, keeps held outposts and strongholds, and resets the old surrounding fortifications. Friend-directed or cross-server migration is not offered.

The map is supplied in bounded viewport responses through the existing authenticated Unreal connection. The authoritative world, ownership and battle decisions remain on the host. No web service, database server or separate account system is introduced.

## Rival layouts and yard residents

Each rival has a deterministic layout preference derived from its identity, with six planning styles and different placement preferences for central, production, storage and defensive buildings. New rivals place their existing starter inventory through footprint-checked planning. Existing idle, unchanged starter yards receive a one-time layout migration that changes positions only: building IDs, levels, health, resources, troops, mushrooms and paid state are retained. A busy yard or paid job defers that migration. Advanced yards rearrange eligible structures gradually through the normal validated move command.

The local resident display takes its bounded roster from the **displayed yard**, not the visiting account's home summary. Resident identities are qualified by yard, species changes reconcile existing visuals, mismatched roster ownership clears the display, and combat clears ambient walkers. Returning home constructs the home roster again. This hardens the transition reported by the user; the original report was visual, so final confirmation still needs a native client playtest.

Rivals gain access to the map through the same progression gate, attack quote, resource charge and battle rules as players. Captured sites contribute income and bonuses to their owner, and rivals can repair, defend and recapture territory. They remain scheduled simulated accounts rather than 1,000 permanently rendered yard scenes.

## Intentional NXT adaptations and remaining differences

- One shared hosted world accommodates the requested 1,000 rivals plus players, rather than the source's 1,000-account world limit. Home spacing and its seven-cell footprint still constrain placement; cells are not silently overwritten to force a new home.
- Early progression retains the existing pre-MR3 rival battle loop between accounts that have not entered MR3. It cannot be used as a free attack route against MR3 participants.
- The original obsolete attack-price experiment is fixed at its baseline multiplier of 1. Prices are host-checked instead of trusting the legacy client.
- Stronghold range includes the stronghold's own coordinate. This corrects the supplied server's truthiness check that accidentally excluded distance zero. The native host recomputes territory bonuses from current ownership during combat; the original client received those buffs at base load.
- The requested RP ladder is an NXT addition. Player-versus-player or rival-owned territory battles affect the owning accounts; defeating an unowned wild site does not create a ranked account for that site.
- After entering MR3, newly ordered monsters use the active source's threefold hatch cost/time. Already-paid queues retain their original purchase. The source's disabled monster rebalance stays disabled; ordinary health and damage are retained.
- The existing ordinary count-based Housing and Bunker inventory remains. The original MR3 individual-monster persistent health, healing, healing fees and reservation UI are **not implemented** by this map update. Their exact source data and requirements are preserved in `maproom-monsters.json`; data preservation is not a claim of working healing gameplay.
- Alliances, truces, friend relationships, near-friend relocation and historical cross-server world migration are not fabricated. Their social and service dependencies need separate native implementations.
- The original map does not make every special monster ability available automatically. Existing documented combat limitations still apply to territory battles.

## Save compatibility

The existing host account `.sav` and world-save arrangement remain unchanged. World schema 5 has an optional version-1 map extension. Old worlds receive stable map positions in memory; existing paid jobs and player inventories are retained. Owned or changed site state, active battles, bookmarks, map entry, fractional income and retaliation allowances are durable.

Scouting an untouched source layout does not duplicate it in the save. A bounded pristine-site cache can reconstruct it from bundled data. Site buildings use source-relative deltas for health, repairs, moved positions and garrisons; unchanged, fully repaired walls need no duplicate row. Authored fractional positions and intentional source footprint overlaps remain intact, while a player-made move must pass full-footprint validation. The host's existing 128 MiB save limit still applies: an oversized or invalid candidate is refused, preserving the last committed file and current in-memory state. Dirty and owned yards retain their expanded simulation state in memory; compact disk deltas do not establish a bounded-memory soak result. Long-running capacity and save-size behavior still require a real host soak test. In the rare case of an account-registration save failure after its map placement displaced a damaged unowned tribal site, rollback can restore that NPC site to its original template. Player-owned territory and paid player state are never placement candidates.

## Provenance and reproduction

`Content/Data/Original/MapRoom3`, `MapRoom3Art`, and `MapRoom3Monsters` retain the consulted original files and notices. `maproom-rules.json`, `maproom-art.json`, `maproom-building-art.json`, `maproom-monsters.json` and `maproom-world.json` record the normalized data and hashes. The monster comparison verifies all 1,008 active numeric values across 27 server monster definitions against the original client; the blocked looter record is not exposed as a trainable monster or worker.

Optional developer checks:

```sh
python Tools/test_maproom_source.py
python Tools/test_maproom_art.py
python Tools/test_maproom_monsters.py
python Tools/test_maproom_world.py
node Tools/prepare_maproom_world.mjs --check
python Tools/OfflineValidation/run.py
```

The generator roundtrip requires Node 24 or newer. None of these commands is required to open the `.uproject` normally. Current recorded test outcomes and engine-only gates are in `VALIDATION.md`.

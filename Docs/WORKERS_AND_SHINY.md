# Workers, mushrooms and Shiny

This update implements the supplied refitted source's ordinary overworld worker and Shiny economy in the existing host-and-play game. The host owns worker assignments, purchases, mushroom identities and rewards. Accounts remain native Unreal `.sav` data; the existing world and bot snapshot format remains unchanged. No payment service, account backend or external runtime has been added.

## Workers

A yard starts with one worker. The General Store sells four permanent additional workers for **250, 500, 1,000 and 2,000 Shiny**, giving a maximum of five. Workers are characters, not a new building. The original non-main-yard branch allows one worker; the currently implemented NXT player yard is the main-yard branch.

Timed construction and building upgrades reserve an available worker. That worker travels to the job before work progresses. A damaged paid job keeps its assigned worker while its work is paused. Ordinary repairs do not occupy another worker. Original zero-time construction, including the normal shop decorations, does not require a worker.

There is no normal pending-building FIFO: when every worker is busy, a new timed job is rejected before payment. Completion and cancellation release the assignment. A loaded older yard can already contain more paid jobs than the new worker limit. Those existing jobs receive temporary legacy assignments so their paid work is retained; these assignments disappear when the jobs finish or are cancelled. They are not newly purchased workers and do not grant additional permanent capacity.

Worker presentation uses the original facing-pose sheet. Its 30 directional poses are not 30 consecutive walking frames. The source's second sheet row is the Sharper Tools variant, not a second animation. Host-generated routes supply positions and arrival times; the client presents the original poses and the worker hop over occupied ground. The source's `WORKER.Wander()` body is empty, so idle free roaming is not claimed as an original implemented worker behavior.

## General Store and Shiny

Construct the original **General Store, building 12**, and keep it at least half healthy to use the shop. Purchases use the existing local host wallet. A client supplies an action and selection; the host determines ownership, availability, price, balance and capacity. A stale purchase tier is rejected instead of silently charging the next tier.

The new Project Settings value `StartingShiny` defaults to **1,500**. This is the effective value in the supplied source's `server/src/config/GameConfig.ts`, where `devSandbox` is disabled. `getDefaultBaseData.ts` has a **1,000** fallback when that setting is unset/falsy. Neither figure is presented as the historical launch-era balance of every original game version. Existing saves without this economy extension receive the configured initial grant once during migration, and the resulting wallet and purchases are then saved with the yard.

The implemented permanent products are:

| Product | Original code | Successive prices | Effect |
| --- | --- | --- | --- |
| Extra worker | `BEW` | 250, 500, 1,000, 2,000 | One additional worker; five total |
| More Yardage | `ENL` | 50, 100, 150, 200, 250, 300 | Increase both yard dimensions by 10% per tier |
| Improved packing skills | `BIP` | 50 through 500, in steps of 50 | Add 10% of base storage capacity per tier; ten tiers |

Packing is additive: level 10 doubles the applicable base storage capacity. It does not repeatedly multiply the previous capacity by 1.1. The original applies it to all four resource capacities and rounds down the result.

Selected building construction and upgrades also support the source speedups:

| Action | Original code | Eligibility | Price |
| --- | --- | --- | --- |
| Close enough | `SP1` | At most five minutes remain | Free completion |
| Reduce by 1 hour | `SP2` | At least one hour remains | 20 Shiny |
| Reduce by 2 hours | `SP3` | At least two hours remain | 40 Shiny |
| Finish now | `SP4` | More than five minutes remain | Original time-based quote |

The Finish Now quote is the smaller of `ceil(seconds × 20 / 3600)` and `floor(sqrt(seconds × 0.8))`. NXT rounds its continuous remaining countdown up to the next source second for these thresholds and quotes. The host checks the selected yard, building job, purchase revision, balance and displayed maximum price before charging. A duplicate partial speedup cannot charge twice using the same revision. Finishing a job uses the same single XP/completion path and releases its worker, including when the worker is still walking. These building shortcuts do not require a General Store, matching the original selected-building flow.

This SP implementation covers construction and building upgrades. It explicitly rejects a currently active repair: the original selects repair as a different speedup target with a different health formula. Repair, fortification, Locker/Academy/Lab research speedups and gifted speedup inventory are not presented as implemented here.

The decoration shop contains **73 currently offerable priced source decorations**, grouped under the actual supplied labels **War, Nature, Leisure, Flags and Special**. It uses the server store's `BUILDING<id>.c[0]` price, as the original button does. The `YARD_PROPS` `r5` values are stale for many items: an Acorn is 5 Shiny, not its catalog `r5` value of 150; the Pirate Flag is 125, not 250. Placement checks the yard boundary, buildings, mushrooms, balance and server building limit before charging. Normal original decoration quantity `0` means unlimited by species, not unavailable.

The ordinary shop excludes source-blocked items 58, 59, 67, 69, 70, 110, 111, 120, 121, 131 and 135. Event rewards, promotional items and blocked decorations have not been turned into arbitrary free purchases. Retained source metadata describing storage/recycling is provenance, not a claim that the complete original decoration inventory flow has been implemented in this update.

## Yard expansion

The original baseline is 1,000 by 800 Cartesian source pixels. Every purchase multiplies each integer dimension by 1.1, truncates that multiplication, and rounds up to a multiple of 20 only after all purchased tiers have been applied.

| Purchased tiers | Width | Height |
| --- | ---: | ---: |
| 0 | 1,000 | 800 |
| 1 | 1,100 | 880 |
| 2 | 1,220 | 980 |
| 3 | 1,340 | 1,080 |
| 4 | 1,480 | 1,180 |
| 5 | 1,620 | 1,300 |
| 6 | 1,780 | 1,420 |

NXT uses 20 source pixels per logical cell and preserves these default extents. A custom yard-size setting scales the baseline. Purchased bounds are used by host placement and deployment checks and by the client presentation; the surrounding terrain remains scenery rather than free building space. When an extent has an odd number of cells, centering on integer build cells can translate the rectangle by half a cell compared with Flash's exact half-pixel-coordinate center.

The original `MAP.Edge` is a closed white two-pixel, 50%-alpha boundary transformed through `GRID.ToISO`. The Inferno `ENLI` entitlement is a separate source product and is not offered as a new Inferno system here.

## Mushrooms

New yards receive **20** mushrooms, matching the source's starting population. The original starts them in five random clusters of four. NXT places them collision-safely around the yard collar, preserving existing buildings and avoiding overlapping clickable mushrooms. This placement policy is an explicit adaptation, not a claim to reproduce the original random cluster coordinates exactly.

Regrowth adds one mushroom per **17,280 seconds (4 hours 48 minutes)**. A catch-up adds at most ten and fills only to a population of **ten**, even though the initial population is twenty. The original retained up to twenty mushrooms and performed this regrowth calculation when a yard loaded. NXT also advances it on the host while the world is running. The growth timestamp is persisted; elapsed fractional intervals are discarded when a regrowth calculation is applied, matching the original reset-to-now behavior.

The original mushroom footprint is 30 by 30 source pixels. Spawn positions can occupy the terrain collar up to 200 pixels outside the buildable yard. The five visible mushroom variants do not reveal their rewards.

Clicking a mushroom reserves a free worker without a resource or Shiny charge. After arrival, the source clears it on the 62nd display call: **1.55 seconds at 40 Hz**. One quarter of mushrooms are golden; a golden mushroom gives 3 Shiny with a two-thirds chance or 8 Shiny with a one-third chance. Ordinary mushrooms give zero. This means the reward distribution across all mushrooms is 75% zero, 16⅔% three and 8⅓% eight.

Flash used `BitmapData.noise` through `Rndm`, seeded from mushroom coordinates, for its golden check. NXT rolls once on the host and persists a private reward with a unique mushroom identity. The public snapshot contains the artwork variant and state, not the hidden reward or RNG. Clearing consumes that identity once; retrying the same request cannot award it again. This preserves the original reward probabilities without requiring the Flash noise implementation or trusting a client-provided reward.

## Source evidence and validation limits

`Content/Data/store-rules.json` is reproducibly extracted by `Tools/prepare_store_rules.py`. It contains original prices, expansion dimensions, mushroom rules, decoration eligibility and the relevant source branch distinctions. Exact reference files and SHA-256 hashes are retained under `Content/Data/Original/Store/`.

Run the source conversion checks with:

```text
python Tools/test_workers_store_source.py
```

These tests verify source hashes, reproducible extraction, the effective ordinary Shiny default, worker rules, mushroom timing and payouts, expansion rounding, packing, speedup source thresholds and decoration eligibility/prices. They do not execute Unreal. Native simulation and route regression results are reported separately in `VALIDATION.md`. An actual UE5.8 UHT/UBT build, editor rendering, packaged host/client session and visual comparison must not be inferred from Python or portable simulation checks.

This feature set does not claim complete Inferno, outposts, events, the entire premium store, Sharper Tools boosts or every original shop/inventory flow. It does not alter the existing rival count, server player cap, shield length, autosave interval, account storage or server connection model.

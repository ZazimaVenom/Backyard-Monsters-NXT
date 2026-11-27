# Shiny, the General Store, decorations and mushrooms

This update ports the ordinary main-yard economy from the supplied **backyard-monsters-refitted** source. The host owns all balances, purchases, mushroom rewards and expansion state. Clients request actions; they cannot specify balances, prices or rewards. Accounts remain native Unreal `.sav` files. The existing host world/bot snapshot format retains its JSON representation with a versioned economy extension.

## Original rules used

- The supplied normal server starts with **1,500 Shiny**, because its `GameConfig.ts` overrides the 1,000 fallback. This is separate from sandbox mode. `StartingShiny` exposes that starting allowance as a host project setting. An older yard without the economy extension receives this initial allowance once during migration, then retains its saved balance.
- One worker is free. The four permanent extra workers cost **250, 500, 1,000 and 2,000 Shiny**, reaching five ordinary workers. Purchases recheck the exact tier on the host; repeating an old request cannot buy the next, more expensive tier.
- **More Yardage** has six purchases at **50, 100, 150, 200, 250 and 300 Shiny**. Each source pixel dimension is multiplied by 1.1 with integer truncation at each step, then rounded up to a 20-pixel cell. Default dimensions are **50×40, 55×44, 61×49, 67×54, 74×59, 81×65 and 89×71 cells**. The rectangular border expands around the existing layout. A host override of the original 40-cell starting height scales the starting width by the original 1.25 ratio.
- **Improved packing skills** adds 10 percentage points to resource capacity for each of ten permanent levels, priced **50 through 500 Shiny in steps of 50**. It increases capacity; it does not grant resources.
- The General Store is original building **12**: **1,080 Twigs, 720 Pebbles, 10 source seconds**, requiring Town Hall level 1. Store purchases require it to be completed and at least half healthy.
- Building construction and upgrades support the original **Close Enough** free finish at five minutes or less, **20 Shiny** for one hour when at least one hour remains, **40 Shiny** for two hours when at least two hours remain, and **Finish Now** above five minutes. Finish Now costs the smaller of `ceil(seconds × 20 / 3600)` and `floor(sqrt(seconds × 0.8))`. These selected-building shortcuts do not require a General Store. Purchases validate the current job, a persisted purchase revision and the player's maximum displayed price. Completion uses the same XP and worker-release path as normal work. Repair, research and fortification speedup targets are not included in this construction/upgrade subset.
- The decoration catalog contains **73 source-priced, unblocked decorations**, with the original **War, Nature, Leisure, Flags and Special** categories. It uses the server's `BUILDING...` store prices, which differ from some stale `YARD_PROPS.r5` values. Event rewards, blocked entries and unavailable promotional items are excluded. Decorations place instantly without a worker. The host checks ownership limits, available Shiny, yard bounds and occupied ground before charging.
- New yards contain **20 mushrooms**. Regrowth adds one per **17,280 seconds (4 hours 48 minutes)**, with at most ten catch-up spawns and a regrowth population ceiling of **10**. Existing initial mushrooms above that ceiling are retained.
- Picking a mushroom is free and reserves a worker. After arrival, the source's 62 calls at 40 Hz take **1.55 seconds**. A mushroom has a **25% golden probability**; a golden mushroom awards **3 Shiny with probability 2/3**, otherwise **8 Shiny**. The host rolls once at spawn and saves the result privately. Only completed picking grants the reward and removes the mushroom; repeated requests cannot repeat that reward.

`Plugins/BackyardMMO/Content/Data/store-rules.json` records the source files, hashes, exact prices, blocked decorations and branch distinctions. `Tools/prepare_store_rules.py` regenerates that conversion from the supplied source.

## Native host adaptations

The supplied Flash source grows mushrooms when loading a main yard. NXT also advances that same bounded growth clock while the host is running, so growth does not require reconnecting. Its host-persisted random stream replaces the Flash client `BitmapData.noise`/`Math.random` calls. Collision-safe spawning in the yard and its original 200-pixel surrounding terrain collar replaces the initial unconstrained five screen-space clusters; it cannot overwrite an existing paid building or another mushroom. The original probabilities, initial count, catch-up ceiling, footprint, variants and picking time are retained.

Native continuous work countdowns are rounded up to the next source integer second when evaluating speedup thresholds and quotes. This avoids granting a source second before it has elapsed. A falling countdown may lower the charged price below the displayed maximum; it cannot raise it without a new quote.

These are main-yard systems. This update does not claim the unported Inferno/outpost economy, platform billing, promotional rewards, or every original timed shop boost. Unimplemented products are not displayed as working purchase buttons.

## Validation limits

Portable simulation tests exercise the actual host simulation through test adapters. Native Unreal Header Tool, Unreal Build Tool, editor rendering, mouse interaction, packaged networking and actual engine `.sav` execution require Unreal Engine and are not established by those portable tests.

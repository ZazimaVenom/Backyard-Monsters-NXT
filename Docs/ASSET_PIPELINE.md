# Original asset and catalog preparation

The plugin contains the original source imagery in its fixed `Plugins/BackyardMMO/SourceAssets` directory. The editor importer reads this directory automatically. It does not require an asset-directory selection.

The preparation included 6,273 original image, audio and font files. Every original byte hash is mapped to a prepared file in `Content/Data/source-index.json`; byte-identical duplicates share one prepared file. The finished source directory contains 6,525 files, including 6,413 textures/PCM WAVs importable by the editor tool. MP3 originals and fonts are preserved but not counted as imported Unreal textures or sounds. All 101 unique original MP3s were converted to 16-bit PCM, 44.1kHz WAV. Three are background music.

`Content/Data/asset-manifest.json` records each prepared relative filename, SHA-256, size, Unreal object path and image dimensions. Original catalog text and parsed JSON are preserved in `Content/Data/Original`; the original source license and README are preserved there verbatim. The source LICENSE says PolyForm Noncommercial 1.0.0; the source README advertises GPLv3. This discrepancy has not been resolved by the conversion.

## Catalog

`Content/Data/catalog.json` contains 140 overworld building records, 327 building-level records, 28 creature records and 65 sprite descriptors. The 135 Inferno and 135 outpost literal records are also preserved as parsed/raw reference catalogs. Merely importing these data records does not implement their original gameplay systems.

Creature records now also include 162 normalized level rows, 134 research transitions, original Locker prices/requirements, source blocked/fake flags and portrait/menu metadata. Each source property array clamps independently to its last entry, matching `CREATURES.GetProperty`. The legacy scalar fields remain available, while runtime research and training use the per-level rows. `MONSTER_PROGRESSION.md` documents the supported runtime subset and source differences.

The parser reads ActionScript literal arrays/objects, arithmetic, and `SecNum`, `Point` and `Rectangle` constructors without executing the source. Numeric reference constants are read from the corresponding original declarations. Prerequisite type 999 is the source's UnderHall requirement; it must not be silently treated as a Town Hall prerequisite.

| Field | Meaning |
|---|---|
| Building `size` | Original visual size parameter, not Unreal centimeters or the exact collision footprint |
| `collisionFootprints` | Original overworld collision rectangles from the building class, where statically available |
| Level `twigs`, `pebbles`, `putty`, `goo`, `time` | Original purchase/upgrade costs and seconds |
| `production` / `productionCycleSeconds` | Yield per original cycle; **not** yield per second |
| `capacity` | Producer buffer, silo resource storage, or housing space, depending on building type |
| `attackRateFrames` | Raw original tower `rate` value |
| `attackIntervalSeconds` | `rate / 30`; timing conversion must be checked against Unreal combat |
| `requirements` | Original `[building type, quantity, level]` tuples |
| Creature `damage` | Original signed damage; Zafreeti/Vorg have negative values because they heal |
| Creature `time`, `goo`, `housing` | Level-1 original creation seconds, cost and housing space |
| Creature `sourceProps` | All original per-level arrays, including special properties |
| `rangeStatus` | Explicitly flags creatures whose range was not in the literal catalog |

The original visual `size` differs from the collision footprint. For example, Twig Snapper has `size=100` but a 70×70 collision rectangle; Town Hall has `size=190` but a 130×130 overworld collision rectangle. The runtime uses a square grid approximation derived from `size`, not a direct port of the original collision geometry. `collisionFootprints`, `originalFootprintExpression` and `classSource` preserve this distinction.

The eight records without a render texture are source placeholders/nonbuildable entries: 112, 122–126, 128 and 130. They have no original normal purchase availability or lack level rows. This is not a reason to invent replacement art.

## Rendering the flat world

`terrain.grassField` points to `derived/terrain/grass_blended_tile.png`, a 1000×500 composite built using the original `MAPBG.MakeTile` recipe. The seven original images are **rectangular texture samples, not diamond tiles**. Each sample is repeated 5×5 in its own layer; layers 2–7 are blended in symbol order through stitched, two-octave turbulence alpha masks. The compositor reads the actual embedded client images, including the original JPEG for isograss1. It combines each source alpha with its mask rather than treating partially transparent pixels as opaque. `Original/MAPBG.as` and `Original/MAP.as` retain the source recipe and repeat behavior.

The prepared composite uses the original default base seed 0. Its noise parameters, blend order and dimensions follow the source; byte-identical Flash output has not been verified against a Flash reference capture. Per-yard original base seeds and the other original terrain types remain future work. This corrects the earlier staggered, opaque patchwork generation.

The terrain texture wraps in both directions. The board repeats it at the same source-pixel scale as the building artwork (20 source pixels per game cell), preserving its 2:1 shape instead of stretching it across a square yard. A continuous backdrop extends beyond camera pan/zoom bounds, following the original outer-ground fill. Camera clamping considers the entire orthographic viewport and its current aspect ratio, leaving a safety margin inside that backdrop; unusually tall windows reduce maximum zoom to keep all corners over terrain. Other images and sprite atlases remain clamped. The placement marker is a transparent 2:1 diamond around the logical footprint, accompanied by the actual selected building artwork at its original back-corner anchor. Construction and deployment use separate preview rules matching their respective host checks. Simulation collision sizes remain the existing grid approximation, and visual selection uses sprite rectangles rather than Flash hit silhouettes.

Building `texture` is the original top image. Many original buildings are assembled from several layers, so displaying only this image produces missing pieces. Prefer a level's `compositeTexture`, or `composite.texture`. The compositor places original shadow/top/animation frame 0 at their original offsets; `pixelWidth`, `pixelHeight`, `pivotX`, and `pivotY` retain alignment. Matching `damagedComposite` and `destroyedComposite` records are supplied where the source defines those states. Original animation layers and atlas frame counts remain in `imageLayers` for further animation work.

Composite shadows convert the source's monochrome multiply layer to black alpha. The unlit translucent M_SpriteAlpha material preserves their soft alpha; M_Sprite remains masked for the opaque terrain. Both support custom depth, and sprites sort by their projected depth. The composite still approximates Flash multiply blending. Supported activity buildings now composite every authored animation cell with the original layers and pivots. Twenty-eight new atlases cover 62 levels across collectors, Silo, Locker, Hatchery and Academy; source-static structures retain their still artwork. This is not complete animation coverage of every catalog system. See `BUILDING_ANIMATION.md`.

Monster sheets have **facing directions across columns** and **animation states down rows**. Most early monsters have one row. Cycling horizontally as if these were walking frames makes a monster spin. `frameWidth`, `frameHeight`, `columns`, `rows`, `pivotX`, `pivotY` and `layout` are derived from `SPRITES.as` and the PNG dimensions. `fps=7.5` is explicitly marked as a presentation default; original state-dependent pacing is preserved in `Original/SPRITES.as`. `idleRow`, `movingStartRow` and `movingFrames` explicitly distinguish animation ranges from state rows; Brain must not cycle into its invisible state. `sourceDirections` and `sourceFacingOffsetDegrees` preserve the original facing formula (atan2 minus 90 degrees for regular monsters). Original ground and Inferno worker variants are retained separately. The source declares Project X frames 35 pixels high, while its actual image is 34 pixels high; the runtime frame height is normalized to 34 pixels, while `originalFrameHeight=35` preserves the declaration. The original PNG remains byte-for-byte unchanged. Other trimmed effect strips are bounded the same way.

The original creep-specific animation/state code, additional dynamically registered creatures, projectiles, champions, siege weapons and campaign behavior are not replaced by a catalog export. Consult the feature audit for gameplay implementation status.

## Native UI artwork

`prepare_ui.py` composes the four resource plates from the original SWF-exported shapes at their source coordinates. `ui-art-provenance.json` records input hashes, symbols and offsets. The native HUD also reads original wood/stone button images, modal frame parts, building menu cards and monster icons. The complete Groboldov and Verdana font exports are staged from plugin content as UFS runtime dependencies; small subset-only font exports remain preserved but are not used for normal text.

The UI retains live host state in widget attributes instead of rebuilding every button on each snapshot. Construction is arranged in the original four categories and 5×2 card layout. The new ranking ladder and host/account screens are native additions required by this project, not features claimed to match the original Flash screens.

## Reproduce and check

Run from a command prompt with Python 3.10+, Pillow and optional FFmpeg:

```
python Tools/prepare_assets.py --source "C:\path\to\backyard-monsters-refitted"
python Tools/test_asset_pipeline.py
```

The preparation script resolves its plugin output relative to itself. `--plugin` exists for development automation, not the in-editor user flow. Existing WAVs newer than their MP3 input are reused. Original files are never executed.

Asset tests verify restricted parsing, original numeric values, all 6,273 original asset hashes, every prepared checksum and collision-free Unreal path, texture existence, composite dimensions and atlas validity. These tests do not compile Unreal C++ or validate the resulting `.uasset` rendering.

## Texture defaults after every import

All bundled PNG/JPG artwork is color imagery. Every Setup All and Import / Refresh operation ends with a separate **Finalizing color texture defaults** pass after all `ImportAssetTasks` calls have returned. Image paths are collected before the source-hash cache check, so unchanged source files cannot bypass this pass. The pass waits for texture compilation, loads every reached image asset, enforces the settings below, verifies the result and saves changed or dirty packages. Setup cannot report success if an asset cannot be loaded, normalized or saved. Canceled imports finalize the paths reached so far and report cancellation; running the same import again resumes work and checks all images.

| Texture setting | Final value |
| --- | --- |
| Compression Settings | Default (`TC_Default`) |
| sRGB | Enabled |
| Compression None | Enabled, preserving the existing uncompressed 2D artwork policy |
| Compress Without Alpha / Flip Green Channel | Disabled |
| Texture Group / Mip Gen Settings | 2D Pixels / NoMipmaps |
| Filter / Never Stream | Bilinear / Enabled |
| X and Y addressing | Wrap for the seamless grass composite; Clamp for all other images |

The same policy is applied before each newly imported texture is checkpointed, so interrupted imports also retain color settings. The final pass covers existing imports regardless of source hash and corrects any settings changed during import. Textures already matching the complete policy are verified without another rebuild. The setup JSON report includes `textureDefaultsVerified` and `textureDefaultsCorrected`. Source images and their alpha bytes are not rewritten.

Unreal's legacy texture factory may still show a content-based normal-map detection notice during import. This implementation does not claim to disable that undocumented detection path; it explicitly overrides its classification before saving and again in the final pass. Do not treat a notice shown partway through import as the final settings. No speculative factory properties or project-wide detection changes are used.

The editor automation test `BackyardMMO.Assets.ColorTextureDefaults` covers normal-map correction, unchanged source/alpha bytes, repeated verification, cached misclassification and terrain wrapping. It is included but has not been run here. Engine import and rendered output still require verification in UE 5.8.

## Combat presentation update

`prepare_combat.py` records separate source simulation/display clocks, explicit monster state-row selections and unavailable frames. It composes full defense frames from untouched original layers and pivots, stages source-derived effects and sounds, and records collision rectangles independently of artwork dimensions. `Content/Data/combat-art-provenance.json` records the evidence. Direction columns are never treated as walk frames. Deployment masks are generated interaction geometry, with transparent corners and contrasting closed borders. The original image/audio/font byte hashes remain preserved.

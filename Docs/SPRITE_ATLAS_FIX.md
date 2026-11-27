# Wandering sprite fix — source preview 0.1.5

The reported screenshot shows some Pokeys as several narrow pink strips while other Pokeys retain their full bodies. The original Pokey image is intact: `monsters/sprite.1.v1.png` is a 720 × 21 RGBA sheet containing thirty 24 × 21 facing poses.

## Corrected defect

`ABYMBoard::SetAtlas` previously committed the descriptor pointer before validating texture readiness, and derived frame dimensions from `UTexture2D::GetSizeX/Y`. If initialization happened against a default texture resource, the resulting bad UV scale was cached for that resident. Later residents could initialize correctly, explaining a mixture of good and corrupted sprites. This is a confirmed code defect and a plausible explanation of the screenshot; the exact live resource values were not captured on the user's machine.

The asset pipeline now records the exact source image width and height for every prepared atlas. Runtime frame geometry uses those authored dimensions, including trailing padding and fractional frame widths. A layout is committed only when valid and its texture is ready. Pending initialization retries on subsequent updates. The cache key includes the texture path, so reused visuals cannot retain another sheet's layout.

Sampling runs between texel centres inside the selected frame. Facing selection uses the source direction count independently of sheet capacity; spare columns are not treated as additional facings. Each resident retains its own dynamic material and frame state. A readiness retry also preserves combat-effect lifetimes and smoke playback, and cannot reveal an expired effect or a resident hidden during a battle.

## Scope

This is a presentation and metadata fix. Original sprite pixels, imported color defaults, terrain, resident ownership/variety/50 limit, combat simulation, rival bots, account data and host `.sav` formats are preserved. Existing host data does not require conversion. Normal `.uproject` opening and the existing automatic bundled-catalog refresh remain the setup path.

## Verification

- The original Pokey source hash and all thirty pose bounds are tested.
- Offline layout tests exercise exact and padded sheets, fractional dimensions, first/last frames, one-pixel effects, independent layouts, invalid metadata, overflow and retry after rejection.
- A native editor integration test exercises the actual board with several Pokeys, checks independent materials and full-frame UVs through walking/turning/pausing, refreshes snapshots and changes the resident roster. It allows pending texture preparation, finishes only the test texture, and checks that the existing materials initialize correctly afterward.
- The automated result counts and execution limits are recorded in `VALIDATION.md` and `ALGORITHM_CHECKS.json`.

Unreal is unavailable in this development environment. The new editor integration test is included but was not executed here. A fresh UE 5.8 build and visual check remain necessary before claiming this screenshot defect is confirmed resolved in-engine.

## Target-editor regression

Open `BackyardMonstersNXT.uproject` normally and allow the usual module rebuild and automatic catalog refresh. Observe several Pokeys walking and turning, then a varied trained roster. No resident should display multiple poses compressed into strips. Check both initial texture loading and a subsequent session, zoom in/out, refresh the yard, and enter/leave an invasion. Smoke should continue advancing through its frames. Run the `BackyardMMO.Presentation.SpriteAtlasLayout`, `SpriteAtlasValidation` and `ResidentSpriteAtlasUVs` automation tests after normal editor setup.

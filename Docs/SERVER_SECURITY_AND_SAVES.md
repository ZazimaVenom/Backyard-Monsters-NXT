# Host accounts and world saves

Backyard Monsters NXT uses the Unreal listen server in the same packaged game as its clients. Players connect to the configured Unreal game port, then the native game UI creates or authenticates an account on that host. There is no external account service, database, custom key exchange, fingerprint prompt or separate server application.

## Authority boundary

`UBYMServerSubsystem` opens accounts and world data only inside `ABYMGameMode`, in a game world whose net mode is not `NM_Client`. MainMenu and remote client worlds cannot open or save the account store. The owned PlayerController sends the username, password and registration/login choice through its server RPC; the authenticated account ID remains server-owned. Clients request actions; they cannot submit authoritative resource balances, combat results, shield expiry times or RP awards.

Account records and password material are excluded from yard snapshots and leaderboard responses. New accounts receive GUID identities; usernames are unique without regard to case. Usernames permit 3–24 ASCII letters, digits, underscores and hyphens. Passwords permit 10–72 characters with a maximum of 128 UTF-8 bytes. Credentials are not logged.

The host owner controls their executable and files. Server-owned saves prevent remote players from supplying edited local account saves; they do not prevent the host owner from modifying their own data.

## Login and password storage

Login uses ordinary Unreal RPC networking. This plugin does not add transport encryption; hashing stored passwords does not encrypt credentials in transit. It contains no custom host keys, authentication envelopes or fingerprint acceptance step.

The Core-only `BYMPassword` helper stores PBKDF2-HMAC-SHA1 password verifiers with separate 16-byte salts, 20-byte derived keys and at least 210,000 iterations for new accounts. The verifier uses Unreal's `FSHA1::HMACBuffer`; salts are generated from Unreal GUIDs. Stored iteration counts and decoded salt/digest sizes are validated before use. Unsupported older digest formats stop loading and preserve the files instead of resetting accounts. Password comparison examines the complete digest without an early exit.

Password derivation runs on a worker thread. Account, session, simulation and filesystem mutations run on the game thread. A connection that closes during authentication cannot acquire a session or create an account afterward. Per-connection, per-username and global attempt limits apply, with at most 16 active password jobs. Duplicate usernames and already-online accounts are checked again after background work completes.

## Files and save policy

Account storage uses native Unreal `USaveGame`; world/bot state retains the existing JSON file format. Source preview 0.1.8 retains lifecycle and experience fields and adds Bunker garrison/active-defense state inside that world's simulation subdocument (schema 5, reading schemas 1–4). It does not convert world/bot persistence to `.sav` or change the existing hourly world autosave and logout-retention policy.

| Host-local file | Purpose |
| --- | --- |
| `Saved/SaveGames/BackyardMonstersNXT_Accounts.sav` | Native `UBYMAccountSaveGame` with typed account IDs, usernames, salts, password verifiers and iteration counts. |
| `Saved/SaveGames/BackyardMonstersNXT_Accounts_Previous.sav` | Previous committed native account save. |
| `Saved/BackyardMMO/world.json` | Existing world/bot simulation save; new snapshots omit account records. |
| `Saved/BackyardMMO/world.previous.json` | Previous committed world snapshot. |
| `Saved/BackyardMMO/host.lock` | Exclusive host process lock. |
| `*.pending` | Flushed candidate files from an incomplete or failed save. |

The `.sav` data is produced by `UGameplayStatics::SaveGameToMemory` and restored by `LoadGameFromMemory`. It uses Unreal's binary SaveGame archive and custom class metadata, with account data stored directly in typed `UPROPERTY(SaveGame)` fields. The existing flushed atomic file replacement commits these native bytes. Only the authority subsystem holding the hosted world's exclusive lock writes account saves.

Registration commits the initial yard to `world.json` with its account ID marked in a registration journal, then commits the native account save. Success is returned only after both commits succeed. If account saving fails, the new account and initial yard are removed from memory. On restart, only a yard explicitly marked as an uncommitted registration may be removed. Established accounts, ordinary saved yards and bots are preserved. A committed account missing its matching yard stops loading and requires recovery instead of silently creating a new yard.

A valid previous-format world migrates its account records once, after validating the accounts and world. If both legacy and native account records exist after an interrupted migration, every record must agree before the legacy account array can be removed. Mismatched stores are preserved and loading stops. Future world snapshots omit the account array. The preserved `world.previous.json` can retain legacy password hashes until a later save replaces that recovery copy; it is not the active account store. A missing or corrupt native account file for a migrated world stops loading instead of creating an empty store.

Gameplay and bot progress remain in host memory between autosaves. Logout removes the active session but retains the full yard; logout does not trigger a save. The default world autosave interval remains 3,600 seconds. Creating an account also commits current world progress before acknowledging registration. By default, orderly host shutdown attempts a final world save. Accounts commit when created; the account `.sav` does not need to be rewritten every gameplay tick or world autosave.

The nested simulation now writes **schema 4** and continues to read schemas **1, 2 and 3**. Each building preserves its attained and target levels, stable paid-job identity, remaining construction/upgrade work, health and independent repair state. Damage/repair can pause that work without replacing it with a repair deadline. The legacy `readyAt` field is retained for compatibility, not as authority for new jobs. Schemas 1–3 migrate their old absolute deadlines at the existing bounded accrual origin; a repair-only legacy record cannot restore a pending upgrade that an older build had already discarded. Schema-3 grouped Hatchery queues remain intact, while schemas 1–2 retain their existing paid-queue migration. `BUILDING_LIFECYCLE.md` and `HATCHERY_PRODUCTION.md` describe these transitions.

Schema 4 also stores `earnedExperience` and the retained `baseValue` for each player/rival yard. Displayed level and combined points are derived by the host. Older worlds start with zero unrecorded earned experience and their known completed-yard value; migration does not invent past action rewards. Invalid counters, work states or identities reject loading while preserving the original save. No client command accepts an experience award. These additions leave account class serialization and the outer world JSON storage mechanism unchanged; see `PLAYER_EXPERIENCE.md`.

Each commit writes and fully flushes a candidate in the same directory, then atomically replaces the destination. Windows uses `MoveFileExW` with replacement and write-through flags. Previous backups come from the last successfully committed in-memory snapshot. Failed saves retain current world progress in memory and retry on the existing schedule. A crash can lose gameplay progress since the last successful world commit. A failed shutdown save cannot preserve memory after the process exits.

For recovery, stop all host processes and copy both `Saved/SaveGames` account files and the entire `Saved/BackyardMMO` directory somewhere safe. Restore matching account/world snapshots. Do not distribute host save files with client packages. Do not replace an existing player's yard to resolve a missing or mismatched save.

## Validation status

Native engine automation includes `BackyardMMO.Accounts.PasswordVerifier` and `BackyardMMO.Persistence.NativeAccountSaveGame`. The latter verifies native GVAS format and password preservation through an Unreal SaveGame round trip. These tests still require execution in Unreal Engine 5.8. The packaged game additionally requires:

- Two-machine registration, login, wrong-password and reconnection checks.
- Native account `.sav` reload, valid legacy migration and refusal to reset corrupt/missing/mismatched stores.
- Interruption between world and account commits, with recovery limited to the explicitly journaled initial yard.
- Connection closure during hashing, duplicate username races and duplicate account login attempts.
- Read-only/full-disk failures and account rollback.
- Hourly autosave, logout memory retention, shutdown save and restart continuity.
- Schemas 1–3 migration into schema 4, paused construction/upgrade plus repair reload, stale cancellation rejection, and experience counter continuity without duplicate awards.
- Exclusive host lock, interrupted file replacement and backup recovery.
- Login bursts and world load with 100 simultaneous players and 1,000 rival bots.

No Unreal build, packaged multiplayer run or 100-player capacity test has been performed in this environment.

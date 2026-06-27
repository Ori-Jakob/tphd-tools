# actor_data — actor names + spawn options

Source data for the actor tooling (save-state spawn/delete commands today, a
dedicated enemy spawner later). Nothing here is built into the RPL directly; the
generator turns `actor_table.tsv` into `data/procs.bin`, which *is* embedded.

## Files

| File | What it is |
|---|---|
| `actor_table.tsv` | **Source of truth.** `id_hex <TAB> proc_name <TAB> friendly_name`, one row per actor. Hand-editable. |
| `enemy_spawn_options.txt` | Per-actor `params`/`subtype` documentation for the spawner. Seeded with Keese + Kargarok. |
| `proc_ids_tphd.txt` | Read-only reference dump of id → proc_name (from the rpx). |
| `procs.bin.vanilla.bak` | The original (wrong, vanilla-TP-id) procs.bin, kept for reference. |
| `../tools/gen_procs_bin.py` | Generator: `actor_table.tsv` → `../data/procs.bin`. |

## procs.bin format (generated)

Dense array indexed by proc id, embedded as `procs_bin` (BIN2S). **64 bytes/record:**

```
0x00  u16  id            big-endian fpcNm id (must equal the record's index)
0x02  char proc_name[30] engine module name (d_a_*), NUL-padded
0x20  char friendly[32]  human name (may be empty), NUL-padded
```

A record is "valid" only when its big-endian id equals its index, so gaps (ids with
no actor) read back as invalid. The consumer in `src/tools/save_state.cpp`
(`actorProcIdValid` / `actorProcName` / `actorFriendlyName` / `findActorProcId`)
resolves and searches by **id, proc name, or friendly name**.

### Regenerating

```
python tools/gen_procs_bin.py     # actor_table.tsv -> data/procs.bin
```

Then rebuild (BIN2S regenerates `procs_bin.h`).

## Where the data comes from

- **id ↔ proc_name**: TPHD-specific. The rpx holds a profile table at `0x100034a0`
  — an array of `{u16 id; u16 pad; char* name}` that the loader (`FUN_020096e0`)
  walks to build the runtime id→profile map (`DAT_10131250`). The vanilla GameCube
  ids do **not** match TPHD (they're shifted), so these must be dumped from the
  actual `Zelda.rpx` per game version. `proc_ids_tphd.txt` is that dump for US v81.
- **friendly_name**: auto-seeded from the `@brief` of each `d_a_*` class header in
  the zeldaret/tp decomp (mirrored in `../dusklight/include/d/actor/`). Some are
  imperfect (a few headers lead with a file comment) — fix them directly in the TSV.

## Adding spawn options for the rest of the enemies (the ongoing task)

Goal: a `params`/`subtype` entry in `enemy_spawn_options.txt` for every enemy actor
(`d_a_e_*`) and boss (`d_a_b_*`) in `actor_table.tsv`.

For each actor, open its decomp source (`../dusklight/src/d/actor/<proc>.cpp`), find
the **create** function, and read how it decodes `fopAcM_GetParam(this)` and the
subtype/`home.angle`. Record the bit fields and any enums in a block (format is
documented at the top of `enemy_spawn_options.txt`). Keese is the clean worked
example: `mType = (param & 0xf000) >> 12`.

Tip: `grep -n "GetParam" ../dusklight/src/d/actor/d_a_e_*.cpp` lists every enemy's
param reads in one shot.

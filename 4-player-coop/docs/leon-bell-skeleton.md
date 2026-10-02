# Leon Bell as a player character: skeleton compatibility (2026-10-01)

Question: does Chuck have a unique skeleton, or could players 3/4 be swapped to the TIR contestant Leon Bell?

Answer: **Chuck's skeleton is not unique, and Leon Bell's matches it exactly.** All evidence below is read-only, from the
retail PC `data/datafile.big` and `data/models/npcs.big`.

## Leon Bell's model

`missions.txt` (LEVEL_ARENA_BACKSTAGE) has `cMissionSetChuckState MetLeonBell2 { Item = "boss_ultimatefan" }` next to
MetBobby2 (`CineContestant2`) and the Twins. `items.txt` defines it as follows:

```text
cNPCItem boss_ultimatefan
  AssetFilename = "data/models/npcs/boss_ultimatefan"   BossType = 19   HealthAmount = 2500   Height = 1.9558
  SkeletonSlotName = "SkeletonPlayer"   CinematicSkeletonSlotName = "SkeletonCinematicPlayer"
```

`SkeletonPlayer` is the player slot (`eSkeletonSlot::SKELETON_PLAYER = 0`). `CineContestant1..4` use the same pair of
slots. `Height` is gameplay/AI metadata, not mesh scale; the bones below are identical.

## Bone comparison

The reference is Chuck's player outfit piece `chest_tir_outfit.big`, which has 98 `dummy01.*` bone records
(52 bytes each) plus `_BONENAMES_` (3038 bytes).

| Model | Bones present | `_BONENAMES_` | Rest-pose records that differ |
|---|---:|---|---:|
| `boss_ultimatefan` (Leon Bell) | 98/98 | identical | **0** |
| `cine_contestant1` | 98/98 | identical | 0 |
| `chest_tir`, `feet_Bare_Feet_Chuck_over`, `headwear_Chuck_Coloured_Hair_Blue` (other Chuck pieces) | 98/98 | identical | 0 |
| `cast_sullivan` | 98/98 | identical | 0 |
| `boss_tk` | 98/98 | identical | 96 (own proportions) |
| `cast_katey` | 98/98 | identical | 96 (own proportions) |

Every human model uses the same 98-bone naming. Leon's bind pose is byte-identical to Chuck's, so his meshes skin
correctly on the player skeleton and play every Chuck animation with no retargeting.

## What Leon is made of

| Leon mesh | Leon textures | Chuck equivalent |
|---|---|---|
| `geo_rassltboot`, `geo_lassltboot` | `tir_assltboot_*` (shared) | TIR outfit boots (feet `tir_outfit_over`) |
| `geo_ltirglove`, `geo_rtirglove` | `tir_glove_*` (shared) | TIR outfit gloves (hands `tir_outfit`) |
| `geo_onesuit_torso`, `geo_onesuit_legs` | **`uf_onesuit_*` (Leon's own suit)** | TIR onesuit (`tir_onesuit_*`) |
| `geo_leye`, `geo_reye`, `geo_mouth` | `cr_eye_*`, `cr_mouth_*` (shared with Chuck's `head_naked`) | Chuck's eyes/mouth |
| `geo_head` | **`uf_head_*`** | Chuck's `head_naked` (`cr_headnude_*`) |
| `geo_headcap`, `geo_headcapedge`, `geo_haircard1..25` | **`uf_haircap_*`** | headwear slot (hair) |

Leon is Chuck's TIR outfit geometry with his own head, hair and suit texture.

## Swap options for players 3/4

`outfits.csv` columns are: name, headwear, head, facewear, chest, legs, hands, feet. For example:
`OUTFIT_TIR,tir_outfit,naked,NONE,tir_outfit,tir_outfit,tir_outfit,tir_outfit_over`.

1. **As a player outfit (recommended).** Split Leon's model into player pieces:
   - `head_leon` (`geo_head` + eyes + mouth, `uf_head`/`cr_*` textures);
   - `headwear_leon` (headcap + haircards, `uf_haircap`);
   - `chest_leon` / `leg_leon` (onesuit meshes, `uf_onesuit`).

   Then add `OUTFIT_LEON,leon,leon,NONE,leon,leon,tir_outfit,tir_outfit_over`. Boots and gloves reuse the existing TIR
   pieces unchanged. This uses DR2's normal per-player clothing path (`cPlayerDataTracker::SetClothingInfo`), which the
   four-player harness already replicates with four clothing heap sets.

   Work needed: a model-piece splitter that rewrites SceneDescription, MatArray, MatTextureInfoArray and
   `persistent.big` for a subset of Leon's meshes. Everything goes in as additive overlay files.
2. **Whole-model swap.** Point the remote player's actor at `boss_ultimatefan`. This avoids splitting assets, but
   bypasses the clothing system (outfit changes, partner sync). It needs new runtime code in the player actor's
   model-loading path. Not recommended.

Open items before building option 1:
- Confirm how the player resolves outfit column values to piece files (`<slot>_<value>.big` plus `.tex`), including
  the case of the `leg`/`legs` prefix.
- Check whether any outfit or clothing table besides `outfits.csv` must list new pieces.

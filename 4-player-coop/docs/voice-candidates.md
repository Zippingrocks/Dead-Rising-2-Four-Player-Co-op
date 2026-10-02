# Who could replace Chuck's voice? (2026-10-01, read-only research)

Goal: make players 3/4 entirely new characters, voice included. This note records which DR2 characters ship a voice set
complete enough to stand in for Chuck. Nothing in the game or mod was changed.

Tools and data:
- `tools/gasp_voices.py` and `research/gasp/` (extracted `data/audio/gasp.big` tables, `voice_inventory.json`).
- `data/audio/scenegraph.txt` (effort sounds).

## How DR2 voices a character

There are two independent systems.

1. **GASP speech** (`gasp.big`). Each spoken event is a keyword hashed with `h = h*33 ^ c`. The chain runs keyword →
   sound → bucket → sound group → `.adw` samples (in `chuck.big`, `charvocals.big`, `bossvocals.big`).
   - Chuck has a private table (`chuc_*`).
   - Everyone else shares the `char_*`/`boss_*` tables. Each line is gated by a rule on `characterNameID`, which equals
     the `VoiceType` field in `items.txt` (Leon Bell: `VoiceType = 14` → `PsychoUltimateFan*` lines).
2. **Effort sounds** (`scenegraph.txt` → `fx_*.big/*.sfx`). These are attack grunts, hit reactions, death, falling,
   lifting and so on, triggered by animation and gameplay events.

## Chuck's complete player set (the target)

**GASP:** 34 keywords and 280 samples in `chuck.big`. Examples: ChuckCall, ChuckFollow, ChuckOrder, ChuckRevival,
ChuckFriendlyFire, ChuckWeaponBreak, ChuckEmotePositive/Negative, ChuckComboSuccess, ChuckFindZombrex, ChuckRadio*,
ChuckVehicle, ChuckSlide, ChuckLevelSwitch, ChuckClothes*, food and poker reactions.

**Efforts:** 32 `.sfx` in `fx_main.big`, using another 50 `chuck.big` samples.

| Category | Sounds |
|---|---|
| Attacks | X1/X2/Y1/Y2 Attack, PunchVocal, KickVocal, SwingHeavy |
| Hit reactions | Light, Medium, Heavy, Wind |
| Death | Death, DeathEaten, Falling |
| Exertion | LiftHeavy, HoistStart/End, Grapple, GrappleEndPush, MakeComboItem |
| Condition | IdleInjured, RunInjured, GainsHp, CoughSmall/Med/Big/Spit, PukeStart/End, EatsBadFood |
| Vehicle | CrashBikeHit, CrashBikeRoll |

**Nobody else has Chuck's situational GASP lines** (follow/order/revive/friendly-fire and so on). Those would be
missing or remapped for any replacement. Efforts and emotional lines are where other characters differ.

## Candidates

Skeleton = Chuck's 98 bones in the identical bind pose (body swap works with all Chuck animations).

| Character (model, VoiceType) | Effort `.sfx` | GASP lines | Skeleton | Fit |
|---|---|---|---|---|
| **Generic male survivors** (`srv_*`, VoiceTypes 101–126; effort sets YWM1, YWM2, MWM1, MWM2, OldMan, Ted) | 12 each: AttackLight/Medium, GrappleSmall/Medium/Large, HitReactionLight/Heavy/Fatal, InjuredExhale, VomitStart/End, Getup | 14–16 keywords, ~85–135 samples each (Affirmation, Anger, Apology, Call, Cries, DeathScream, Gratitude, Happiness, Help, Laugh, Rejection, Rescue, Sadness, Scared, Scream, Wait); poker types 102/107/116/118/126/129 add 9 poker lines | `SkeletonPlayer` (bones identical to Chuck; checked on Ted) | **Most complete overall.** Covers efforts, injury, vomit, death and a full emotional bark set |
| **Sullivan** (`cast_sullivan`, 4) | 10: AttackLight/Medium/Heavy, HitReactionLight/Medium/Heavy, Death, Jump, Struggle, Throw | 4 keywords, 60 samples (SullivanRadio, Taunt, ChuckHealthLoss, Dodge) | identical | Best named male: full combat efforts, few spoken lines |
| **TK** (`boss_tk`, 6) | 10: Attack L/M/H, HitReaction L/M/H, Death, Dizzy, Getup, Jump | 11 keywords, 94 samples (Charge, ComeOn, Laugh, TheMan, Taunt…) | **differs** (long-cloak body, 96 bones) | Strong voice; body needs his own skeleton |
| **Boykin** (`boss_boykin`, 17) | 11: Attack L/M/H, HitReaction L/M/H/Fatal, Death, Getup, ThrowGrenade, ThrowPlayer | 9 keywords, 92 samples | identical | Strong |
| **Hangman** (`boss_hangman`, 9) | 12: Attack L/M/H, HitReaction L/M/H/Fatal, Death, Getup, Lift, Release, Throw | 4 keywords, 50 samples | identical | Good efforts, few lines |
| **Mechanic** (`boss_mechanic`, 41) | 10: Attack L/M/H, HitReaction L/M/H/Fatal, Death, Getup, Lift | none found | identical | Efforts only |
| Chef (7), Postman (13), Protester (8), Mascot (18) | 6–13 each | 3–8 keywords, 38–79 samples | identical (Mascot: 1 bone differs) | Partial |
| Militia MM Young/Old/Fat/Big (36–39) | 6 each: Attack M/H, HitReaction L/M/H/Fatal | 3 keywords, 46–57 samples | identical | Partial |
| **Leon Bell** (`boss_ultimatefan`, 14) | **3 only**: Fling, HitReactionMed, Slice | 7 keywords, 62 samples (Goading, Laugh, Celebrate, Miss, Stuck, Taunt) | identical | Body yes, voice thin |
| Rebecca (5), Twins (15/16), Diva (40), female survivors | 11–12 | up to 17 keywords, 119 samples (Rebecca) | female/different (96 bones) | Need the female skeleton |

## Practical reading

- **A survivor archetype** (for example a YWM or MWM survivor) is the only voice that covers combat efforts, injury,
  vomiting, death, and a broad set of emotional lines. It shares Chuck's skeleton, so model, animation and voice all
  transfer.
- **Sullivan, Boykin and Hangman** have full combat-effort sets, identical skeletons and some unique spoken lines. They
  are good fits if a recognisable named character matters more than line coverage.
- **Leon Bell** can be the body, but his voice is mostly taunts. Pairing Leon's model with a survivor or Sullivan voice
  is possible, because the two systems are independent.

What any replacement still lacks:
- Chuck's order/follow/revive/friendly-fire/zombrex lines. Remap these to the nearest generic lines (for example,
  ChuckFollow → SurvivorCall, ChuckRevival → SurvivorGratitude) or leave them silent.
- KickVocal, Falling, coughs, bike crash and DeathEaten. Map these to the closest effort (HitReaction, Death).

Implementation is not designed yet. The likely shape is a per-player voice override at the two lookups:
- the GASP category / `characterNameID` used for that player's speech;
- the `Chuck*` scenegraph trees used for that player's efforts.

Both need runtime work, because the stock game assumes the player is always Chuck.

# nostalRE fork of rAthena

This fork carries the rAthena changes used by
[nostalRE](https://github.com/si-klyde/nostalre). Its
[launcher](https://github.com/si-klyde/nostalre-launcher) takes this branch as its
`server/` submodule and builds the Android app and the desktop servers from it
(`scripts/build-server.sh`, `scripts/build-apk.sh`). The app bundles
login, char and map servers built from this source, together with this tree's
`conf/`, `db/` and `npc/` folders and a SQLite database made from `sql-files/`.

rAthena is licensed under the GNU General Public License version 3
([LICENSE](LICENSE)). These changes are distributed under the same terms.

## Baseline and modifications

- Upstream: [rathena/rathena](https://github.com/rathena/rathena) at
  `d4b8e7b8f16061cc2496d377ac8f777ce72a4f39` (2026-10-06).
- Modified between 2026-10-08 and 2026-10-20 in the Ragnarok Solo integration
  project. The `nostalre` branch's first commit after the baseline holds every
  change; `git diff d4b8e7b8 -- <path>` shows any file's modifications.
- Most behavior changes carry a `Ragnarok Solo` comment.

Summary:

- Optional embedded SQLite backend instead of MySQL: `ENABLE_SQLITE` CMake
  option, `src/common/sql_sqlite.cpp`, the SQLite amalgamation in
  `3rdparty/sqlite` (public domain), `tools/mysql2sqlite.py`, and the
  `sql-compat-test` tool.
- Query fixes for SQLite operator precedence and trailing-space login IDs.
- `RA_KEEP_CWD` keeps the working directory when binaries live apart from
  `conf/`; overridable CMake output directories for cross builds.
- Monster HP bar for every packet version; packet obfuscation off.
- Hair style limits per sex and race; elemental ATK never negative.
- Single-player configuration: local-only IPs, instant character delete,
  pincode off, MVP EXP message on, homunculus growth message off.
- NPC and warp positions for current client map data (Renewal Izlude, no
  airship docks).
- Custom NPCs on (`npc/scripts_custom.conf`): Job Master, Warper, Healer
  (free), Stylist and Reset Girl, in Prontera's square. Prontera's Healer
  moved to 162,195, clear of the Ash-Vacuum Warper. `npc/nostalre_off.conf`
  (empty here) takes `delnpc:` lines for the ones the app's player turns off.
- Booster Event (`npc/custom/booster.txt`, off by default: the app adds an `npc:` line
  for it): kRO's 2021 Booster Promotion. A Booster Registration NPC (Prontera 168,300)
  gives each character a Booster Call and Booster Pack (1), and makes a Base Lv 1
  Novice a High Novice (transcended without a rebirth, like kRO's); the Booster Call
  (item 100337, its script was a TODO) summons the Booster Assistant next to the
  player (`duplicate_dynamic`), with the Booster Coin Shop, EXP potion exchange, daily
  hunting and item collection quests (reset at 4:00 server time). A Booster Equipment
  NPC (Prontera 166,300) trades tickets and coins for Metal, Booster, shadow and
  Illusion gear. The shops are barters in `npc/custom/booster_barters.yml`, imported by
  `npc/custom/barters.yml`, so they load even with the event off (nothing opens them).
- Job Master offers the 4th job change: its check kept the transcendent bit,
  which no 4th job's class has, so no transcendent 3rd job got the option.

## Population Engine

Fake players ("shells") from the
[Population Engine](https://github.com/YlenXWalker/Population-Engine) by Louis T
Steinhil (GPL-3.0), taken from its commit `2cfb45d` (built on the same rAthena
baseline): `src/map/population_engine*`, `db/population_*.yml`,
`conf/battle/population_engine.conf`, `npc/custom/population/arena.txt`, and the
engine's hooks in rAthena's own files. Left out: its committed `conf/import/`
and `db/import/` trees, the `PACKETVER` define in `src/custom/defines_pre.hpp`
(CMake sets it), the FluxCP `sql-files/population_engine/` table and the build
files we do not use. Our changes carry a `nostalRE` comment:

- `population_engine_enable` (off by default): when off, no yml is read, no
  timer runs, `@populate`/`@reloadpopenginedb` refuse and the arena NPC idles.
  `conf/battle_athena.conf` imports the engine conf before the app's import.
- The FluxCP stats query (MySQL-only syntax, a table we do not ship) is compiled
  out of the SQLite build.
- Shells can join a player's party as companions: `party_invite` on a shell, or a
  whispered "party" (which makes a party for a player without one); "leave" or
  "bye" dismisses one, and so does expelling it. A companion holds a member slot
  on the map server only (`party_companion_add`, `src/map/party.cpp`), so the
  char-server and the database never see it; one record in
  `population_engine.cpp` says whose it is, and every way out goes through
  `population_engine_companion_dismiss`. It follows its owner, across maps too,
  and helps in fights. It reacts to its owner's moves a moment late, trails a
  walking owner at its own distance, and around an owner standing still takes a
  free cell of its own by role (a tank in front, healers, casters and ranged
  weapons behind, the rest at a side), shifting now and then. It is a party member for EXP: Even Share pools its kills
  with the player's (a party made by the whisper starts on Even Share), Each Take
  keeps them its own, and its share is lost. The char-server's
  `party_share_level` gap applies to it too (`party_exp_share_allowed`, read from
  `inter_athena.conf` by the map server as well). Loot priority on its kills goes
  to its owner, like a mercenary's. It can die: Resurrection and the like revive
  it, and if nobody does it gets up next to its owner when they change map or
  after `population_engine_companion_revive_ms`. It leaves on logout, stays out
  of instances, and nothing brings it back after a restart.
  `population_engine_companion_max` sets the limit per player.
- `population_engine_max_count` 300.
- `population_engine_map_levels` (on): autosummoned shells take their level from
  the mean level of their map's permanent monsters, with a floor by class (a
  Knight is at least 45); towns and maps with no monsters use 30-85. A class
  whose floor is clearly above the map steps down (Knight to Swordsman to
  Novice); a first class on a map with monsters from level 40 may step up to a
  second class, and from level 60 always does (Swordsman to Knight or Crusader).
  Either way it still counts as its spawn-table job. Stats are fitted to what the
  level has in `db/re/statpoint.yml`, gear to what it can wear; a shell below its
  whole weapon pool gets the plainest weapon of that kind its job can wear.
  Off, and arena and `@populate` shells either way: the profile's range or 99/70.
  `@populate levels` lists the shells on your map.
- Personalities (`db/population_personality.yml`, `population_personality_db.cpp`):
  each autosummoned shell rolls a build and a temperament at spawn. A build is
  what a player made of their class and links to the other files: the jobs it is
  for (by weight), a skill set (a `Name:` entry in `db/population_skill_db.yml`,
  whose unnamed entry per job stays the default), a gear set from
  `db/population_gear_sets.yml`, stat ranges, names and chat. A temperament is how
  they play, whatever their class: reaction time when a fight starts, precision
  (the share of choices that are its best, and of dodges it makes) and idle
  pauses after a kill.
  Both weigh the kinds of skill they prefer. A job with no build keeps its
  `population_engine.yml` entry and still gets a temperament. Builds so far:
  Priest (support, battle), Wizard (area, bolts), Knight (two-handed, spear),
  Hunter (traps, arrows). `@populate levels` shows `build/temperament`, reaction,
  precision, the skills and basic attacks it chose and the pauses it took. Shells
  only act within sight of a player, as before.
- `population_engine_scored_skills` (on): a shell weighs its usable skills and a
  basic attack instead of rotating them: the skill's `Rate` as a weight, the
  element against the target, cast time and delay, SP, the monsters an area skill
  would hit, a primed combo finisher, and its personality's `Prefers`. Skill
  entries may say what they are for with `Kind:`. The conditions still decide
  what can be cast. A caster waits out its delay instead of meleeing. Off: the
  old rotation, with `population_engine_shell_basic_attack_chance`.
- Shell parties (`db/population_party.yml`, `population_engine_parties`, on): on
  the maps the file lists (Veins Field 03, Geffen Field 06, Sograt Desert 17)
  some shells play as a party. A party is a tactic and its slots, each slot a
  duty; the one tactic so far is `camp`: the leader (`anchor`) stands on a camp
  spot, half of them with a recruiting chat room; a `support` by them heals,
  buffs and raises the party's members and nobody else; `roamer`s hunt within
  `Roam` cells of the camp and walk back when they are hurt, out of SP, out of
  Blessing and Increase Agility or out long enough, wait there to be topped up
  and leave again; a `performer` Bard with a Dancer on the next cell keeps songs
  and ensembles up; a `battery` Professor gives SP with Soul Exchange and turns
  its own HP into SP, which the support heals back. Members at the camp fight
  only what attacks the camp, and those with nothing to cast sit down.
  A map entry says how many parties the map holds (scaled by
  `population_engine_density_pct`), where they camp (`Camps`, else a spot by one
  of the map's warps) and how likely the optional slots are there. A party takes
  its members out of the map's shells: as the map fills, the spawner gives its
  next shell to a party with a free place, in the place's class, and the quota
  counts it as the job it was going to be. Classes fit the map's level: on a map
  too low for a class the slot gets the class below, and a performer, a battery
  or a support who would be a Novice is left out. A party is its own party id on
  its members (in the fake range, so party skills and songs reach the party
  alone); it is not a real party and players cannot join one yet (a player who
  enters the leader's chat room is told it is full; a member invited to a
  player's party says it is in one). Members tick on any map a player is on,
  seen or not, or the roamers would never come back; they do not take portals.
  A member killed in sight of a healer with Resurrection waits a minute for it;
  any other gets up on its cell at the camp after the party's `Respawn`, the
  same shell (a plain shell's respawn makes a new one). Two skill conditions for
  it, `ally_dead` and `ally_sp_below`, and four skill sets (`priest_camp`,
  `professor_battery`, `bard_camp`, `dancer_camp`). `@populate parties` lists the
  map's parties and what each member is doing. Files:
  `config/population_party_db.cpp`, `runtime/population_party.cpp`, and the
  "shell parties" section of `population_engine.cpp`.
- Sages, Professors and Sorcerers cast Dispell only on players: a new skill
  condition, `enemy_is_player`, keeps it for PvP and the arena. Against monsters
  it was mostly wasted SP, and it could even thaw a monster someone had frozen.
- Vendor shells stand in the main towns: one `VendorPlacement:` list at the end
  of `db/population_vendors.yml` (a body entry without `VendorKey`) names the
  maps, Prontera with the most, each with a box around the town centre. Upstream
  put them all on `prt_mk`, which no warp leads to. A vendor's cell keeps 3 cells
  from any NPC or warp and skips `novending`; it holds its stall for a random
  shift (`population_engine_vendor_shift_min_ms`/`_max_ms`, 15-45 min), then a
  new vendor opens elsewhere in the box. Untradeable items are left out of the
  stock (one used to refuse the whole shop), and a shop that still fails is
  logged and its shell released. `@populate vendors` lists the map's shops.
- The app's AI shop settings, each a battle key: `population_engine_vendor_pct`
  (shops per town, apart from density), `_towns` (placement `Tier:` capital,
  main or minor; 12 minor towns added), `_price_pct`, `_loot`/`_supplies`/`_gear`
  (each shop's `Goods:`) and `_cards` (up to 2 non-MVP cards at `CardPrice:` in
  shops marked `CardsBySetting`). A merchant job lists its shops by weight in
  `VendorKeys:` and opens one of the kinds switched on; Alchemists and Creators
  sell potions and buff items, smiths dungeon finds or starter gear. With AI
  shops off, no shop merchants spawn (the pass counted open shops only and kept
  adding sitting merchants up to the cap).

## Building the Android servers

The app's servers are built for Renewal at packet version 20250716, with the
Android NDK (r30, `30.0.16248370`), CMake and Ninja:

```sh
NDK="$ANDROID_NDK_HOME"
cmake -S . -B build-android -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SQLITE=ON -DENABLE_WEB_SERVER=OFF -DPACKETVER=20250716 \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
  -DHAVE_TLS_EXITCODE=0 -DHAVE_MONOTONIC_CLOCK_EXITCODE=0 \
  -DRT_LIBRARY="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/26/libc.so" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-android/bin"
ninja -C build-android login-server char-server map-server
for exe in login-server char-server map-server; do
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "build-android/bin/$exe"
done
```

The app ships them renamed as `libloginserver.so`, `libcharserver.so` and
`libmapserver.so`.

Server data and database, as packed into the app:

```sh
mkdir -p stage
git ls-files -z conf db npc | tar --null -T - -cf - | tar -xf - -C stage
for t in conf/import-tmpl conf/msg_conf/import-tmpl db/import-tmpl; do
  [ -d "stage/${t%-tmpl}" ] || cp -r "stage/$t" "stage/${t%-tmpl}"
done
(cd stage && python3 -m zipfile -c ../server.zip conf db npc)
python3 tools/mysql2sqlite.py sql-files/main.sql sql-files/logs.sql \
  sql-files/web.sql sql-files/roulette_default_data.sql | sqlite3 -bail ragnarok.db
```

At runtime the app sets `RA_KEEP_CWD=1` and `RA_SQLITE_DIR` to the folder
holding `ragnarok.db`.

A host build uses the same CMake options without the toolchain arguments.

## Tags

- `nostalre-v0.1.0`: source of the servers in nostalRE v0.1.0.
- `nostalre-v0.1.1`: source of the servers in nostalRE v0.1.1 (drops go straight to the inventory).

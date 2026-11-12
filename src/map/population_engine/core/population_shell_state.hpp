// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
//
// Population shell: per-shell runtime state (sd->pop).

#ifndef POPULATION_SHELL_STATE_HPP
#define POPULATION_SHELL_STATE_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/timer.hpp> // t_tick

#include "population_engine_core.hpp"

namespace expanded_ai { class ExpandedCondition; }
struct PopBuild;       // nostalRE: population_personality_db.hpp
struct PopTemperament;

/// One offensive skill the population shell may rotate through (seeded from the PC skill tree or
/// population_skill_db.yml).  condition / cond_* mirror PopSkillCondition from population_skill_db.hpp
/// but stored as plain integers here to avoid cascading header dependencies.
struct PopulationShellCombatSkill {
	uint16_t skill_id   = 0;
	uint16_t skill_lv   = 0;
	uint16_t rate       = 10000; ///< 1–10000 per-10000; 0 = disabled
	bool     active     = true;
	uint8_t  condition  = 0;     ///< PopSkillCondition cast to uint8_t
	uint8_t  cond_value_num = 0; ///< Numeric threshold (HP%, SP%, cells, zone id)
	int16_t  cond_sc_resolved = -1; ///< Resolved sc_type for status conditions (-1 = none)
	uint8_t  target     = 0;    ///< 0=enemy (default), 2=ally (target=1 self skills go to buff list)
	uint8_t  around_range = 0;  ///< If >0, place ground/trap skills at a random cell within this radius of the shell
	bool     around_target = false; ///< When true, around_range centres on the enemy (mob_skill_db around5-8); when false (default), on self (around1-4)
	uint32_t cooldown_ms  = 0;  ///< Per-skill cooldown in ms after a successful cast (0 = no individual cooldown)
	uint8_t  kind = 0;             ///< nostalRE: PopSkillKind (YAML Kind:), for the scored picker
	std::shared_ptr<expanded_ai::ExpandedCondition> expanded; ///< Set iff condition == Expanded; evaluated in lieu of the flat condition fields.
};

/// One maintenance skill (Target:1=self, Target:2=ally) the population shell casts on upkeep.
/// Self skills re-cast whenever the associated SC expires.
/// Ally skills re-cast whenever a suitable ally (HP low, missing status, etc.) is found.
struct PopulationShellBuffSkill {
	uint16_t skill_id   = 0;
	uint16_t skill_lv   = 0;
	uint8_t  condition  = 0;     ///< PopSkillCondition cast to uint8_t
	uint8_t  cond_value_num = 0;
	int16_t  cond_sc_resolved = -1;
	uint8_t  target     = 1;     ///< 1=self, 2=ally
	uint8_t  around_range = 0;   ///< If >0, place ground/trap skills at a random cell within this radius of the shell
	bool     around_target = false; ///< When true, around_range centres on the enemy (mob_skill_db around5-8); when false (default), on self (around1-4)
	uint32_t cooldown_ms  = 0;   ///< Per-skill cooldown in ms after a successful cast (0 = no individual cooldown)
	uint8_t  kind = 0;             ///< nostalRE: PopSkillKind (YAML Kind:), for the scored picker
	std::shared_ptr<expanded_ai::ExpandedCondition> expanded; ///< Set iff condition == Expanded; evaluated in lieu of the flat condition fields.
};

/// Population engine shell state (sd->pop). Only meaningful for bots managed by the population engine;
/// real players leave this in its default-constructed state.
struct s_population {
	std::vector<PopulationShellCombatSkill> attack_skills; ///< Offensive skill rotation (not `ca.autocombatskills` / not autosupport).
	std::vector<PopulationShellBuffSkill>   buff_skills;   ///< Self-buff maintenance list (Target:1 from population_skill_db.yml).
	t_tick   reactive_buff_cd    = 0;  ///< Independent buff cooldown — decoupled from ca.skill_cd so self-buffs and attacks can fire in the same tick.
	t_tick   sticky_until        = 0;  ///< Tick at which the current sticky target commitment expires.
	uint32_t flags               = 0;  ///< PSF_* flag bitmask from population_engine.yml Flags: list.
	uint32_t sticky_target_id    = 0;  ///< Sticky-target commitment so the shell does not ping-pong between equidistant enemies. Standalone of autocombat's t3 sticky.
	int      max_attack_skill_range = -1; ///< Cached max range across attack_skills.
	int16_t  spawn_x             = 0;  ///< Spawn coordinates used by Guard behavior for return-to-post.
	int16_t  spawn_y             = 0;  ///< Spawn coordinates used by Guard behavior for return-to-post.
	int16_t  spawn_map_id        = -1; ///< Internal map ID of the designated spawn map (drift detection / death respawn).
	uint16_t spawn_job           = 0;  ///< nostalRE: spawn-table job the quota counts; the class may have stepped down for the map (0 = its class).
	int      respawn_timer       = -1; ///< INVALID_TIMER when no respawn pending; else timer id from add_timer. Cancel before re-adding to prevent duplicates if a shell dies twice within the respawn window.
	uint8_t  attack_skill_cursor = 0;  ///< Round-robin index into attack_skills.
	uint8_t  map_category        = 0;  ///< Map category at spawn (0=none, 1=town, 2=field, 3=dungeon).
	uint8_t  behavior            = 2;  ///< Resolved behavior (mirrors PopulationBehavior, stored as uint8). Default: Combat.
	uint8_t  behavior_base       = 2;  ///< Original configured behavior (for daily cycle restore).
	int8_t   arena_team          = 0;  ///< PvP arena team (0=not in arena, 1=teamA, 2=teamB). Set by population_arena_start.
	int8_t   role                = 0;  ///< Combat role (0=none, 1=tank, 2=support, 3=attacker). From Role: in population_engine.yml.
	uint8_t  db_source           = 0;  ///< PopulationDbSource (0=Main, 1=Pvp, 2=Vendor). Set at spawn time so runtime lookups can find the correct DB.
	t_tick   last_attacked_tick  = 0;  ///< gettick() when shell last received damage (any source). Powers SelfTargeted condition for PvP / non-mob attackers.
	uint32_t last_attacker_id    = 0;  ///< id of the last source that damaged this shell (0 if unknown).
	int      last_damage_received = 0; ///< Largest single-hit damage received (reset on each hit). Powers damagedgt condition.
	uint16_t last_cast_skill_id   = 0; ///< Skill ID of the last offensive/buff skill successfully cast by this shell. Powers afterskill combo condition.
	uint16_t last_skill_used_on_me = 0; ///< Skill ID of the last skill used AGAINST this shell (set in on_shell_damaged from src unit_data). Powers skillused condition.
	t_tick   last_skill_used_on_me_tick = 0; ///< Tick when last_skill_used_on_me was recorded. Used for 3s stale window in SkillUsed condition eval.
	t_tick   safetywall_kited_since = 0; ///< Tick when target first became unreachable while standing on SC_SAFETYWALL (0 = not kited). Used to abandon the wall after 4s so the bot can chase.

	// -----------------------------------------------------------------------
	// Standalone movement / combat runtime state
	// (was stored in sd->ca / s_autocombat in BanquetOfHeroes; moved here so
	//  the population engine has no dependency on the autocombat system)
	// -----------------------------------------------------------------------

	/// Which subsystem owns the walk token this tick (Roam, Combat, etc.).
	MovementOwner  movement_owner             = MovementOwner::None;
	MovementOwnerReason movement_owner_reason = MovementOwnerReason::None;
	uint32         movement_decision_trace_id = 0;
	t_tick         movement_owner_since_tick  = 0;
	t_tick         movement_owner_lock_until_tick = 0;
	bool           movement_emitted_this_tick = false;
	bool           movement_conflict_logged_this_tick = false;
	uint32         owner_switch_count  = 0;
	uint32         owner_blocked_count = 0;
	uint32         owner_noop_count    = 0;

	/// Last movement timestamp (gettick()).
	t_tick         last_move = 0;

	/// Last-known position (direction included for type-1 wander).
	s_pe_position  lastposition{};

	// --- Target / skill global cooldown ---
	int            target_id = 0;  ///< Active combat target (mob id or PC id in arena).
	t_tick         skill_cd  = 0;  ///< Global skill cooldown — no new skill until tick >= skill_cd.

	// --- Pathfinding ---
	std::vector<std::tuple<int, int>> path{}; ///< Pending waypoints (popped front-to-back).
	t_tick path_last_calc_tick = 0;

	// --- Session state ---
	/// Incremented whenever shell state is fully reset (map-change, disconnect, stop).
	/// 0 is reserved as unset; initial value is 1.
	uint32 session_guard       = 1;
	bool   initialized         = false; ///< True after first combat init (was: ca.client_addr != 0).
	uint32 unique_id           = 0;     ///< char_id or block id, set at init.
	bool   manual_stop_requested = false;
	int    action_on_end         = 0;   ///< 0=none, 1=return-to-savepoint, 2=relog.
	uint16 last_start_reject_code_raw = 0; ///< cast of CAStartRejectCode / rejection reason.
	/// Target IDs recently force-cleared, with the tick they were cleared.
	/// Prevents ping-pong re-targeting within PE_SHELL_TARGET_EXCLUSION_MS.
	std::unordered_map<uint32, t_tick> recently_cleared_targets{};

	// --- Local navigation FSM ---
	LocalNavState nav_state               = LocalNavState::DirectAdvance;
	int           nav_goal_x              = 0;
	int           nav_goal_y              = 0;
	int           nav_heading_x           = 0; ///< Normalized to -1/0/1.
	int           nav_heading_y           = 0; ///< Normalized to -1/0/1.
	t_tick        next_nav_attempt_tick   = 0;
	t_tick        last_nav_progress_tick  = 0;
	uint8         blocked_heading_streak  = 0;
	std::array<CANavBlockedCell, PE_NAV_BLOCKED_CELLS_MAX> nav_blocked_cells{};
	uint8         nav_blocked_cells_cursor = 0;

	// --- Roam anti-oscillation memory ---
	int    roam_last_target_x  = 0;
	int    roam_last_target_y  = 0;
	int    roam_last_dir_x     = 0; ///< Normalized to -1/0/1.
	int    roam_last_dir_y     = 0;
	t_tick roam_last_move_tick = 0;

	// --- Per-skill cooldown map (populated by skill-use logic) ---
	std::unordered_map<uint16, t_tick> skill_next_use_tick{};

	// --- Active buff tracking ---
	std::vector<s_pe_active_buff> active_buffs{};

	// --- Mob tracker ---
	s_pe_mob_tracker mob_tracker{};

	// --- Movetype-3 hotspot seeder ---
	std::array<Type3Hotspot, PE_TYPE3_HOTSPOTS_MAX> type3_hotspots{};

	// --- Movement emit tick (rate-limiter: one walk per tick) ---
	t_tick movement_emit_tick = 0; ///< Tick of last movement emission.

	// --- Pathfinding index / failure counters ---
	int    path_index          = 0; ///< Current index into path[] waypoint list.
	int    path_exec_fail_count = 0; ///< Consecutive ticks where position was stuck executing a path.
	t_tick last_move_fail      = 0; ///< Tick of last failed walk command (cooldown).

	// --- Attack / target tracking ---
	t_tick last_attack             = 0; ///< Tick of last normal or skill attack.
	t_tick last_target_switch_tick = 0; ///< Tick of last target switch.
	t_tick target_lost_tick        = 0; ///< Tick when the active target was lost.
	int    move_fail_count         = 0; ///< Consecutive movement failures chasing current target.
	int    attack_fail_count       = 0; ///< Consecutive attack command failures on current target.

	t_tick vendor_shift_end = 0;
	std::string vendor_key;         ///< nostalRE: vendor shell: the VendorKey of the shop it opened (empty = built-in stock).    ///< nostalRE: vendor shell: when it packs up its stall for a new one elsewhere (0 = never).

	// --- nostalRE: party companion ---
	uint32 companion_owner = 0;     ///< Shell: char id of the player whose party it joined (0 = plain shell).
	int32  companion_pending = 0;   ///< Player: id of the shell waiting for this player's new party (0 = none).
	uint32 party_join_pending = 0;  ///< Player: id of the shell party waiting for this player's new party (0 = none).
	t_tick companion_next_scan = 0; ///< Shell: when to look for mobs around the owner again.
	int16  companion_spot_x = -1;   ///< Shell: the cell it stands on next to its idle owner (-1 = none picked).
	int16  companion_spot_y = -1;
	int16  companion_seen_x = -1;   ///< Shell: where it last saw its owner; a change means the owner moved.
	int16  companion_seen_y = -1;
	t_tick companion_react_at = 0;  ///< Shell: when it reacts to the owner's move (0 = nothing to react to).
	t_tick companion_next_shuffle = 0; ///< Shell: when it may take a step while its owner stands still.

	// --- nostalRE: shell party (db/population_party.yml) ---
	uint32 party_group = 0;         ///< Shell: id of the party it is a member of (0 = none).
	uint8  party_duty = 0;          ///< PopPartyDuty.
	uint8  party_state = 0;         ///< Roamer: PopPartyRoam (at the camp, out hunting, on its way back).
	bool   party_regroup = false;   ///< Roamer: strayed past its leash, walking back within it.
	bool   party_buffed = false;    ///< Roamer: left the camp with the support's buffs on.
	bool   party_down = false;      ///< It died and has not been seen up since.
	int16  party_spot_x = -1;       ///< Its own cell at the camp.
	int16  party_spot_y = -1;
	t_tick party_next_scan = 0;     ///< When it looks again for monsters on the camp.
	t_tick party_idle_since = 0;    ///< On its cell with nothing to do since then (0 = busy).
	t_tick party_arrived_at = 0;    ///< Roamer: when it got back to the camp.
	t_tick party_wait_until = 0;    ///< Roamer at the camp: leaves by then, topped up or not.
	t_tick party_left_at = 0;       ///< Roamer: when it last left the camp.
	t_tick party_trip_until = 0;    ///< Roamer: heads back then (0 = no limit).
	t_tick party_ticked_at = 0;     ///< Its last duty tick: members stand still while no player is on their map.
	t_tick party_progress_at = 0;   ///< Walking to the camp: when it last got closer.
	int16  party_progress_dist = 0; ///< ... and how close that was.
	uint32 party_trips = 0;         ///< Roamer: times it came back (@populate parties).
	uint32 party_revived = 0;       ///< Times a member raised it (@populate parties).
	uint32 party_helps = 0;         ///< Skills it cast on its party's members (@populate parties).
	std::string party_skill_set;    ///< Its slot's SkillSet in population_skill_db.yml (empty: its build's or job's).

	// --- nostalRE: personality (db/population_personality.yml), rolled at spawn ---
	std::shared_ptr<const PopBuild> build;             ///< what it made of its class (nullptr: the job's own)
	std::shared_ptr<const PopTemperament> temperament; ///< how it plays (nullptr: no file, plays as before)
	std::array<uint16_t, 9> prefers{ 100, 100, 100, 100, 100, 100, 100, 100, 100 }; ///< % per PopSkillKind, build × temperament
	bool     melee_chosen = false;      ///< its last choice was a basic attack: it keeps swinging until the next one
	int16_t  melee_weight = -1;         ///< % score of a basic attack (-1: by weapon)
	uint16_t reaction_ms = 0;           ///< its usual delay before acting on something new
	uint8_t  precision = 100;           ///< % of choices that are its best one
	t_tick   react_until = 0;           ///< acts on its new target from then
	t_tick   pause_until = 0;           ///< idle pause: stands still until then
	t_tick   pause_from = 0;            ///< when the idle pause began (a hit after it ends the pause)
	t_tick   next_pause_at = 0;         ///< when the next idle pause starts (0: none planned)
	t_tick   panic_roll_until = 0;      ///< a missed dodge stays missed until then
	uint32_t acts_skill = 0;            ///< skills it cast on enemies (@populate levels)
	uint32_t acts_melee = 0;            ///< basic attacks it chose
	uint32_t pauses = 0;                ///< idle pauses it took (@populate levels)

	// --- Skill fail tracking ---
	t_tick last_skill_fail    = 0;  ///< Tick of last failed skill use.
	uint32 last_failed_target = 0;  ///< Target ID at last skill fail (0 = none).

	// --- Teleport tracking ---
	t_tick last_teleport = 0; ///< Tick of last warp / teleport.

	// --- Nearby mob tracking ---
	s_pe_mobs mobs{}; ///< Per-map mob-scan state (mapindex change detection).

	// --- Detection cache ---
	s_pe_detection_cache detection_cache{}; ///< Cached results of last nearest-mob scan.
};

#endif // POPULATION_SHELL_STATE_HPP

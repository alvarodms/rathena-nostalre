// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: shell parties at run time (db/population_party.yml, config/population_party_db.hpp).
//
// A party is a record here (g_pop_parties) and a party id of its own on its members, in the range of the fake
// ids shells carry (population_engine_spawn_shell), so party skills (songs, Gloria, Devotion) reach the party
// and not the shells around it. Nothing of it is a real party.
//
// A player can join one (population_engine.cpp's pop_party_on_join_request): a real party is made for them and
// the members take companion slots in it (party_companion_add), so the player sees their HP and shares EXP,
// and the party's skills reach them. The members keep their duties; they do not follow the player. The join
// ends when the player leaves the map, logs out or leaves the party (population_party_unjoin), and the
// members go back to the party's own id. population_party_check_joins puts right any path that missed it.
//
// population_engine.cpp makes the parties, rolls who is in them and has the spawner fill them (its "shell
// parties" section). This file is what a member does once it is on the map: population_party_member_tick, from
// population_engine_combat_per_tick. With the `camp` tactic every duty but the roamer's holds a cell at the
// camp and fights only what attacks the camp; a roamer hunts as a shell on its own between its visits.
//
// Shells only tick within sight of a player. A party's members tick on any map a player is on
// (population_party_tick_unseen), or its roamers would stop where the player lost sight of them.
//
// Built in population_engine_factory.cpp's unity unit after population_engine_combat.cpp, whose tick it uses.

#include "population_party.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include <common/random.hpp>
#include <common/showmsg.hpp>

#include "../config/population_party_db.hpp"
#include "../config/population_personality_db.hpp"
#include "../core/population_manners.hpp"
#include "../../chat.hpp"
#include "../../npc.hpp"

enum class PopPartyRoam : uint8_t { Rest = 0, Hunt, Return };

struct PopPartyMember {
	PopPartyDuty duty = PopPartyDuty::Roamer;
	uint8_t slot = 0;        ///< index into the template's slots
	uint16_t job = 0;        ///< fitted to the map's level
	std::string skill_set;
	std::string build;       ///< population_personality.yml build to take (empty: rolled)
	int32_t level = 0;       ///< base level, in its party's window (0: no map levels, rolled as any shell)
	int32 shell_id = 0;      ///< 0: not on the map yet
	int16 spot_x = 0, spot_y = 0; ///< its cell at the camp (0: not picked yet)
	int16 pair = -1;         ///< performer: the member it stands next to
	uint8_t spawn_fails = 0;
	bool out = false;        ///< out of the joined player's party (expelled, or no room): it stays out until the join ends
};

struct PopPartyGroup {
	uint32 id = 0;
	int16 m = -1;
	int16 camp_x = 0, camp_y = 0;
	int32_t level_lo = 0, level_hi = 0; ///< its members' base levels, within party_share_level of each other
	std::shared_ptr<const PopPartyTemplate> tpl;
	std::vector<PopPartyMember> members;
	std::string title;       ///< the leader's chat room (empty: none)
	int32 party_id = 0;      ///< the real party of the player who joined it (0: nobody joined)
	uint32 player_char_id = 0;
};

/// A place in a party for the spawner to fill.
struct PopPartySpawn {
	uint32 group = 0;
	size_t member = 0;
	uint16_t job = 0;
	int16 x = 0, y = 0;
	std::string build;
	int32_t level = 0;
};

static std::map<uint32, PopPartyGroup> g_pop_parties; // id -> party
static uint32 g_pop_party_serial = 0;

static constexpr int POP_PARTY_CAMP_SPACING = 30;  // cells between two camps on a map
static constexpr int POP_PARTY_WARP_CLEARANCE = 6; // and between a camp and a warp
static constexpr t_tick POP_PARTY_SCAN_MS = 500;
static constexpr t_tick POP_PARTY_STUCK_MS = 8000;        // walking back and no closer for this long: it warps
static constexpr t_tick POP_PARTY_MIN_HUNT_MS = 20000;    // out at least this long before SP, buffs or time send it back
static constexpr t_tick POP_PARTY_MIN_REST_MS = 3000;
static constexpr t_tick POP_PARTY_RAISE_WAIT_MS = 60000;  // a member killed in the healer's sight waits this long for them

static bool pop_shell_enter_map(map_session_data &shell, int16 m, int16 x, int16 y); // population_engine.cpp

/// The party id a party's members carry. Shells on their own carry 0x70000000 | map.
static int32 pop_party_fake_id(uint32 group_id)
{
	return static_cast<int32>(0x78000000u | (group_id & 0x00FFFFFFu));
}

static PopPartyGroup *pop_party_group(const map_session_data *sd)
{
	if (sd == nullptr || sd->pop.party_group == 0)
		return nullptr;
	const auto it = g_pop_parties.find(sd->pop.party_group);
	return it != g_pop_parties.end() ? &it->second : nullptr;
}

static int pop_party_member_index(const PopPartyGroup &g, const map_session_data *sd)
{
	for (size_t i = 0; i < g.members.size(); i++)
		if (g.members[i].shell_id == sd->id)
			return static_cast<int>(i);
	return -1;
}

/// The member's shell when it is on the map, dead or alive.
static map_session_data *pop_party_member_sd(const PopPartyGroup &g, const PopPartyMember &member)
{
	if (member.shell_id == 0)
		return nullptr;
	map_session_data *sd = map_id2sd(member.shell_id);
	if (sd == nullptr || sd->pop.party_group != g.id || sd->prev == nullptr || sd->m != g.m)
		return nullptr;
	return sd;
}

/// The player who joined the party, when they are on its map.
static map_session_data *pop_party_player(const PopPartyGroup &g)
{
	if (g.party_id == 0)
		return nullptr;
	map_session_data *sd = map_charid2sd(static_cast<int32>(g.player_char_id));
	if (sd == nullptr || sd->m != g.m || sd->prev == nullptr || sd->status.party_id != g.party_id)
		return nullptr;
	return sd;
}

/// Whether the member holds its slot in the joined player's party.
static bool pop_party_in_real_party(const PopPartyGroup &g, const map_session_data &sd)
{
	if (g.party_id == 0 || sd.status.party_id != g.party_id)
		return false;
	const party_data *p = party_search(g.party_id);
	if (p == nullptr)
		return false;
	for (int32 i = 0; i < MAX_PARTY; i++)
		if (p->data[i].sd == &sd)
			return true;
	return false;
}

/// The member takes its party id: its slot in the joined player's party, else the party's own.
static void pop_party_take_party_id(map_session_data &sd, const PopPartyGroup &g, const PopPartyMember &me)
{
	if (pop_party_in_real_party(g, sd))
		return;
	if (g.party_id != 0 && !me.out) {
		if (party_data *p = party_search(g.party_id); p != nullptr && party_companion_add(*p, sd, true))
			return;
	}
	sd.status.party_id = pop_party_fake_id(g.id);
}

/// Acolytes and Priests; Monks are their class too but do not heal.
static bool pop_party_job_heals(uint16_t job)
{
	const uint64 mapid = pc_jobid2mapid(job);
	return (mapid & MAPID_FIRSTMASK) == MAPID_ACOLYTE && (mapid & JOBL_2_2) == 0;
}

/// An anchor who heals and holds no chat room (nobody casts from one) is the party's support too.
static bool pop_party_member_heals(const PopPartyGroup &g, const PopPartyMember &member)
{
	return member.duty == PopPartyDuty::Support
		|| (member.duty == PopPartyDuty::Anchor && g.title.empty() && pop_party_job_heals(member.job));
}

/// A member who heals, alive and at the camp.
static map_session_data *pop_party_healer(const PopPartyGroup &g, const map_session_data *except = nullptr)
{
	for (const PopPartyMember &member : g.members) {
		if (!pop_party_member_heals(g, member))
			continue;
		map_session_data *sd = pop_party_member_sd(g, member);
		if (sd == nullptr || sd == except || pc_isdead(sd))
			continue;
		if (distance_xy(sd->x, sd->y, g.camp_x, g.camp_y) <= g.tpl->camp_defend)
			return sd;
	}
	return nullptr;
}

bool population_party_holds(const map_session_data *sd)
{
	if (sd == nullptr || sd->pop.party_group == 0)
		return false;
	return static_cast<PopPartyDuty>(sd->pop.party_duty) != PopPartyDuty::Roamer
		|| static_cast<PopPartyRoam>(sd->pop.party_state) != PopPartyRoam::Hunt;
}

bool population_party_ignores_ally(const map_session_data *shell, const map_session_data *ally)
{
	// the fake id is the party's alone: anyone else with the member's party id is in the player's party with it
	return shell->pop.party_group != 0 && shell->pop.party_group != ally->pop.party_group
		&& shell->status.party_id != ally->status.party_id;
}

int32_t population_engine_shell_party_members(const map_session_data &sd, map_session_data *out[], int32_t max)
{
	const PopPartyGroup *g = pop_party_group(&sd);
	if (g == nullptr || sd.status.party_id != pop_party_fake_id(g->id))
		return 0;
	int32_t count = 0;
	for (const PopPartyMember &member : g->members) {
		map_session_data *other = pop_party_member_sd(*g, member);
		if (other != nullptr && other->status.party_id == sd.status.party_id && count < max)
			out[count++] = other;
	}
	return count;
}

bool population_party_joined(const map_session_data *sd)
{
	const PopPartyGroup *g = pop_party_group(sd);
	return g != nullptr && g->party_id != 0 && sd->status.party_id == g->party_id;
}

// ---- the camp: where it is, and each member's cell at it ----------------------------------------------------

/// Room for a party to stand around the cell.
static bool pop_party_cell_is_open(int16 m, int16 x, int16 y)
{
	int passable = 0;
	for (int dy = -3; dy <= 3; dy++)
		for (int dx = -3; dx <= 3; dx++)
			if (map_getcell(m, static_cast<int16>(x + dx), static_cast<int16>(y + dy), CELL_CHKPASS))
				passable++;
	return passable >= 40; // of 49
}

/// A camp spot for a party with none given: by one of the map's warps, where parties gathered, else anywhere
/// open; away from the map's other camps and off the warps themselves.
static bool pop_party_pick_camp(int16 m, int16 &out_x, int16 &out_y)
{
	static constexpr double PI = 3.14159265358979323846;
	const struct map_data *mapdata = map_getmapdata(m);
	if (mapdata == nullptr || mapdata->cell == nullptr)
		return false;

	std::vector<std::pair<int16, int16>> warps;
	for (int32 i = 0; i < mapdata->npc_num; i++) {
		const npc_data *nd = mapdata->npc[i];
		if (nd != nullptr && nd->subtype == NPCTYPE_WARP)
			warps.emplace_back(nd->x, nd->y);
	}
	const auto clear_of = [](const std::vector<std::pair<int16, int16>> &cells, int16 x, int16 y, int cells_apart) {
		for (const auto &c : cells)
			if (distance_xy(c.first, c.second, x, y) < cells_apart)
				return false;
		return true;
	};
	std::vector<std::pair<int16, int16>> camps;
	for (const auto &entry : g_pop_parties)
		if (entry.second.m == m)
			camps.emplace_back(entry.second.camp_x, entry.second.camp_y);

	const int margin_x = std::min<int>(30, std::max<int>(4, mapdata->xs / 6));
	const int margin_y = std::min<int>(30, std::max<int>(4, mapdata->ys / 6));
	for (int attempt = 0; attempt < 120; attempt++) {
		int x, y;
		if (!warps.empty() && attempt < 80) {
			const auto &warp = warps[rnd() % warps.size()];
			const double angle = (static_cast<double>(rnd_value(0, 3600)) / 3600.0) * 2 * PI;
			const int dist = rnd_value(8, 20);
			x = warp.first + static_cast<int>(std::lround(std::cos(angle) * dist));
			y = warp.second + static_cast<int>(std::lround(std::sin(angle) * dist));
		} else {
			x = rnd_value(margin_x, std::max<int>(margin_x, mapdata->xs - margin_x - 1));
			y = rnd_value(margin_y, std::max<int>(margin_y, mapdata->ys - margin_y - 1));
		}
		if (x < 4 || y < 4 || x >= mapdata->xs - 4 || y >= mapdata->ys - 4)
			continue;
		const int16 cx = static_cast<int16>(x), cy = static_cast<int16>(y);
		// the last tries give up the spacing between camps, so a small map still gets its parties
		if (!map_getcell(m, cx, cy, CELL_CHKPASS) || !pop_party_cell_is_open(m, cx, cy)
			|| !clear_of(warps, cx, cy, POP_PARTY_WARP_CLEARANCE)
			|| !clear_of(camps, cx, cy, attempt < 100 ? POP_PARTY_CAMP_SPACING : 12))
			continue;
		out_x = cx;
		out_y = cy;
		return true;
	}
	return false;
}

/// A member's own cell at the camp: the leader on the camp spot, a support right by them, a performer next to
/// its partner, the others around, each with a cell between it and the next where there is room.
static void pop_party_pick_spot(PopPartyGroup &g, size_t index)
{
	PopPartyMember &me = g.members[index];
	if (me.spot_x > 0)
		return;
	me.spot_x = g.camp_x;
	me.spot_y = g.camp_y;
	if (me.duty == PopPartyDuty::Anchor)
		return;

	const auto taken = [&g, index](int16 x, int16 y, int gap) {
		for (size_t i = 0; i < g.members.size(); i++) {
			const PopPartyMember &other = g.members[i];
			if (i != index && other.spot_x > 0 && distance_xy(other.spot_x, other.spot_y, x, y) <= gap)
				return true;
		}
		return false;
	};
	const auto usable = [&g](int16 x, int16 y) {
		return map_getcell(g.m, x, y, CELL_CHKPASS) && !map_getcell(g.m, x, y, CELL_CHKNPC)
			&& path_search(nullptr, g.m, g.camp_x, g.camp_y, x, y, 0, CELL_CHKNOPASS);
	};

	if (me.pair >= 0 && g.members[me.pair].spot_x > 0) {
		const PopPartyMember &partner = g.members[me.pair];
		const int first = rnd_value(0, 7);
		for (int i = 0; i < 8; i++) {
			const int dir = (first + i) % 8;
			const int16 x = static_cast<int16>(partner.spot_x + dirx[dir]), y = static_cast<int16>(partner.spot_y + diry[dir]);
			if (usable(x, y) && !taken(x, y, 0)) {
				me.spot_x = x;
				me.spot_y = y;
				return;
			}
		}
	}

	const int radius = g.tpl->camp_radius + (me.duty == PopPartyDuty::Roamer ? 2 : 0);
	for (int attempt = 0; attempt < 60; attempt++) {
		const int reach = me.duty == PopPartyDuty::Support && attempt < 20 ? 2 : radius;
		const int16 x = static_cast<int16>(g.camp_x + rnd_value(-reach, reach));
		const int16 y = static_cast<int16>(g.camp_y + rnd_value(-reach, reach));
		if (!usable(x, y) || taken(x, y, attempt < 40 ? 1 : 0))
			continue;
		me.spot_x = x;
		me.spot_y = y;
		return;
	}
}

// ---- joining and leaving ---------------------------------------------------------------------------------

/// The leader's chat room closes: whoever is in it leaves first, or the room would pass to them.
static void pop_party_close_room(map_session_data *sd)
{
	const chat_data *cd = sd->chatID != 0 ? static_cast<const chat_data *>(map_id2bl(sd->chatID)) : nullptr;
	if (cd != nullptr && cd->owner == sd) {
		for (int guard = 0; cd->users > 1 && guard < MAX_CHAT_USERS; guard++) {
			map_session_data *other = cd->usersd[cd->users - 1];
			if (other == sd || chat_leavechat(other, false) != 0)
				break;
		}
	}
	if (sd->chatID != 0)
		chat_leavechat(sd, false);
}

static void pop_party_get_up(map_session_data *sd)
{
	if (sd->chatID != 0)
		pop_party_close_room(sd);
	if (pc_issit(sd) && pc_setstand(sd, false))
		clif_standing(*sd);
}

static void pop_party_sit(map_session_data *sd)
{
	if (pc_issit(sd) || pc_isdead(sd))
		return;
	pc_setsit(sd);
	clif_sitting(*sd);
}

/// A shell the spawner made for the party takes its place in it.
static void population_party_attach(map_session_data &sd, uint32 group_id, size_t index)
{
	const auto it = g_pop_parties.find(group_id);
	if (it == g_pop_parties.end() || index >= it->second.members.size())
		return;
	PopPartyGroup &g = it->second;
	PopPartyMember &me = g.members[index];
	const t_tick now = gettick();

	me.shell_id = sd.id;
	me.spawn_fails = 0;
	s_population &pe = sd.pop;
	pe.party_group = g.id;
	pe.party_duty = static_cast<uint8>(me.duty);
	pe.party_state = static_cast<uint8>(PopPartyRoam::Rest);
	pe.party_spot_x = me.spot_x;
	pe.party_spot_y = me.spot_y;
	pe.party_regroup = pe.party_buffed = false;
	pe.party_next_scan = pe.party_idle_since = pe.party_left_at = pe.party_trip_until = pe.party_progress_at = 0;
	pe.party_arrived_at = now;
	pe.party_wait_until = now + rnd_value(4000, 15000); // the party gathers, then its roamers head out
	pe.party_skill_set = me.skill_set;
	pe.spawn_x = me.spot_x; // where it gets up after a death nobody raised it from
	pe.spawn_y = me.spot_y;
	pe.flags |= PSF::Mortal; // a party is about heals: its members can be hurt
	pe.behavior = pe.behavior_base = static_cast<uint8_t>(PopulationBehavior::Combat);
	me.out = false;
	pop_party_take_party_id(sd, g, me); // a member that comes after a player joined is in their party too
	population_engine_path_erase_for_pc(sd.id); // no wandering

	// its slot's skill set, whatever the spawn seeded from its job
	pe.attack_skills.clear();
	pe.attack_skill_cursor = 0;
	pe.buff_skills.clear();
	if (sd.state.population_combat)
		population_shell_seed_attack_skills_if_empty(&sd);
	else
		population_engine_combat_start_session(&sd, PopulationCombatStartMode::AutoCombat, -1, SCSTART_NOAVOID | SCSTART_LOADED);
}

/// The shell is a shell on its own again (or about to be released): its place in the party is free.
static void population_party_detach(map_session_data &sd)
{
	if (sd.pop.party_group == 0)
		return;
	if (PopPartyGroup *g = pop_party_group(&sd)) {
		for (PopPartyMember &member : g->members)
			if (member.shell_id == sd.id)
				member.shell_id = 0;
	}
	if (sd.status.party_id != 0)
		party_companion_clear_slot(sd, sd.status.party_id, PARTY_MEMBER_WITHDRAW_LEAVE); // a joined player's party
	sd.pop.party_group = 0;
	sd.pop.party_duty = 0;
	sd.pop.party_state = 0;
	sd.pop.party_skill_set.clear();
	sd.status.party_id = static_cast<int32>(0x70000000u | static_cast<uint32>(static_cast<uint16>(sd.m)));
}

/// The party is over: members still on the map play on as shells on their own.
static void population_party_disband(PopPartyGroup &g)
{
	g.party_id = 0; // population_party_detach empties their slots in a joined player's party
	g.player_char_id = 0;
	for (PopPartyMember &member : g.members) {
		map_session_data *sd = member.shell_id != 0 ? map_id2sd(member.shell_id) : nullptr;
		if (sd == nullptr || sd->pop.party_group != g.id)
			continue;
		if (sd->prev != nullptr)
			pop_party_get_up(sd);
		population_party_detach(*sd);
	}
	g.members.clear();
}

// ---- duties ----------------------------------------------------------------------------------------------

/// Walks to a cell: straight there when it is close, else a leg of the way (unit_walktoxy refuses paths longer
/// than max_walk_path).
static bool pop_party_walk_to(map_session_data *sd, int16 x, int16 y, t_tick now)
{
	if (!population_shell_can_emit_movement(sd, MovementOwner::Combat, "population_party:walk"))
		return false;
	const int dist = distance_xy(sd->x, sd->y, x, y);
	const int leg = std::max(4, battle_config.max_walk_path - 5);
	int tx = x, ty = y;
	if (dist > leg) {
		tx = sd->x + (x - sd->x) * leg / dist;
		ty = sd->y + (y - sd->y) * leg / dist;
	}
	for (int attempt = 0; attempt < 6; attempt++) {
		// the cell itself, then cells around it: a leg's end may be a wall or a cliff
		const int16 cx = static_cast<int16>(tx + (attempt != 0 ? rnd_value(-3, 3) : 0));
		const int16 cy = static_cast<int16>(ty + (attempt != 0 ? rnd_value(-3, 3) : 0));
		if ((cx == sd->x && cy == sd->y) || !map_getcell(sd->m, cx, cy, CELL_CHKPASS))
			continue;
		if (unit_walktoxy(sd, cx, cy, 4)) {
			sd->pop.last_move = now;
			return true;
		}
	}
	sd->pop.movement_emitted_this_tick = false;
	return false;
}

/// Puts a member on a cell of its map, as a player using a Fly Wing arrives.
static void pop_party_warp(map_session_data *sd, int16 x, int16 y)
{
	const int16 m = sd->m, old_x = sd->x, old_y = sd->y;
	population_shell_target_change(sd, 0);
	unit_remove_map_pc(sd, CLR_TELEPORT);
	if (!pop_shell_enter_map(*sd, m, x, y))
		pop_shell_enter_map(*sd, m, old_x, old_y);
	sd->pop.last_move = gettick();
}

struct PopPartyDefendCtx {
	map_session_data *shell;
	const PopPartyGroup *group;
	int best_id = 0, best_dist = INT_MAX;
};

/// The nearest monster that is after one of the party's members at the camp, or one on its way back to it,
/// or after the player who joined it. What a roamer is hunting next to the camp is the roamer's.
static int32 pop_party_defend_scan_cb(block_list *bl, va_list ap)
{
	mob_data *md = BL_CAST(BL_MOB, bl);
	PopPartyDefendCtx *ctx = va_arg(ap, PopPartyDefendCtx *);
	if (md == nullptr || md->target_id == 0)
		return 0;
	const map_session_data *victim = map_id2sd(md->target_id);
	if (victim == nullptr)
		return 0;
	const bool member = victim->pop.party_group == ctx->group->id && population_party_holds(victim);
	if (!member && victim != pop_party_player(*ctx->group))
		return 0;
	if (!population_shell_check_target_for_movement(ctx->shell, bl->id))
		return 0;
	const int dist = distance_bl(ctx->shell, bl);
	if (dist < ctx->best_dist) {
		ctx->best_id = bl->id;
		ctx->best_dist = dist;
	}
	return 0;
}

/// A member at the camp fights what attacks the camp and nothing else.
static void pop_party_pick_defend_target(map_session_data *sd, const PopPartyGroup &g, t_tick now)
{
	s_population &pe = sd->pop;
	const int reach = g.tpl->camp_defend;
	if (pe.target_id != 0) {
		const block_list *target = map_id2bl(static_cast<int>(pe.target_id));
		if (target != nullptr && target->m == g.m && distance_xy(target->x, target->y, g.camp_x, g.camp_y) <= reach + 3
			&& population_shell_check_target_for_movement(sd, pe.target_id))
			return;
		population_shell_target_change(sd, 0);
	}
	if (DIFF_TICK(now, pe.party_next_scan) < 0)
		return;
	pe.party_next_scan = now + POP_PARTY_SCAN_MS;

	PopPartyDefendCtx ctx{ sd, &g };
	map_foreachinallarea(pop_party_defend_scan_cb, g.m, g.camp_x - reach, g.camp_y - reach, g.camp_x + reach, g.camp_y + reach,
		BL_MOB, &ctx);
	if (ctx.best_id != 0)
		population_shell_target_change(sd, ctx.best_id);
}

/// On its cell with nothing to do: the leader opens the chat room, and whoever casts nothing sits down.
static void pop_party_settle(map_session_data *sd, const PopPartyGroup &g, const PopPartyMember &me, t_tick now, bool wants_rest)
{
	s_population &pe = sd->pop;
	if (pe.party_idle_since == 0)
		pe.party_idle_since = now;
	if (DIFF_TICK(now, pe.party_idle_since) < 3000 + static_cast<t_tick>(sd->id % 5) * 1000)
		return;

	const bool leader_idles = me.duty == PopPartyDuty::Anchor && !pop_party_member_heals(g, me);
	if (g.tpl->sit && (leader_idles || wants_rest))
		pop_party_sit(sd);
	if (me.duty == PopPartyDuty::Anchor && !g.title.empty() && g.party_id == 0 && sd->chatID == 0)
		chat_createpcchat(sd, g.title.c_str(), "", 20, true); // not while a player is with them
}

/// Back to its cell after a fight; settles once it is there.
static void pop_party_hold_spot(map_session_data *sd, const PopPartyGroup &g, const PopPartyMember &me, t_tick now, bool wants_rest)
{
	s_population &pe = sd->pop;
	if (sd->x == me.spot_x && sd->y == me.spot_y) {
		pop_party_settle(sd, g, me, now, wants_rest);
		return;
	}
	pe.party_idle_since = 0;
	pop_party_get_up(sd);
	if (unit_is_walking(sd) && sd->ud.to_x == me.spot_x && sd->ud.to_y == me.spot_y)
		return;
	if (unit_is_walking(sd) && distance_xy(sd->ud.to_x, sd->ud.to_y, me.spot_x, me.spot_y) < distance_xy(sd->x, sd->y, me.spot_x, me.spot_y))
		return; // on a leg of the way
	pop_party_walk_to(sd, me.spot_x, me.spot_y, now);
}

static bool pop_party_can_raise(const map_session_data *sd)
{
	const auto &skills = sd->pop.attack_skills;
	return std::any_of(skills.begin(), skills.end(), [](const PopulationShellCombatSkill &sk) { return sk.skill_id == ALL_RESURRECTION; });
}

/// A healer with Resurrection walks up to a member who died in its sight, or to the player who joined the party,
/// as far as the skill reaches, and stays there until they are up. False: nobody to raise.
static bool pop_party_go_raise(map_session_data *sd, const PopPartyGroup &g, const PopPartyMember &me, t_tick now)
{
	static constexpr int REACH = 7; // Resurrection's range is 9
	if (!pop_party_member_heals(g, me) || !pop_party_can_raise(sd))
		return false;
	const map_session_data *fallen = nullptr;
	int best = AREA_SIZE + 1;
	const auto consider = [&](const map_session_data *other) {
		if (other == nullptr || other == sd || !pc_isdead(other))
			return;
		const int dist = distance_bl(sd, other);
		if (dist < best) {
			best = dist;
			fallen = other;
		}
	};
	for (const PopPartyMember &member : g.members)
		consider(pop_party_member_sd(g, member));
	consider(pop_party_player(g));
	if (fallen == nullptr)
		return false;
	sd->pop.party_idle_since = 0;
	if (best > REACH && !unit_is_walking(sd)) {
		pop_party_get_up(sd);
		// towards the member, a cell short of where the skill reaches
		const int16 x = static_cast<int16>(fallen->x + (sd->x - fallen->x) * (REACH - 1) / best);
		const int16 y = static_cast<int16>(fallen->y + (sd->y - fallen->y) * (REACH - 1) / best);
		pop_party_walk_to(sd, x, y, now);
	}
	return true;
}

/// A member at the camp: it fights what attacks the camp, keeps its heals, buffs and songs going (the skill
/// picker's part, with the party as its only allies), and holds its cell.
static void pop_party_camp_tick(map_session_data *sd, const PopPartyGroup &g, const PopPartyMember &me, t_tick now, bool do_skills, bool wants_rest)
{
	s_population &pe = sd->pop;
	pop_party_pick_defend_target(sd, g, now);
	if (pe.target_id != 0) {
		pe.party_idle_since = 0;
		pop_party_get_up(sd); // nobody fights sitting down or from a chat room
	}
	if (!pc_issit(sd) && sd->chatID == 0)
		population_shell_combat_process_tick(sd, now, do_skills);
	if (pe.target_id != 0 || sd->ud.skilltimer != INVALID_TIMER)
		return;
	if (pop_party_go_raise(sd, g, me, now))
		return;
	pop_party_hold_spot(sd, g, me, now, wants_rest);
}

static int pop_party_pct(uint32 value, uint32 max)
{
	return max > 0 ? static_cast<int>(static_cast<uint64>(value) * 100 / max) : 100;
}

static bool pop_party_has_buffs(const map_session_data *sd)
{
	return sd->sc.getSCE(SC_BLESSING) != nullptr || sd->sc.getSCE(SC_INCREASEAGI) != nullptr;
}

/// Out hunting: is it time to go back to the camp?
static bool pop_party_should_return(const map_session_data *sd, const PopPartySlot &slot, t_tick now)
{
	const s_population &pe = sd->pop;
	if (sd->ud.skilltimer != INVALID_TIMER)
		return false;
	if (pop_party_pct(sd->battle_status.hp, sd->battle_status.max_hp) < slot.return_hp)
		return true;
	// the rest can wait for the fight at hand, and for a while after it left
	if (pe.target_id != 0 || DIFF_TICK(now, pe.party_left_at) < POP_PARTY_MIN_HUNT_MS)
		return false;
	if (pop_party_pct(sd->battle_status.sp, sd->battle_status.max_sp) < slot.return_sp)
		return true;
	if (pe.party_trip_until != 0 && DIFF_TICK(now, pe.party_trip_until) >= 0)
		return true;
	return slot.return_unbuffed && pe.party_buffed && !pop_party_has_buffs(sd);
}

/// At the camp: is it ready to leave again?
static bool pop_party_should_leave(const map_session_data *sd, const PopPartyGroup &g, const PopPartySlot &slot, t_tick now)
{
	const s_population &pe = sd->pop;
	if (pe.target_id != 0 || sd->ud.skilltimer != INVALID_TIMER || DIFF_TICK(now, pe.party_arrived_at) < POP_PARTY_MIN_REST_MS)
		return false;
	if (DIFF_TICK(now, pe.party_wait_until) >= 0)
		return true;
	if (pop_party_pct(sd->battle_status.hp, sd->battle_status.max_hp) < slot.leave_hp
		|| pop_party_pct(sd->battle_status.sp, sd->battle_status.max_sp) < slot.leave_sp)
		return false;
	// with a healer at the camp it waits for Blessing and Increase Agility
	return pop_party_healer(g, sd) == nullptr
		|| (sd->sc.getSCE(SC_BLESSING) != nullptr && sd->sc.getSCE(SC_INCREASEAGI) != nullptr);
}

static void pop_party_roamer_arrive(map_session_data *sd, const PopPartySlot &slot, t_tick now)
{
	s_population &pe = sd->pop;
	pe.party_state = static_cast<uint8>(PopPartyRoam::Rest);
	pe.party_arrived_at = now;
	pe.party_wait_until = now + 1000 * static_cast<t_tick>(rnd_value(slot.wait_min, slot.wait_max));
	pe.party_idle_since = 0;
	pe.party_regroup = false;
}

/// Whether the party's battery (a Professor with Soul Exchange) is up on the map.
static bool pop_party_has_battery(const PopPartyGroup &g)
{
	for (const PopPartyMember &member : g.members) {
		const map_session_data *sd = member.duty == PopPartyDuty::Battery ? pop_party_member_sd(g, member) : nullptr;
		if (sd != nullptr && !pc_isdead(sd))
			return true;
	}
	return false;
}

/// How far from the camp a roamer hunts: its slot's Roam (or the whole map), held nearer by its temperament's Leash.
static int pop_party_roam_leash(const map_session_data *sd, const PopPartySlot &slot)
{
	const map_data *mapdata = map_getmapdata(sd->m);
	const PopTemperament *t = sd->pop.temperament.get();
	return pop_manners::roam_leash(slot.roam, mapdata != nullptr ? mapdata->xs : 0, mapdata != nullptr ? mapdata->ys : 0,
		t != nullptr ? t->leash : 0);
}

/// A roamer: out hunting it is a shell on its own (false: the caller plays it on), kept within its leash of
/// the camp; when it is hurt, out of SP, out of buffs or out long enough it walks back, waits at the camp to
/// be healed and buffed, and leaves again.
static bool pop_party_roamer_tick(map_session_data *sd, const PopPartyGroup &g, const PopPartyMember &me, const PopPartySlot &slot,
	t_tick now, bool do_skills)
{
	s_population &pe = sd->pop;

	if (static_cast<PopPartyRoam>(pe.party_state) == PopPartyRoam::Hunt) {
		if (!pop_party_should_return(sd, slot, now)) {
			const int camp_dist = distance_xy(sd->x, sd->y, g.camp_x, g.camp_y);
			const int leash = pop_party_roam_leash(sd, slot);
			if (camp_dist > leash)
				pe.party_regroup = true;
			else if (camp_dist <= leash * 6 / 10)
				pe.party_regroup = false;
			if (!pe.party_regroup)
				return false;
			// past its leash: back towards the camp before it looks for monsters again
			if (pe.target_id != 0)
				population_shell_target_change(sd, 0);
			if (sd->ud.skilltimer == INVALID_TIMER && !unit_is_walking(sd))
				pop_party_walk_to(sd, g.camp_x, g.camp_y, now);
			return true;
		}
		pe.party_state = static_cast<uint8>(PopPartyRoam::Return);
		pe.party_progress_at = 0;
		pe.party_regroup = false;
	}

	if (static_cast<PopPartyRoam>(pe.party_state) == PopPartyRoam::Return) {
		if (pe.target_id != 0)
			population_shell_target_change(sd, 0); // what follows it, the camp deals with
		if (sd->ud.skilltimer != INVALID_TIMER)
			return true;
		const int16 dist = static_cast<int16>(distance_xy(sd->x, sd->y, me.spot_x, me.spot_y));
		if (dist <= 1) {
			pe.party_trips++;
			pop_party_roamer_arrive(sd, slot, now);
			return true;
		}
		if (pe.party_progress_at == 0 || dist < pe.party_progress_dist) {
			pe.party_progress_at = now;
			pe.party_progress_dist = dist;
		} else if (DIFF_TICK(now, pe.party_progress_at) > POP_PARTY_STUCK_MS) {
			pop_party_warp(sd, me.spot_x, me.spot_y); // no way round on foot
			pe.party_progress_at = 0;
			return true;
		}
		if (!unit_is_walking(sd))
			pop_party_walk_to(sd, me.spot_x, me.spot_y, now);
		return true;
	}

	// at the camp: low on SP it sits down to get it back, unless a Professor is there to give it some
	const bool wants_rest = (pop_party_pct(sd->battle_status.sp, sd->battle_status.max_sp) < slot.leave_sp && !pop_party_has_battery(g))
		|| (pop_party_pct(sd->battle_status.hp, sd->battle_status.max_hp) < slot.leave_hp && pop_party_healer(g, sd) == nullptr);
	pop_party_camp_tick(sd, g, me, now, do_skills, wants_rest);
	if (pop_party_should_leave(sd, g, slot, now)) {
		pop_party_get_up(sd);
		pe.party_state = static_cast<uint8>(PopPartyRoam::Hunt);
		pe.party_left_at = now;
		pe.party_trip_until = slot.trip_max > 0 ? now + 1000 * static_cast<t_tick>(rnd_value(slot.trip_min, slot.trip_max)) : 0;
		pe.party_buffed = pop_party_has_buffs(sd);
		pe.party_idle_since = 0;
	}
	return true;
}

bool population_party_member_tick(map_session_data *sd, t_tick now, bool do_skills)
{
	PopPartyGroup *g = pop_party_group(sd);
	const int index = g != nullptr ? pop_party_member_index(*g, sd) : -1;
	if (index < 0 || sd->m != g->m) { // its party is gone
		population_party_detach(*sd);
		return false;
	}
	const PopPartyMember &me = g->members[index];
	// nobody was on the map for a while: the time it stood still is not time it was stuck
	if (DIFF_TICK(now, sd->pop.party_ticked_at) > 2000)
		sd->pop.party_progress_at = 0;
	sd->pop.party_ticked_at = now;
	if (sd->pop.party_down) { // up before its respawn: a member raised it
		sd->pop.party_down = false;
		sd->pop.party_revived++;
	}
	population_shell_reset_movement_tick_state(sd, now);
	if (me.duty == PopPartyDuty::Roamer)
		return pop_party_roamer_tick(sd, *g, me, g->tpl->slots[me.slot], now, do_skills);
	pop_party_camp_tick(sd, *g, me, now, do_skills, false);
	return true;
}

/// Ticks the members no player has in sight, on maps a player is on. `ticked`: the shells this pass has done.
static void population_party_tick_unseen(std::unordered_set<int32> &ticked)
{
	for (auto &entry : g_pop_parties) {
		PopPartyGroup &g = entry.second;
		if (!population_engine_map_has_real_players(g.m))
			continue;
		for (size_t i = 0; i < g.members.size(); i++) {
			map_session_data *sd = pop_party_member_sd(g, g.members[i]);
			if (sd == nullptr || !sd->state.active || !sd->state.population_combat || !ticked.insert(sd->id).second)
				continue;
			population_engine_combat_per_tick(sd, true);
		}
	}
}

// ---- a player in the party -------------------------------------------------------------------------------

static PopPartyGroup *pop_party_find(uint32 group_id)
{
	const auto it = g_pop_parties.find(group_id);
	return it != g_pop_parties.end() ? &it->second : nullptr;
}

/// The player's party `p` takes the party's members in, as companions it does not lead: they keep their duties.
/// The leader closes their chat room while the player is with them.
static void population_party_join(PopPartyGroup &g, party_data &p, map_session_data &player)
{
	g.party_id = p.party.party_id;
	g.player_char_id = player.status.char_id;
	for (PopPartyMember &member : g.members) {
		member.out = false;
		map_session_data *sd = pop_party_member_sd(g, member);
		if (sd == nullptr)
			continue; // not on the map yet: population_party_attach puts it in
		if (member.duty == PopPartyDuty::Anchor && sd->chatID != 0)
			pop_party_close_room(sd);
		if (!party_companion_add(p, *sd, true))
			member.out = true; // no room: it stays the party's own
	}
}

/// The player is gone: the members still in their party go back to the party's own id.
static void population_party_unjoin(PopPartyGroup &g)
{
	const int32 party_id = g.party_id;
	g.party_id = 0;
	g.player_char_id = 0;
	for (PopPartyMember &member : g.members) {
		member.out = false;
		map_session_data *sd = member.shell_id != 0 ? map_id2sd(member.shell_id) : nullptr;
		if (sd == nullptr || sd->pop.party_group != g.id)
			continue;
		if (party_id != 0 && sd->status.party_id == party_id)
			party_companion_clear_slot(*sd, party_id, PARTY_MEMBER_WITHDRAW_LEAVE);
		sd->status.party_id = pop_party_fake_id(g.id);
		if (sd->prev != nullptr)
			clif_name_area(sd);
	}
}

/// The player expelled a member (or it left their party some other way): it stays at the camp in the
/// party's own id until the join ends. False: the shell is no joined party's member.
static bool population_party_member_leaves(map_session_data &sd, bool expelled)
{
	PopPartyGroup *g = pop_party_group(&sd);
	const int index = g != nullptr ? pop_party_member_index(*g, &sd) : -1;
	if (index < 0 || g->party_id == 0 || sd.status.party_id != g->party_id)
		return false;
	g->members[index].out = true;
	party_companion_clear_slot(sd, g->party_id, expelled ? PARTY_MEMBER_WITHDRAW_EXPEL : PARTY_MEMBER_WITHDRAW_LEAVE);
	sd.status.party_id = pop_party_fake_id(g->id);
	if (sd.prev != nullptr)
		clif_name_area(&sd);
	return true;
}

/// The party a player joined is gone from under them (they left it, it broke up) or they logged out:
/// `char_id` 0 matches by party alone.
static void population_party_unjoin_where(uint32 char_id, int32 party_id)
{
	for (auto &entry : g_pop_parties) {
		PopPartyGroup &g = entry.second;
		if (g.party_id != 0 && (char_id != 0 ? g.player_char_id == char_id : g.party_id == party_id))
			population_party_unjoin(g);
	}
}

/// party_recv_info dropped every shell's slot: the members of a party the player joined take theirs back.
/// True: any did (the caller sends the party window).
static bool population_party_reattach(party_data &p)
{
	bool any = false;
	for (auto &entry : g_pop_parties) {
		PopPartyGroup &g = entry.second;
		if (g.party_id != p.party.party_id)
			continue;
		for (PopPartyMember &member : g.members) {
			map_session_data *sd = pop_party_member_sd(g, member);
			if (sd == nullptr || member.out)
				continue;
			if (party_companion_add(p, *sd, false))
				any = true;
			else { // the char-server's members filled the party: they come first
				member.out = true;
				sd->status.party_id = pop_party_fake_id(g.id);
			}
		}
	}
	return any;
}

/// Ends the joins whose player is gone: offline, off the camp's map, or out of the party they joined. Each way
/// out has its hook; this heals a path one missed, as pop_companions_maintain does for companions.
static void population_party_check_joins()
{
	for (auto &entry : g_pop_parties) {
		PopPartyGroup &g = entry.second;
		if (g.party_id == 0)
			continue;
		const map_session_data *player = map_charid2sd(static_cast<int32>(g.player_char_id));
		if (player == nullptr || player->m != g.m || player->status.party_id != g.party_id || party_search(g.party_id) == nullptr)
			population_party_unjoin(g);
	}
}

// ---- death -----------------------------------------------------------------------------------------------

/// How long a member that just died stays down (ms), 0 for a shell in no party. One killed in sight of a member
/// who can raise it waits for them; any other gets up at the camp after the party's Respawn, as a player who
/// walked back from town.
static t_tick population_party_respawn_delay(const map_session_data &sd)
{
	const PopPartyGroup *g = pop_party_group(&sd);
	if (g == nullptr)
		return 0;
	for (const PopPartyMember &member : g->members) {
		const map_session_data *healer = pop_party_member_sd(*g, member);
		if (healer == nullptr || healer == &sd || pc_isdead(healer) || !pop_party_member_heals(*g, member))
			continue;
		if (distance_bl(healer, &sd) <= AREA_SIZE && pop_party_can_raise(healer))
			return POP_PARTY_RAISE_WAIT_MS;
	}
	return 1000 * static_cast<t_tick>(rnd_value(g->tpl->respawn_min, g->tpl->respawn_max));
}

/// Nobody raised a dead member: it gets up on its cell at the camp, and a roamer rests there before it leaves
/// again. The same shell: a plain shell's respawn goes through pc_setpos, which leaves a shell off the map for
/// the stale sweep to release and the spawner to replace with another. False: not a member, respawn as usual.
static bool population_party_respawn(map_session_data &sd)
{
	const PopPartyGroup *g = pop_party_group(&sd);
	const int index = g != nullptr ? pop_party_member_index(*g, &sd) : -1;
	if (index < 0)
		return false;
	const PopPartyMember &me = g->members[index];
	s_population &pe = sd.pop;
	status_revive(&sd, 100, 100);
	sd.ud.canmove_tick = 0;
	sd.ud.canact_tick = 0;
	pop_party_warp(&sd, me.spot_x, me.spot_y);
	pop_party_take_party_id(sd, *g, me);
	pe.sticky_target_id = 0;
	pe.last_cast_skill_id = 0;
	pe.last_damage_received = 0;
	pe.last_attacked_tick = 0;
	pe.last_skill_used_on_me = 0;
	pe.skill_next_use_tick.clear();
	pe.party_down = false; // it walked back; nobody raised it
	pe.party_next_scan = 0;
	pe.party_idle_since = 0;
	if (me.duty == PopPartyDuty::Roamer)
		pop_party_roamer_arrive(&sd, g->tpl->slots[me.slot], gettick());
	return true;
}

// ---- @populate parties -----------------------------------------------------------------------------------

/// How many songs, dances and ensembles a member is under.
static int pop_party_song_count(const map_session_data *sd)
{
	int count = 0;
	for (const sc_type sc : { SC_WHISTLE, SC_ASSNCROS, SC_POEMBRAGI, SC_APPLEIDUN, SC_HUMMING, SC_FORTUNE, SC_SERVICE4U,
			SC_DRUMBATTLE, SC_NIBELUNGEN, SC_SIEGFRIED })
		if (sd->sc.getSCE(sc) != nullptr)
			count++;
	return count;
}

/// One chat line for each party on the map and for each of its members.
void population_party_report(int fd, int16_t map_id)
{
	static const char *const roam_names[] = { "rest", "hunt", "return" };
	char msg[CHAT_SIZE_MAX];
	for (const auto &entry : g_pop_parties) {
		const PopPartyGroup &g = entry.second;
		if (g.m != map_id)
			continue;
		size_t on_map = 0;
		for (const PopPartyMember &member : g.members)
			if (pop_party_member_sd(g, member) != nullptr)
				on_map++;
		const map_session_data *leader = g.members.empty() ? nullptr : pop_party_member_sd(g, g.members.front());
		safesnprintf(msg, sizeof(msg), "party %u %s camp %d,%d levels %d-%d members %zu/%zu room '%s' leader %d joined %u party_id %d", g.id,
			g.tpl->name.c_str(), g.camp_x, g.camp_y, g.level_lo, g.level_hi, on_map, g.members.size(), g.title.c_str(), leader != nullptr ? leader->id : 0,
			g.player_char_id, g.party_id);
		clif_displaymessage(fd, msg);
		if (const map_session_data *player = pop_party_player(g)) {
			safesnprintf(msg, sizeof(msg), "player %u name %s lv %d at %d,%d camp_dist %d hp %d sp %d dead %d bless %d agi %d",
				g.id, player->status.name, player->status.base_level, player->x, player->y,
				distance_xy(player->x, player->y, g.camp_x, g.camp_y),
				pop_party_pct(player->battle_status.hp, player->battle_status.max_hp),
				pop_party_pct(player->battle_status.sp, player->battle_status.max_sp), pc_isdead(player) ? 1 : 0,
				player->sc.getSCE(SC_BLESSING) != nullptr ? 1 : 0, player->sc.getSCE(SC_INCREASEAGI) != nullptr ? 1 : 0);
			clif_displaymessage(fd, msg);
		}
		for (const PopPartyMember &member : g.members) {
			const map_session_data *sd = pop_party_member_sd(g, member);
			if (sd == nullptr) {
				safesnprintf(msg, sizeof(msg), "member %u %s job %s absent", g.id, population_party_duty_name(member.duty), job_name(member.job));
				clif_displaymessage(fd, msg);
				continue;
			}
			const s_population &pe = sd->pop;
			const char *state = pc_isdead(sd) ? "dead"
				: member.duty == PopPartyDuty::Roamer ? roam_names[std::min<uint8>(pe.party_state, 2)]
				: pe.target_id != 0 ? "fight" : "camp";
			safesnprintf(msg, sizeof(msg),
				"member %u %s job %s lv %d name %s at %d,%d spot %d,%d camp_dist %d state %s hp %d sp %d sit %d room %d "
				"bless %d agi %d songs %d trips %u raised %u helps %u idle %d skills %zu/%zu set %s in_party %d",
				g.id, population_party_duty_name(member.duty), job_name(sd->status.class_), sd->status.base_level, sd->status.name,
				sd->x, sd->y, member.spot_x, member.spot_y, distance_xy(sd->x, sd->y, g.camp_x, g.camp_y), state,
				pop_party_pct(sd->battle_status.hp, sd->battle_status.max_hp), pop_party_pct(sd->battle_status.sp, sd->battle_status.max_sp),
				pc_issit(sd) ? 1 : 0, sd->chatID != 0 ? 1 : 0,
				sd->sc.getSCE(SC_BLESSING) != nullptr ? 1 : 0, sd->sc.getSCE(SC_INCREASEAGI) != nullptr ? 1 : 0,
				pop_party_song_count(sd), pe.party_trips, pe.party_revived, pe.party_helps,
				pe.party_idle_since != 0 ? static_cast<int>(DIFF_TICK(gettick(), pe.party_idle_since) / 1000) : -1,
				pe.attack_skills.size(), pe.buff_skills.size(),
				pe.party_skill_set.empty() ? "-" : pe.party_skill_set.c_str(), pop_party_in_real_party(g, *sd) ? 1 : 0);
			clif_displaymessage(fd, msg);
		}
	}
}

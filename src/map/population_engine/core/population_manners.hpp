// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: how a shell plays like a player beside other players. Rules with no map-server state in them, so
// src/test/population_manners_test.cpp checks them on their own:
// - it answers a player after a while, as a player reads and types (reply_delay_ms);
// - it does not cast the same skill again and again (repeat_factor) nor the next one at once (skill_gap_ms);
// - it leaves alone a monster someone else is fighting (mob_open, kill_steal_roll);
// - a party's roamer hunts the map, a careful one nearer its camp (roam_leash);
// - a shell party's chat room takes a player into a party named for its leader (camp_party_name).
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace pop_manners {

/// Before a shell answers in chat (ms): its reaction (`reaction_ms`, give or take a third by `roll` 0-99), a
/// moment to read, and the time to type `chars` letters.
inline int reply_delay_ms(int reaction_ms, int chars, int roll)
{
	static constexpr int READ_MS = 700, PER_CHAR_MS = 55;
	const int reaction = std::max(0, reaction_ms) * (67 + std::clamp(roll, 0, 99) * 66 / 99) / 100;
	return reaction + READ_MS + std::max(0, chars) * PER_CHAR_MS;
}

/// The scored picker's weight on a skill cast `repeats` times in a row just before: a player mixes theirs.
inline double repeat_factor(int repeats)
{
	static constexpr double FACTORS[] = { 1.0, 0.85, 0.6, 0.35 };
	return FACTORS[std::clamp(repeats, 0, 3)];
}

/// After a skill, before it picks the next (ms): its temperament's [gap_min, gap_max] by `roll` 0-99, never
/// less than its attack motion; a player does not press the next key while the last swing plays.
inline int skill_gap_ms(int gap_min, int gap_max, int roll, int amotion)
{
	const int lo = std::max(0, std::min(gap_min, gap_max)), hi = std::max(0, std::max(gap_min, gap_max));
	return std::max(amotion, lo + (hi - lo) * std::clamp(roll, 0, 99) / 99);
}

/// Who a monster is with, seen from a shell looking for one to fight.
struct MobClaim {
	bool on_us = false;      ///< it is after the shell, or after someone in the shell's party
	bool hit_by_us = false;  ///< the shell hit it already: the fight is the shell's as much as anyone's
	bool foreign = false;    ///< someone else fights it: it is after them, they hit it, or a shell took it
};

/// Whether the shell may fight it. `kill_steal`: the shell is one that takes others' monsters (kill_steal_roll).
inline bool mob_open(const MobClaim &claim, bool kill_steal)
{
	return claim.on_us || claim.hit_by_us || !claim.foreign || kill_steal;
}

/// Whether a shell that steals `pct` % of the time steals this monster. The same pair always gives the same
/// answer, so it does not reroll each tick until it steals.
inline bool kill_steal_roll(uint32_t shell_id, uint32_t mob_id, int pct)
{
	if (pct <= 0)
		return false;
	uint32_t h = shell_id * 2654435761u ^ mob_id * 2246822519u;
	h ^= h >> 15;
	h *= 2654435761u;
	h ^= h >> 13;
	return static_cast<int>(h % 100) < pct;
}

/// Shells on their own carry their map's party id (0x70000000 | map); a party's members and a player's companions
/// carry a party of their own. Two shells in the map's party are not together.
inline bool map_wide_party(int32_t party_id)
{
	return (static_cast<uint32_t>(party_id) & 0xFFFF0000u) == 0x70000000u;
}

/// How far from its camp a roamer hunts (cells). `slot_roam` <= 0 is the whole map; `leash_pct` (its
/// temperament's, 1-99) holds it to that share of the map's longer side; 0 or 100 does not.
inline int roam_leash(int slot_roam, int map_xs, int map_ys, int leash_pct)
{
	const int whole = std::max(map_xs, map_ys);
	int leash = slot_roam > 0 ? slot_roam : whole;
	if (leash_pct > 0 && leash_pct < 100)
		leash = std::min(leash, std::max(10, whole * leash_pct / 100));
	return leash;
}

/// A party named for `leader`: "Ragnar Party". Party names take letters, digits and spaces (char_name_letters),
/// so the rest goes; empty when nothing is left.
inline std::string camp_party_name(const std::string &leader)
{
	static constexpr size_t MAX_NAME = 17; // NAME_LENGTH 24, less " Party" and the terminator
	std::string name;
	for (const char c : leader) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
		if (ok && !(c == ' ' && (name.empty() || name.back() == ' ')))
			name += c;
	}
	if (name.size() > MAX_NAME)
		name.resize(MAX_NAME);
	while (!name.empty() && name.back() == ' ')
		name.pop_back();
	return name.empty() ? name : name + " Party";
}

} // namespace pop_manners

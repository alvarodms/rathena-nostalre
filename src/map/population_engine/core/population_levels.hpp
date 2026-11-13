// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: the levels shells have, as rules with no map-server state in them, so
// src/test/population_levels_test.cpp checks them on their own:
// - the base levels a class can have (class_levels): a second class from 55, as players change job late;
// - the levels a map's shell parties are (party_default): below the map's monsters, since Renewal gives
//   more EXP for monsters above you;
// - a party's members within a few levels of each other (party_window), so they share EXP.
#pragma once

#include <algorithm>
#include <cstdint>

namespace pop_levels {

struct Range {
	int32_t lo = 0, hi = 0;
};

/// Where a class stands: a Novice, a first class (with or without a second to move on to), a second class,
/// a transcendent second class or a third class.
enum class Tier : uint8_t { Novice, First, FirstOnly, Second, TransSecond, Third };

/// The base levels a class can have, before its job's own maximum. A first class with a second to move on to
/// stops at 70; Gunslingers and Ninjas have none and go on.
inline Range class_levels(Tier tier)
{
	switch (tier) {
	case Tier::Novice:      return { 1, 15 };
	case Tier::First:       return { 10, 70 };
	case Tier::FirstOnly:   return { 10, 999 };
	case Tier::Second:      return { 55, 999 };
	case Tier::TransSecond: return { 60, 999 };
	case Tier::Third:       return { 99, 999 };
	}
	return { 1, 999 };
}

/// The levels a map's shell parties are when its entry names none: from 8 below its monsters to 7 above,
/// since Renewal gives more EXP for a monster above you, up to 15 levels.
inline Range party_default(int32_t map_level)
{
	static constexpr int32_t BELOW = 8, ABOVE = 7;
	return { std::max(1, map_level - BELOW), std::max(1, map_level + ABOVE) };
}

/// The levels of one party's members: within 7 of a centre in `range` (`roll` 0..hi-lo picks it), so the
/// party stays inside party_share_level's 15 and shares EXP.
inline Range party_window(Range range, int32_t roll)
{
	static constexpr int32_t HALF = 7;
	const int32_t lo = std::min(range.lo, range.hi), hi = std::max(range.lo, range.hi);
	const int32_t centre = lo + std::clamp(roll, 0, hi - lo);
	return { std::max(lo, centre - HALF), std::min(hi, centre + HALF) };
}

/// A member's level in its party's window (`roll` 0..hi-lo), then held to what its class can be.
inline int32_t member_level(Range window, int32_t roll, Range klass)
{
	const int32_t level = window.lo + std::clamp(roll, 0, std::max(0, window.hi - window.lo));
	return std::clamp(level, klass.lo, std::max(klass.lo, klass.hi));
}

} // namespace pop_levels

// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: shell parties at run time (db/population_party.yml, config/population_party_db.hpp).
//
// What population_engine_combat.cpp needs from population_party.cpp, which comes after it in the unity unit.
#pragma once

#include <common/cbasetypes.hpp>
#include <common/timer.hpp>

class map_session_data;

/// A party member whose duty keeps it in place: it takes no targets of its own and does not wander.
/// False for a roamer out hunting, and for shells in no party.
bool population_party_holds(const map_session_data *sd);

/// Whether `shell` keeps its heals and buffs from `ally`: a party member's are for its party.
bool population_party_ignores_ally(const map_session_data *shell, const map_session_data *ally);

/// A member in the party of a player who joined it: it holds a slot in their real party.
bool population_party_joined(const map_session_data *sd);

/// One tick of a party member's duty. True: the duty ran the tick. False: it plays on as a shell on its own
/// (a roamer out hunting).
bool population_party_member_tick(map_session_data *sd, t_tick now, bool do_skills);

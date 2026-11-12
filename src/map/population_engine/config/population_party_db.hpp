// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: Population shell parties, db/population_party.yml.
//
// A party is a tactic and a list of slots. The tactic is what its members share (for `camp`, a spot
// on the map and a leader standing on it); a slot is a place in the party with a duty:
// - anchor:    the leader, on the camp spot, with a recruiting chat room over their head or not;
// - support:   stays by the anchor and heals, buffs and raises the party's members;
// - roamer:    hunts around the camp and comes back for heals and buffs, then leaves again;
// - performer: a Bard or Dancer at the camp, next to its partner, keeping songs up;
// - battery:   a Professor at the camp, giving SP with Soul Exchange.
// A duty says where a member is and when it leaves a fight; how it fights stays the skill picker's.
// Map entries say which parties a map holds, where they camp and what they are more or less likely
// to have there. Other party behaviours are other tactics and duties.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common/database.hpp>

enum class PopPartyDuty : uint8_t { None = 0, Anchor, Support, Roamer, Performer, Battery, Count };
enum class PopPartyTactic : uint8_t { Camp = 0 };

const char* population_party_duty_name(PopPartyDuty duty);

struct PopPartySlot {
	PopPartyDuty duty = PopPartyDuty::Roamer;
	std::vector<std::pair<uint16_t, uint16_t>> jobs; ///< job id, weight
	uint16_t partner_job = 0;       ///< performer: the other half of the duo, on the next cell (0: alone)
	std::string skill_set;          ///< population_skill_db.yml Name: (empty: its build's or job's)
	std::string partner_skill_set;
	std::string build;              ///< population_personality.yml Build: the member takes (empty: rolled)
	int32_t count_min = 1, count_max = 1;
	int32_t chance = 100;           ///< % of parties that have the slot
	// roamer
	int32_t roam = 40;              ///< cells from the camp it hunts within
	int32_t return_hp = 40;         ///< heads back below this % HP
	int32_t return_sp = 15;         ///< or this % SP
	bool return_unbuffed = true;    ///< or when the buffs it left with have run out
	int32_t trip_min = 0, trip_max = 0; ///< or after this many seconds out (0: no limit)
	int32_t leave_hp = 90;          ///< leaves again from this % HP
	int32_t leave_sp = 60;          ///< and this % SP
	int32_t wait_min = 10, wait_max = 30; ///< s it waits for them at most
};

struct PopPartyTemplate {
	std::string name;
	PopPartyTactic tactic = PopPartyTactic::Camp;
	int32_t camp_radius = 4;        ///< cells from the camp spot its members stand within
	int32_t camp_defend = 10;       ///< they fight what attacks a member this close to the camp
	bool sit = true;                ///< idle members who cast nothing sit down
	std::vector<std::string> recruit; ///< chat room titles
	int32_t recruit_chance = 0;     ///< % of parties whose leader opens one
	int32_t respawn_min = 15, respawn_max = 30; ///< s a member killed out of the support's sight stays down
	std::vector<PopPartySlot> slots;
};

struct PopPartyMap {
	std::string map;
	std::vector<std::pair<std::string, int32_t>> parties; ///< template name, how many
	std::vector<std::pair<int16_t, int16_t>> camps;       ///< camp spots, in order; the rest are picked
	std::array<int16_t, static_cast<size_t>(PopPartyDuty::Count)> chance; ///< % per duty, -1: the slot's

	PopPartyMap() { chance.fill(-1); }
};

class PopulationPartyDatabase : public YamlDatabase {
	std::unordered_map<std::string, std::shared_ptr<const PopPartyTemplate>> templates_;
	std::vector<std::shared_ptr<const PopPartyMap>> maps_;

	bool readRange(const ryml::NodeRef& node, const char* key, int32_t& lo, int32_t& hi);
	bool readJobs(const ryml::NodeRef& node, const std::string& owner, std::vector<std::pair<uint16_t, uint16_t>>& out);
	bool readSlot(const ryml::NodeRef& node, const std::string& party, PopPartySlot& slot);

public:
	PopulationPartyDatabase();
	void clear() override;
	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;
	void loadingFinished() override;

	std::shared_ptr<const PopPartyTemplate> find(const std::string& name) const;
	const std::vector<std::shared_ptr<const PopPartyMap>>& maps() const { return maps_; }
	size_t template_count() const { return templates_.size(); }
};

PopulationPartyDatabase& population_party_db();

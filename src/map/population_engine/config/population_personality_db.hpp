// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: Population shell personalities, db/population_personality.yml.
//
// A shell's personality is a build and a temperament, rolled apart at spawn:
// - a build is what a player made of their class: which jobs it is for (with weights), its skill set
//   (a Name: in population_skill_db.yml), gear set (population_gear_sets.yml), stats, names and chat;
// - a temperament is how they play, whatever their class: reaction time, precision, pauses.
// Both weigh the scored skill picker's kinds (Prefers). A build lists the temperaments it can take
// (all of them when it lists none). A job with no build keeps its population_engine.yml entry and
// still gets a temperament.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common/database.hpp>

#include "population_skill_db.hpp" // PopSkillKind

/// Percent per PopSkillKind; 100 = no preference.
using PopKindPrefs = std::array<uint16_t, static_cast<size_t>(PopSkillKind::Count)>;

struct PopTemperament {
	std::string name;
	uint16_t weight = 1;                          ///< against the other temperaments a build can take
	int32_t reaction_min = 0, reaction_max = 0;   ///< ms before it acts on something new
	int32_t precision_min = 100, precision_max = 100; ///< % of choices that are its best one
	int32_t pause_every_min = 0, pause_every_max = 0; ///< s between idle pauses (0: none)
	int32_t pause_for_min = 0, pause_for_max = 0;     ///< s each pause lasts
	PopKindPrefs prefers{};
};

struct PopBuild {
	std::string name;
	std::vector<std::pair<uint16_t, uint16_t>> jobs; ///< job id, weight
	std::string skill_set;   ///< population_skill_db.yml Name: (empty: the job's default)
	std::string gear_set;    ///< population_gear_sets.yml GearSetName: (empty: the job's)
	std::string names;       ///< population_names.yml profile (empty: the job's)
	std::string chat;        ///< population_chat.yml profile (empty: the job's)
	/// Str, Agi, Vit, Int, Dex, Luk as min/max pairs before the level fits them; -1 keeps the job's.
	std::array<int16_t, 12> stats;
	int16_t melee_weight = -1; ///< % score of a basic attack; -1: by weapon
	PopKindPrefs prefers{};
	std::vector<std::pair<std::string, uint16_t>> temperaments; ///< name, weight (empty: all)

	PopBuild() { stats.fill(-1); }
};

class PopulationPersonalityDatabase : public YamlDatabase {
	std::vector<std::shared_ptr<const PopTemperament>> temperaments_;
	std::unordered_map<std::string, std::shared_ptr<const PopTemperament>> temperament_by_name_;
	std::vector<std::shared_ptr<const PopBuild>> builds_;
	std::unordered_map<uint16_t, std::vector<std::pair<std::shared_ptr<const PopBuild>, uint16_t>>> builds_by_job_;

	bool readRange(const ryml::NodeRef& node, const char* key, int32_t& lo, int32_t& hi);
	bool readPrefers(const ryml::NodeRef& node, PopKindPrefs& out);
	bool readJobId(const std::string& name, uint16_t& job_id);

public:
	PopulationPersonalityDatabase();
	void clear() override;
	const std::string getDefaultLocation() override;
	uint64 parseBodyNode(const ryml::NodeRef& node) override;
	void loadingFinished() override;

	/// A build for this job (nullptr when it has none) and a temperament (nullptr when the file has none).
	void roll(uint16_t job_id, std::shared_ptr<const PopBuild>& build, std::shared_ptr<const PopTemperament>& temperament) const;
	size_t build_count() const { return builds_.size(); }
	size_t temperament_count() const { return temperaments_.size(); }
};

PopulationPersonalityDatabase& population_personality_db();

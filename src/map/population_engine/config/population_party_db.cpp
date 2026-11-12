// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: YAML parser for db/population_party.yml (see population_party_db.hpp).
// Built in population_engine_factory.cpp's unity unit after population_personality_db.cpp, whose
// node helpers and population_job_id_by_name it uses.

#include "population_party_db.hpp"

#include <algorithm>
#include <cctype>

#include <common/showmsg.hpp>

namespace {

PopulationPartyDatabase g_population_party_db;

const std::unordered_map<std::string, PopPartyDuty> kPartyDuties = {
	{ "anchor", PopPartyDuty::Anchor }, { "support", PopPartyDuty::Support }, { "roamer", PopPartyDuty::Roamer },
	{ "performer", PopPartyDuty::Performer }, { "battery", PopPartyDuty::Battery },
};

std::string pop_lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return s;
}

} // namespace

const char* population_party_duty_name(PopPartyDuty duty)
{
	for (const auto& entry : kPartyDuties)
		if (entry.second == duty)
			return entry.first.c_str();
	return "none";
}

PopulationPartyDatabase& population_party_db()
{
	return g_population_party_db;
}

PopulationPartyDatabase::PopulationPartyDatabase()
	: YamlDatabase("POPULATION_PARTY_DB", 1)
{
}

void PopulationPartyDatabase::clear()
{
	this->templates_.clear();
	this->maps_.clear();
}

const std::string PopulationPartyDatabase::getDefaultLocation()
{
	return population_config_join_db("population_party.yml");
}

std::shared_ptr<const PopPartyTemplate> PopulationPartyDatabase::find(const std::string& name) const
{
	const auto it = this->templates_.find(name);
	return it != this->templates_.end() ? it->second : nullptr;
}

/// `Key: 30` or `Key: [15, 30]`.
bool PopulationPartyDatabase::readRange(const ryml::NodeRef& node, const char* key, int32_t& lo, int32_t& hi)
{
	if (!this->nodeExists(node, key))
		return false;
	const ryml::NodeRef& n = node[c4::to_csubstr(key)];
	int32_t a = 0, b = 0;
	if (n.is_seq()) {
		if (n.num_children() != 2 || !pop_node_int(n[0], a) || !pop_node_int(n[1], b)) {
			this->invalidWarning(n, "%s needs two numbers, [min, max].\n", key);
			return false;
		}
	} else if (pop_node_int(n, a)) {
		b = a;
	} else {
		this->invalidWarning(n, "%s needs a number or [min, max].\n", key);
		return false;
	}
	lo = std::max(0, std::min(a, b));
	hi = std::max(0, std::max(a, b));
	return true;
}

/// `Jobs: { Priest: 3, Knight: 1 }`.
bool PopulationPartyDatabase::readJobs(const ryml::NodeRef& node, const std::string& owner, std::vector<std::pair<uint16_t, uint16_t>>& out)
{
	if (!this->nodeExists(node, "Jobs") || !node[c4::to_csubstr("Jobs")].is_map()) {
		this->invalidWarning(node, "Party '%s': a slot needs Jobs: { JobName: weight }.\n", owner.c_str());
		return false;
	}
	for (const ryml::NodeRef& c : node[c4::to_csubstr("Jobs")].children()) {
		const std::string job_name = pop_node_key(c);
		uint16_t job_id = 0;
		int32_t weight = 1;
		if (!population_job_id_by_name(job_name, job_id)) {
			this->invalidWarning(c, "Party '%s': unknown job '%s' -- skipped.\n", owner.c_str(), job_name.c_str());
			continue;
		}
		if (c.has_val() && !pop_node_int(c, weight))
			weight = 1;
		if (weight > 0)
			out.emplace_back(job_id, static_cast<uint16_t>(std::min(weight, 10000)));
	}
	if (out.empty()) {
		this->invalidWarning(node, "Party '%s': a slot has no known job -- skipped.\n", owner.c_str());
		return false;
	}
	return true;
}

bool PopulationPartyDatabase::readSlot(const ryml::NodeRef& node, const std::string& party, PopPartySlot& slot)
{
	std::string duty;
	if (!this->nodeExists(node, "Duty") || !this->asString(node, "Duty", duty)) {
		this->invalidWarning(node, "Party '%s': a slot needs Duty:.\n", party.c_str());
		return false;
	}
	const auto dit = kPartyDuties.find(pop_lower(duty));
	if (dit == kPartyDuties.end()) {
		this->invalidWarning(node, "Party '%s': unknown duty '%s' (anchor, support, roamer, performer, battery).\n",
			party.c_str(), duty.c_str());
		return false;
	}
	slot.duty = dit->second;
	if (!this->readJobs(node, party, slot.jobs))
		return false;

	if (this->nodeExists(node, "Partner")) {
		std::string partner;
		if (!this->asString(node, "Partner", partner) || !population_job_id_by_name(partner, slot.partner_job)) {
			this->invalidWarning(node, "Party '%s': unknown Partner job '%s' -- the slot stands alone.\n", party.c_str(), partner.c_str());
			slot.partner_job = 0;
		}
	}
	for (const auto& [key, out] : { std::pair<const char*, std::string*>{ "SkillSet", &slot.skill_set },
			{ "PartnerSkillSet", &slot.partner_skill_set } }) {
		if (!this->nodeExists(node, key))
			continue;
		this->asString(node, key, *out);
		if (!out->empty() && population_skill_db().find_set(*out) == nullptr) {
			this->invalidWarning(node, "Party '%s': no skill set named '%s' in population_skill_db.yml -- the job's own skills are used.\n",
				party.c_str(), out->c_str());
			out->clear();
		}
	}

	if (this->nodeExists(node, "Build"))
		this->asString(node, "Build", slot.build);

	if (slot.duty == PopPartyDuty::Support)
		slot.count_min = 0; // a healing anchor does without; pop_party_roll_plan raises it otherwise
	this->readRange(node, "Count", slot.count_min, slot.count_max);
	slot.count_max = std::min<int32_t>(slot.count_max, MAX_PARTY);
	slot.count_min = std::min(slot.count_min, slot.count_max);
	int32_t chance = 100;
	if (this->nodeExists(node, "Chance") && this->asInt32(node, "Chance", chance))
		slot.chance = std::clamp(chance, 0, 100);

	int32_t v = 0;
	if (this->nodeExists(node, "Roam")) {
		const ryml::NodeRef& r = node[c4::to_csubstr("Roam")];
		std::string word;
		if (r.has_val())
			word.assign(r.val().str, r.val().len);
		std::transform(word.begin(), word.end(), word.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (word == "map")
			slot.roam = 0; // the whole map
		else if (pop_node_int(r, v))
			slot.roam = std::clamp(v, 8, 1000);
		else
			this->invalidWarning(r, "Roam needs a number of cells, or map.\n");
	}
	if (this->nodeExists(node, "Return")) {
		const ryml::NodeRef& r = node[c4::to_csubstr("Return")];
		if (this->nodeExists(r, "HpBelow") && this->asInt32(r, "HpBelow", v))
			slot.return_hp = std::clamp(v, 0, 100);
		if (this->nodeExists(r, "SpBelow") && this->asInt32(r, "SpBelow", v))
			slot.return_sp = std::clamp(v, 0, 100);
		if (this->nodeExists(r, "Unbuffed"))
			this->asBool(r, "Unbuffed", slot.return_unbuffed);
		this->readRange(r, "After", slot.trip_min, slot.trip_max);
	}
	if (this->nodeExists(node, "Leave")) {
		const ryml::NodeRef& l = node[c4::to_csubstr("Leave")];
		if (this->nodeExists(l, "HpAbove") && this->asInt32(l, "HpAbove", v))
			slot.leave_hp = std::clamp(v, 0, 100);
		if (this->nodeExists(l, "SpAbove") && this->asInt32(l, "SpAbove", v))
			slot.leave_sp = std::clamp(v, 0, 100);
		this->readRange(l, "Wait", slot.wait_min, slot.wait_max);
	}
	return true;
}

uint64 PopulationPartyDatabase::parseBodyNode(const ryml::NodeRef& node)
{
	std::string name;

	if (this->nodeExists(node, "Map")) {
		if (!this->asString(node, "Map", name))
			return 0;
		auto m = std::make_shared<PopPartyMap>();
		m->map = name;
		if (!this->nodeExists(node, "Parties") || !node[c4::to_csubstr("Parties")].is_map()) {
			this->invalidWarning(node, "Map '%s' needs Parties: { PartyName: count }.\n", name.c_str());
			return 0;
		}
		for (const ryml::NodeRef& c : node[c4::to_csubstr("Parties")].children()) {
			int32_t count = 1;
			if (c.has_val() && !pop_node_int(c, count))
				count = 1;
			if (count > 0)
				m->parties.emplace_back(pop_node_key(c), std::min(count, 20));
		}
		if (this->nodeExists(node, "Camps")) {
			for (const ryml::NodeRef& c : node[c4::to_csubstr("Camps")].children()) {
				int32_t x = 0, y = 0;
				if (!c.is_seq() || c.num_children() != 2 || !pop_node_int(c[0], x) || !pop_node_int(c[1], y) || x <= 0 || y <= 0) {
					this->invalidWarning(c, "Map '%s': a camp is [x, y].\n", name.c_str());
					continue;
				}
				m->camps.emplace_back(static_cast<int16_t>(x), static_cast<int16_t>(y));
			}
		}
		if (this->nodeExists(node, "Chance")) {
			for (const ryml::NodeRef& c : node[c4::to_csubstr("Chance")].children()) {
				const std::string duty = pop_lower(pop_node_key(c));
				const auto dit = kPartyDuties.find(duty);
				int32_t pct = 0;
				if (dit == kPartyDuties.end() || !pop_node_int(c, pct)) {
					this->invalidWarning(c, "Map '%s': Chance needs { duty: percent }, got '%s'.\n", name.c_str(), duty.c_str());
					continue;
				}
				m->chance[static_cast<size_t>(dit->second)] = static_cast<int16_t>(std::clamp(pct, 0, 100));
			}
		}
		const auto existing = std::find_if(this->maps_.begin(), this->maps_.end(),
			[&name](const std::shared_ptr<const PopPartyMap>& o) { return o->map == name; });
		if (existing != this->maps_.end())
			*existing = m;
		else
			this->maps_.push_back(m);
		return 1;
	}

	if (!this->nodeExists(node, "Party") || !this->asString(node, "Party", name)) {
		this->invalidWarning(node, "Each entry needs Party: or Map:.\n");
		return 0;
	}
	auto t = std::make_shared<PopPartyTemplate>();
	t->name = name;

	if (this->nodeExists(node, "Tactic")) {
		std::string tactic;
		this->asString(node, "Tactic", tactic);
		if (pop_lower(tactic) != "camp") {
			this->invalidWarning(node, "Party '%s': unknown Tactic '%s' (camp) -- skipped.\n", name.c_str(), tactic.c_str());
			return 0;
		}
	}
	int32_t v = 0;
	if (this->nodeExists(node, "Camp")) {
		const ryml::NodeRef& c = node[c4::to_csubstr("Camp")];
		if (this->nodeExists(c, "Radius") && this->asInt32(c, "Radius", v))
			t->camp_radius = std::clamp(v, 2, 9);
		if (this->nodeExists(c, "Defend") && this->asInt32(c, "Defend", v))
			t->camp_defend = std::clamp(v, 3, 20);
		if (this->nodeExists(c, "Sit"))
			this->asBool(c, "Sit", t->sit);
	}
	if (this->nodeExists(node, "Recruit")) {
		for (const ryml::NodeRef& c : node[c4::to_csubstr("Recruit")].children()) {
			const std::string title = pop_node_string(c);
			if (!title.empty())
				t->recruit.push_back(title);
		}
	}
	if (this->nodeExists(node, "RecruitChance") && this->asInt32(node, "RecruitChance", v))
		t->recruit_chance = std::clamp(v, 0, 100);
	this->readRange(node, "Respawn", t->respawn_min, t->respawn_max);

	if (!this->nodeExists(node, "Slots") || !node[c4::to_csubstr("Slots")].is_seq()) {
		this->invalidWarning(node, "Party '%s' needs Slots:.\n", name.c_str());
		return 0;
	}
	for (const ryml::NodeRef& c : node[c4::to_csubstr("Slots")].children()) {
		PopPartySlot slot;
		if (this->readSlot(c, name, slot))
			t->slots.push_back(std::move(slot));
	}
	if (std::none_of(t->slots.begin(), t->slots.end(), [](const PopPartySlot& s) { return s.duty == PopPartyDuty::Anchor; })) {
		this->invalidWarning(node, "Party '%s' has no anchor slot -- skipped.\n", name.c_str());
		return 0;
	}
	this->templates_[name] = t;
	return 1;
}

void PopulationPartyDatabase::loadingFinished()
{
	for (const auto& t : this->templates_)
		for (const PopPartySlot& slot : t.second->slots)
			if (!slot.build.empty() && std::none_of(slot.jobs.begin(), slot.jobs.end(), [&slot](const std::pair<uint16_t, uint16_t>& job) {
					return population_personality_db().find_build(slot.build, job.first) != nullptr; }))
				ShowWarning("population_party: party '%s' names build '%s', which population_personality.yml has for none of the slot's jobs -- builds are rolled.\n",
					t.first.c_str(), slot.build.c_str());
	for (const auto& m : this->maps_)
		for (const auto& p : m->parties)
			if (this->templates_.find(p.first) == this->templates_.end())
				ShowWarning("population_party: map '%s' names unknown party '%s' -- left out.\n", m->map.c_str(), p.first.c_str());
}

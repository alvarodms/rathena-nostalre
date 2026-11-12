// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: YAML parser for db/population_personality.yml (see population_personality_db.hpp).
// Built in population_engine_factory.cpp's unity unit after population_config.cpp, whose
// population_config_join_db and kJobNameMap it uses.

#include "population_personality_db.hpp"

#include <algorithm>
#include <cstdlib>

#include <common/random.hpp>
#include <common/showmsg.hpp>

#include "../../script.hpp" // script_get_constant

namespace {

PopulationPersonalityDatabase g_population_personality_db;

const std::unordered_map<std::string, PopSkillKind> kPrefersKeys = {
	{ "damage", PopSkillKind::Damage }, { "aoe",    PopSkillKind::Aoe    }, { "heal",   PopSkillKind::Heal   },
	{ "buff",   PopSkillKind::Buff   }, { "debuff", PopSkillKind::Debuff }, { "defend", PopSkillKind::Defend },
	{ "escape", PopSkillKind::Escape }, { "melee",  PopSkillKind::Melee  },
};

std::string pop_node_string(const ryml::NodeRef& node)
{
	if (!node.has_val())
		return std::string();
	const c4::csubstr v = node.val();
	return std::string(v.data(), v.size());
}

std::string pop_node_key(const ryml::NodeRef& node)
{
	const c4::csubstr k = node.key();
	return std::string(k.data(), k.size());
}

bool pop_node_int(const ryml::NodeRef& node, int32_t& out)
{
	const std::string s = pop_node_string(node);
	if (s.empty())
		return false;
	char* end = nullptr;
	const long v = std::strtol(s.c_str(), &end, 10);
	if (end == s.c_str() || *end != '\0')
		return false;
	out = static_cast<int32_t>(v);
	return true;
}

/// Pick one entry by weight; the vector holds (thing, weight) pairs.
template <typename T>
const T* pop_pick_weighted(const std::vector<std::pair<T, uint16_t>>& items)
{
	uint32_t total = 0;
	for (const auto& it : items)
		total += it.second;
	if (total == 0)
		return nullptr;
	uint32_t roll = static_cast<uint32_t>(rnd() % total);
	for (const auto& it : items) {
		if (roll < it.second)
			return &it.first;
		roll -= it.second;
	}
	return nullptr;
}

/// A job by its population_engine.yml name (Knight, HighPriest, "Lord Knight"), a Job_ constant or a number.
/// Also used by population_party_db.cpp, later in the unity unit.
bool population_job_id_by_name(const std::string& name, uint16_t& job_id)
{
	std::string nosp = name;
	nosp.erase(std::remove(nosp.begin(), nosp.end(), ' '), nosp.end());
	const auto it = kJobNameMap.find(nosp);
	if (it != kJobNameMap.end()) {
		job_id = it->second;
		return true;
	}
	int64 constant = 0;
	if (script_get_constant(nosp.c_str(), &constant) || script_get_constant(("Job_" + nosp).c_str(), &constant)) {
		job_id = static_cast<uint16_t>(constant);
		return job_db.exists(job_id);
	}
	char* end = nullptr;
	const long v = std::strtol(nosp.c_str(), &end, 10);
	if (end != nosp.c_str() && *end == '\0' && job_db.exists(static_cast<uint16_t>(v))) {
		job_id = static_cast<uint16_t>(v);
		return true;
	}
	return false;
}

} // namespace

PopulationPersonalityDatabase& population_personality_db()
{
	return g_population_personality_db;
}

PopulationPersonalityDatabase::PopulationPersonalityDatabase()
	: YamlDatabase("POPULATION_PERSONALITY_DB", 1)
{
}

void PopulationPersonalityDatabase::clear()
{
	this->temperaments_.clear();
	this->temperament_by_name_.clear();
	this->builds_.clear();
	this->builds_by_job_.clear();
}

const std::string PopulationPersonalityDatabase::getDefaultLocation()
{
	return population_config_join_db("population_personality.yml");
}

/// `Key: 300` or `Key: [300, 900]`.
bool PopulationPersonalityDatabase::readRange(const ryml::NodeRef& node, const char* key, int32_t& lo, int32_t& hi)
{
	if (!this->nodeExists(node, key))
		return false;
	const ryml::NodeRef& n = node[c4::to_csubstr(key)];
	if (n.is_seq()) {
		if (n.num_children() != 2 || !pop_node_int(n[0], lo) || !pop_node_int(n[1], hi)) {
			this->invalidWarning(n, "%s needs two numbers, [min, max].\n", key);
			return false;
		}
	} else if (pop_node_int(n, lo)) {
		hi = lo;
	} else {
		this->invalidWarning(n, "%s needs a number or [min, max].\n", key);
		return false;
	}
	if (hi < lo)
		std::swap(lo, hi);
	return true;
}

/// `Prefers: { aoe: 150, melee: 20 }`, in percent; kinds left out stay as they were.
bool PopulationPersonalityDatabase::readPrefers(const ryml::NodeRef& node, PopKindPrefs& out)
{
	if (!this->nodeExists(node, "Prefers"))
		return false;
	const ryml::NodeRef& n = node[c4::to_csubstr("Prefers")];
	if (!n.is_map()) {
		this->invalidWarning(n, "Prefers needs a map of kind: percent.\n");
		return false;
	}
	for (const ryml::NodeRef& c : n.children()) {
		std::string key = pop_node_key(c);
		std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		const auto it = kPrefersKeys.find(key);
		int32_t pct = 0;
		if (it == kPrefersKeys.end() || !pop_node_int(c, pct) || pct < 0) {
			this->invalidWarning(c, "Prefers: unknown kind or bad percent '%s' (damage, aoe, heal, buff, debuff, defend, escape, melee).\n", key.c_str());
			continue;
		}
		out[static_cast<size_t>(it->second)] = static_cast<uint16_t>(std::min(pct, 1000));
	}
	return true;
}

bool PopulationPersonalityDatabase::readJobId(const std::string& name, uint16_t& job_id)
{
	return population_job_id_by_name(name, job_id);
}

uint64 PopulationPersonalityDatabase::parseBodyNode(const ryml::NodeRef& node)
{
	std::string name;
	if (this->nodeExists(node, "Temperament") && this->asString(node, "Temperament", name)) {
		auto t = std::make_shared<PopTemperament>();
		t->name = name;
		t->prefers.fill(100);
		uint16_t weight = 1;
		if (this->nodeExists(node, "Weight") && this->asUInt16(node, "Weight", weight))
			t->weight = weight;
		this->readRange(node, "ReactionMs", t->reaction_min, t->reaction_max);
		if (this->readRange(node, "Precision", t->precision_min, t->precision_max)) {
			t->precision_min = std::clamp(t->precision_min, 0, 100);
			t->precision_max = std::clamp(t->precision_max, 0, 100);
		}
		if (this->nodeExists(node, "Pauses")) {
			const ryml::NodeRef& p = node[c4::to_csubstr("Pauses")];
			this->readRange(p, "Every", t->pause_every_min, t->pause_every_max);
			this->readRange(p, "For", t->pause_for_min, t->pause_for_max);
		}
		this->readRange(node, "SkillGapMs", t->skill_gap_min, t->skill_gap_max);
		uint16_t pct = 0;
		if (this->nodeExists(node, "KillSteal") && this->asUInt16(node, "KillSteal", pct))
			t->kill_steal = static_cast<uint8_t>(std::min<uint16_t>(pct, 100));
		if (this->nodeExists(node, "Leash") && this->asUInt16(node, "Leash", pct))
			t->leash = static_cast<uint8_t>(std::min<uint16_t>(pct, 100));
		this->readPrefers(node, t->prefers);
		const auto existing = std::find_if(this->temperaments_.begin(), this->temperaments_.end(),
			[&name](const std::shared_ptr<const PopTemperament>& o) { return o->name == name; });
		if (existing != this->temperaments_.end())
			*existing = t;
		else
			this->temperaments_.push_back(t);
		this->temperament_by_name_[name] = t;
		return 1;
	}

	if (!this->nodeExists(node, "Build") || !this->asString(node, "Build", name)) {
		this->invalidWarning(node, "Each entry needs Temperament: or Build:.\n");
		return 0;
	}
	auto b = std::make_shared<PopBuild>();
	b->name = name;
	b->prefers.fill(100);

	if (!this->nodeExists(node, "Jobs") || !node[c4::to_csubstr("Jobs")].is_map()) {
		this->invalidWarning(node, "Build '%s' needs Jobs: { JobName: weight }.\n", name.c_str());
		return 0;
	}
	for (const ryml::NodeRef& c : node[c4::to_csubstr("Jobs")].children()) {
		const std::string job_name = pop_node_key(c);
		uint16_t job_id = 0;
		int32_t weight = 1;
		if (!this->readJobId(job_name, job_id)) {
			this->invalidWarning(c, "Build '%s': unknown job '%s' -- skipped.\n", name.c_str(), job_name.c_str());
			continue;
		}
		if (c.has_val() && !pop_node_int(c, weight)) {
			this->invalidWarning(c, "Build '%s': job '%s' needs a weight -- using 1.\n", name.c_str(), job_name.c_str());
			weight = 1;
		}
		if (weight >= 0) // 0: never rolled, only taken by the party slots that name it
			b->jobs.emplace_back(job_id, static_cast<uint16_t>(std::min(weight, 10000)));
	}
	if (b->jobs.empty()) {
		this->invalidWarning(node, "Build '%s' has no known job -- skipped.\n", name.c_str());
		return 0;
	}

	// asString logs a missing key as an error: these are optional
	for (const auto& [key, out] : { std::pair<const char*, std::string*>{ "SkillSet", &b->skill_set },
			{ "GearSet", &b->gear_set }, { "Names", &b->names }, { "Chat", &b->chat } })
		if (this->nodeExists(node, key))
			this->asString(node, key, *out);
	if (!b->skill_set.empty() && population_skill_db().find_set(b->skill_set) == nullptr)
		this->invalidWarning(node, "Build '%s': no skill set named '%s' in population_skill_db.yml -- the job's own skills are used.\n",
			name.c_str(), b->skill_set.c_str());

	if (this->nodeExists(node, "Stats")) {
		const ryml::NodeRef& st = node[c4::to_csubstr("Stats")];
		static const char* const keys[] = { "Str", "Agi", "Vit", "Int", "Dex", "Luk" };
		for (size_t i = 0; i < 6; i++) {
			int32_t lo = 0, hi = 0;
			if (this->readRange(st, keys[i], lo, hi)) {
				b->stats[i * 2] = static_cast<int16_t>(std::clamp(lo, 1, 999));
				b->stats[i * 2 + 1] = static_cast<int16_t>(std::clamp(hi, 1, 999));
			}
		}
	}
	int32_t melee = -1;
	if (this->nodeExists(node, "MeleeWeight") && this->asInt32(node, "MeleeWeight", melee))
		b->melee_weight = static_cast<int16_t>(std::clamp(melee, 0, 1000));
	this->readPrefers(node, b->prefers);

	if (this->nodeExists(node, "Temperaments")) {
		const ryml::NodeRef& tn = node[c4::to_csubstr("Temperaments")];
		if (tn.is_map()) {
			for (const ryml::NodeRef& c : tn.children()) {
				int32_t weight = 1;
				if (c.has_val() && !pop_node_int(c, weight))
					weight = 1;
				if (weight > 0)
					b->temperaments.emplace_back(pop_node_key(c), static_cast<uint16_t>(std::min(weight, 10000)));
			}
		} else {
			this->invalidWarning(tn, "Build '%s': Temperaments needs { name: weight }.\n", name.c_str());
		}
	}

	const auto existing = std::find_if(this->builds_.begin(), this->builds_.end(),
		[&name](const std::shared_ptr<const PopBuild>& o) { return o->name == name; });
	if (existing != this->builds_.end())
		*existing = b;
	else
		this->builds_.push_back(b);
	return 1;
}

void PopulationPersonalityDatabase::loadingFinished()
{
	this->builds_by_job_.clear();
	for (const auto& b : this->builds_) {
		for (const auto& job : b->jobs)
			this->builds_by_job_[job.first].emplace_back(b, job.second);
		for (const auto& t : b->temperaments)
			if (this->temperament_by_name_.find(t.first) == this->temperament_by_name_.end())
				ShowWarning("population_personality: build '%s' names unknown temperament '%s' -- left out.\n",
					b->name.c_str(), t.first.c_str());
	}
}

void PopulationPersonalityDatabase::roll(uint16_t job_id, std::shared_ptr<const PopBuild>& build,
	std::shared_ptr<const PopTemperament>& temperament) const
{
	build = nullptr;
	temperament = nullptr;
	const auto bit = this->builds_by_job_.find(job_id);
	if (bit != this->builds_by_job_.end())
		if (const auto* picked = pop_pick_weighted(bit->second))
			build = *picked;

	std::vector<std::pair<std::shared_ptr<const PopTemperament>, uint16_t>> pool;
	if (build != nullptr && !build->temperaments.empty()) {
		for (const auto& t : build->temperaments) {
			const auto tit = this->temperament_by_name_.find(t.first);
			if (tit != this->temperament_by_name_.end())
				pool.emplace_back(tit->second, t.second);
		}
	}
	if (pool.empty())
		for (const auto& t : this->temperaments_)
			pool.emplace_back(t, t->weight);
	if (const auto* picked = pop_pick_weighted(pool))
		temperament = *picked;
}

bool PopulationPersonalityDatabase::has_build(const std::string& name) const
{
	return std::any_of(this->builds_.begin(), this->builds_.end(), [&name](const std::shared_ptr<const PopBuild>& b) { return b->name == name; });
}

std::shared_ptr<const PopBuild> PopulationPersonalityDatabase::find_build(const std::string& name, uint16_t job_id) const
{
	const auto bit = this->builds_by_job_.find(job_id);
	if (bit == this->builds_by_job_.end())
		return nullptr;
	for (const auto& entry : bit->second)
		if (entry.first->name == name)
			return entry.first;
	return nullptr;
}

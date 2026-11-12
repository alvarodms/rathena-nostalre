// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: population_levels.hpp's rules. No framework: each failed check prints its line, the exit code counts them.
#include <cstdio>

#include <map/population_engine/core/population_levels.hpp>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

using namespace pop_levels;

static void test_class_levels()
{
	CHECK(class_levels(Tier::Second).lo == 55); // no level 45 Knights
	CHECK(class_levels(Tier::TransSecond).lo == 60);
	CHECK(class_levels(Tier::First).hi == 70);
	CHECK(class_levels(Tier::Novice).hi == 15);
}

static void test_party_default()
{
	// Spores (18): from 10 to 25, below the monsters as much as above
	CHECK(party_default(18).lo == 10 && party_default(18).hi == 25);
	CHECK(party_default(3).lo == 1);
}

static void test_party_window()
{
	const Range range = { 40, 60 };
	for (int roll = 0; roll <= 20; roll++) {
		const Range w = party_window(range, roll);
		CHECK(w.lo >= 40 && w.hi <= 60 && w.lo <= w.hi);
		CHECK(w.hi - w.lo <= 14); // within party_share_level's 15
	}
	CHECK(party_window(range, 0).lo == 40 && party_window(range, 0).hi == 47);
	CHECK(party_window(range, 99).hi == 60);
	CHECK(party_window({ 30, 35 }, 3).lo == 30 && party_window({ 30, 35 }, 3).hi == 35);
}

static void test_member_level()
{
	const Range w = { 46, 60 };
	CHECK(member_level(w, 0, class_levels(Tier::First)) == 46);
	CHECK(member_level(w, 99, class_levels(Tier::First)) == 60);
	CHECK(member_level({ 95, 110 }, 15, class_levels(Tier::First)) == 70); // held to its class
	CHECK(member_level(w, 0, class_levels(Tier::Second)) == 55);
}

int main()
{
	test_class_levels();
	test_party_default();
	test_party_window();
	test_member_level();
	if (failures == 0)
		std::printf("population_levels: all checks passed\n");
	return failures;
}

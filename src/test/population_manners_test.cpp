// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// nostalRE: population_manners.hpp's rules. No framework: each failed check prints its line, the exit code counts them.
#include <cstdio>

#include <map/population_engine/core/population_manners.hpp>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

using namespace pop_manners;

static void test_reply_delay()
{
	// a quick shell still takes a moment to read and type; a slow one takes longer
	CHECK(reply_delay_ms(0, 20, 50) == 700 + 20 * 55);
	CHECK(reply_delay_ms(900, 20, 0) < reply_delay_ms(900, 20, 99));
	CHECK(reply_delay_ms(900, 20, 99) - reply_delay_ms(0, 20, 99) == 900 * 133 / 100);
	CHECK(reply_delay_ms(300, 40, 50) > reply_delay_ms(300, 10, 50));
}

static void test_skill_pacing()
{
	CHECK(repeat_factor(0) == 1.0);
	CHECK(repeat_factor(1) > repeat_factor(2) && repeat_factor(2) > repeat_factor(3));
	CHECK(repeat_factor(9) == repeat_factor(3));
	CHECK(skill_gap_ms(200, 500, 0, 0) == 200);
	CHECK(skill_gap_ms(200, 500, 99, 0) == 500);
	CHECK(skill_gap_ms(500, 200, 99, 0) == 500); // a range written backwards
	CHECK(skill_gap_ms(200, 500, 0, 700) == 700); // not before its swing is over
}

static void test_kill_steal()
{
	CHECK(mob_open({}, false));                     // nobody's
	CHECK(!mob_open({ false, false, true }, false)); // someone else's
	CHECK(mob_open({ true, false, true }, false));   // after us: we fight back
	CHECK(mob_open({ false, true, true }, false));   // our fight too
	CHECK(mob_open({ false, false, true }, true));   // a kill stealer
	CHECK(!kill_steal_roll(7, 1234, 0));
	CHECK(kill_steal_roll(7, 1234, 100));
	CHECK(kill_steal_roll(7, 1234, 30) == kill_steal_roll(7, 1234, 30)); // no reroll on the next tick
	int stolen = 0;
	for (uint32_t mob = 0; mob < 1000; mob++)
		stolen += kill_steal_roll(42, 100000 + mob, 10) ? 1 : 0;
	CHECK(stolen > 50 && stolen < 150);
	CHECK(map_wide_party(0x70000000 | 12));
	CHECK(!map_wide_party(0x78000000 | 12)); // a shell party's own
	CHECK(!map_wide_party(0));
	CHECK(!map_wide_party(1500));
}

static void test_roam_leash()
{
	CHECK(roam_leash(40, 300, 200, 0) == 40);
	CHECK(roam_leash(0, 300, 200, 0) == 300);   // the whole map
	CHECK(roam_leash(0, 300, 200, 40) == 120);  // careful: 40% of the longer side
	CHECK(roam_leash(60, 300, 200, 40) == 60);  // the slot's leash is shorter
	CHECK(roam_leash(0, 20, 20, 10) == 10);     // never a leash too short to hunt in
}

static void test_party_name()
{
	CHECK(camp_party_name("Ragnar") == "Ragnar Party");
	CHECK(camp_party_name("Ra'gn.ar_") == "Ragnar Party");
	CHECK(camp_party_name("  Big  Bob ") == "Big Bob Party");
	CHECK(camp_party_name("Abcdefghijklmnopqrstuvwx") == "Abcdefghijklmnopq Party");
	CHECK(camp_party_name("._.") == "");
}

int main()
{
	test_reply_delay();
	test_skill_pacing();
	test_kill_steal();
	test_roam_leash();
	test_party_name();
	if (failures == 0)
		std::printf("population manners: all passed\n");
	return failures;
}

#pragma once

#include <map>
#include "species-type.h"
#include "skill-type.h"

namespace species
{
    map<skill_type,int> get_monster_apts();
    void setup_monster_player(bool game_start=true);
    void give_basic_monster_mutations(mc_species species);
    const vector<string> monster_fake_mutations(mc_species species, bool terse);
    void monster_stat_init(mc_species species);
}

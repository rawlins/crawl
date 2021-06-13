#pragma once

#include <string>
#include <vector>

#include "game-type.h"
#include "item-prop-enum.h"
#include "job-type.h"
#include "monster-type.h"
#include "species-type.h"

using std::string;
using std::vector;

// Either a character definition, with real species, job, and
// weapon, book, wand as appropriate.
// Or a character choice, with possibly random/viable entries.
struct newgame_def
{
    string name;
    game_type type;
    string filename;
    uint64_t seed;
    bool pregenerate;

    // map name for sprint (or others in the future)
    // XXX: "random" means a random eligible map
    string map;

    string arena_teams;

    vector<string> allowed_combos;
    vector<species_type> allowed_species;
    vector<job_type> allowed_jobs;
    vector<weapon_type> allowed_weapons;

    species_type species;
    job_type job;
    monster_type monster_species;

    weapon_type weapon;
    string monster_item_override;

    // Only relevant for character choice, where the entire
    // character was randomly picked in one step.
    // If this is true, the species field encodes whether
    // the choice was for a viable character or not.
    bool fully_random;

    newgame_def();
    void clear_character();
};

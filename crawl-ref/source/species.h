#pragma once

#include <vector>

#include "enum.h"
#include "ability-type.h"
#include "equipment-type.h"
#include "energy-use-type.h"
#include "item-prop-enum.h"
#include "job-type.h"
#include "size-part-type.h"
#include "size-type.h"
#include "species-def.h"
#include "species-type.h"

using std::vector;

const species_def &get_species_def_raw(species_type species);

// union class for MonsterCrawl species; in most scenarios can be dropped where
// a species_type was used.
struct mc_species
{
    mc_species() : base(SP_UNKNOWN), mon_species(MONS_NO_MONSTER) { }
    mc_species(const species_type &other)
        : base(other), mon_species(MONS_NO_MONSTER) { }
    mc_species(const monster_type &other)
        : base(SP_MONSTER), mon_species(other) { }
    mc_species(const mc_species &other) = default;

    mc_species &operator=(const mc_species &other)
    {
        if (this == &other)
            return *this;
        base = other.base;
        mon_species = other.mon_species;
        return *this;
    }

    mc_species &operator=(const species_type &other)
    {
        base = other;
        mon_species = MONS_NO_MONSTER;
        return *this;
    }

    mc_species &operator=(const monster_type &other)
    {
        base = SP_MONSTER;
        mon_species = other;
        return *this;
    }

    bool operator==(const species_type &other) const
    {
        return base == other;
    }

    bool operator!=(const species_type &other) const
    {
        return !(*this == other);
    }

    bool operator==(const monster_type &other) const
    {
        return base == SP_MONSTER && mon_species == other;
    }

    bool operator!=(const monster_type &other) const
    {
        return !(*this == other);
    }

    bool operator==(const mc_species &other) const
    {
        return other.base == base && other.mon_species == mon_species;
    }

    bool operator!=(const mc_species &other) const
    {
        return !(*this == other);
    }


    operator species_type() const { return base; }
    operator monster_type() const { return mon_species; }

    species_type genus() const;
    bool is_monster() const { return base == SP_MONSTER; }
    bool is_genus_monster() const { return genus() == SP_MONSTER; }
    bool is_valid() const;

    species_type base;
    monster_type mon_species;
};

const species_def &get_species_def(mc_species species);

namespace species
{
    enum name_type
    {
        SPNAME_PLAIN,
        SPNAME_GENUS,
        SPNAME_ADJ
    };

    string name(species_type speci, name_type spname = SPNAME_PLAIN);
    const char *get_abbrev(species_type which_species);
    species_type from_abbrev(const char *abbrev);
    species_type from_str(const string &species);
    species_type from_str_loose(const string &species_str,
                                                    bool initial_only = false);
    string player_monster_name(bool full_desc=false);

    bool is_elven(mc_species species);
    bool is_orcish(mc_species species);
    bool is_undead(mc_species species);
    bool is_draconian(mc_species species);
    undead_state_type undead_type(mc_species species) PURE;
    monster_type to_mons_species(species_type species);
    bool is_player_species_equiv(monster_type m);

    monster_type dragon_form(mc_species s);
    const char* scale_type(mc_species species);
    ability_type draconian_breath(mc_species species);
    species_type random_draconian_colour();

    int mutation_level(mc_species species, mutation_type mut, int mut_level=1);
    const vector<string> fake_mutations(mc_species species, bool terse);
    bool has_hair(mc_species species);
    bool has_bones(mc_species species);
    bool can_throw_large_rocks(mc_species species);
    bool wears_barding(mc_species species);
    bool has_claws(mc_species species);
    bool is_nonliving(mc_species species);
    bool can_swim(mc_species species);
    bool likes_water(mc_species species);
    bool likes_lava(mc_species species);
    bool likes_land(mc_species species);
    size_type size(mc_species species, size_part_type psize = PSIZE_TORSO);

    string walking_verb(mc_species sp);
    string prayer_action(mc_species species);
    string shout_verb(mc_species sp, int screaminess, bool directed);
    string skin_name(mc_species sp, bool adj=false);
    string arm_name(mc_species species);
    string hand_name(mc_species species);
    int arm_count(mc_species species);
    equipment_type sacrificial_arm(mc_species species);
    bool bans_eq(mc_species species, equipment_type eq);
    vector<equipment_type> ring_slots(mc_species species, bool missing_hand);

    int get_exp_modifier(mc_species species);
    int get_hp_modifier(mc_species species);
    int get_mp_modifier(mc_species species);
    int get_wl_modifier(mc_species species);
    int get_stat_gain_multiplier(mc_species species);
    bool has_low_str(mc_species species);
    bool recommends_job(species_type species, job_type job);
    bool recommends_weapon(species_type species, weapon_type wpn);

    bool is_valid(species_type species);
    bool is_starting_species(species_type species);
    species_type random_starting_species();
    bool is_removed(species_type species);
    vector<species_type> get_all_species();
}

void species_stat_init(mc_species species);
void species_stat_gain(mc_species species);

monster_type player_species_to_mons_species(species_type species);
species_type mons_species_to_player_species(monster_type mons);

int mons_energy_to_delay(monster &m, energy_use_type et);

void give_basic_mutations(mc_species species);
void give_level_mutations(mc_species species, int xp_level);

void specialize_species_to(monster_type mt);
void despecialize_species();
void change_species_to(mc_species sp, shared_ptr<monster> minstance = nullptr);

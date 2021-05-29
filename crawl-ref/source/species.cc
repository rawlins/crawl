#include "AppHdr.h"
#include <map>

#include "mpr.h"
#include "species.h"

#include "chardump.h"
#include "database.h"
#include "describe.h"
#include "item-prop.h"
#include "mon-util.h"
#include "monster.h"
#include "mutation.h"
#include "ng-setup.h"
#include "output.h"
#include "playable.h"
#include "player.h"
#include "player-stats.h"
#include "random.h"
#include "skills.h"
#include "stringutil.h"
#include "tag-version.h"
#include "tiledoll.h"

#include "species-data.h"

species_type mc_species::genus() const
{
    // The genus of any player species is itself. The genus of any monster
    // with a base class matching a player species is that player species;
    // otherwise it is SP_MONSTER.
    // TODO: maybe the genus of a player draconian is a draconian?

    // returns SP_MONSTER if mon_species is invalid
    if (is_monster())
        return mons_species_to_player_species(mon_species);
    else
        return base;
}

bool mc_species::is_valid() const
{
    return species::is_valid(base)
        && (!is_monster() || !invalid_monster_type(mon_species));
}

/*
 * Get the species_def for the given species type. Asserts if the species_type
 * is not less than NUM_SPECIES.
 *
 * @param species The species type.
 * @returns The species_def of that species.
 */
const species_def &get_species_def_raw(species_type species)
{
    if (species != SP_UNKNOWN)
        ASSERT_RANGE(species, 0, NUM_SPECIES);
    return species_data.at(species);
}

// convenience wrapper -- always use genus if provided a full mc species. This
// is aimed at making merges easier...
const species_def &get_species_def(mc_species species)
{
    return get_species_def_raw(species.genus());
}

namespace species
{
    /**
     * Return the name of the given species.
     * @param speci       the species to be named.
     * @param spname_type the kind of name to get: adjectival, the genus, or plain.
     * @returns the requested name, which will just be plain if no adjective
     *          or genus is defined.
     */
    string name(species_type speci, name_type spname_type)
    {
        const species_def& def = get_species_def(speci);
        if (spname_type == SPNAME_GENUS && def.genus_name)
            return def.genus_name;
        else if (spname_type == SPNAME_ADJ && def.adj_name)
            return def.adj_name;
        return def.name;
    }

    string player_monster_name(bool full_desc)
    {
        if (you.species != SP_MONSTER)
            return "";
        else if (!you.monster_instance) // XX is this actually used?
            return mons_type_name(you.species.mon_species, DESC_PLAIN);

        string r;
        const bool ex_rider = you.base_monster_instance
            && you.base_monster_instance->type == MONS_SPRIGGAN_RIDER;
        const monster_type shapeshifter =
              you.monster_instance->has_ench(ENCH_GLOWING_SHAPESHIFTER)
            ? MONS_GLOWING_SHAPESHIFTER
            : you.monster_instance->has_ench(ENCH_SHAPESHIFTER)
            ? MONS_SHAPESHIFTER
            : MONS_PROGRAM_BUG; // 0

        if (full_desc)
        {
            // includes an article
            monster_info mi(you.monster_instance.get());

            r = getMiscString(mi.common_name(DESC_DBNAME) + " title");
            if (r.empty())
                r = lowercase_first(mi.full_name(DESC_A));
            // not tracked for monster, but we do want it for player:
            if (ex_rider)
                r += " ex-rider";
            // shifters will get "an X shaped shifter" here, but annoyingly,
            // glowiness does not seem to be handled in monster_info at all:
            if (shapeshifter == MONS_GLOWING_SHAPESHIFTER && you.species != shapeshifter)
                r += " (glowing)";
        }
        else
        {
            // TODO: in previous versions, we added "Monstrous" here for things
            // that could be confused with player species. Is this still
            // helpful?

            r += you.monster_instance->full_name(DESC_PLAIN);
            if (ex_rider)
                r += " ex-rider";
            // XX can a shapeshifter turn into a spriggan rider and then die?
            if (shapeshifter && you.species != shapeshifter)
                r += shapeshifter == MONS_SHAPESHIFTER ? " (shifter)" : " (glowing)";
        }
        return r;
    }

    /// Does exact case-sensitive lookup of a species name
    species_type from_str(const string &species)
    {
        species_type sp;
        if (species.empty())
            return SP_UNKNOWN;

        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            sp = static_cast<species_type>(i);
            if (species == name(sp))
                return sp;
        }

        return SP_UNKNOWN;
    }

    /// Does loose, non-case-sensitive lookup of a species based on a string
    species_type from_str_loose(const string &species, bool initial_only)
    {
        // XX consolidate with from_str?
        string spec = lowercase_string(species);

        species_type sp = SP_UNKNOWN;

        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            const species_type si = static_cast<species_type>(i);
            const string sp_name = lowercase_string(name(si));

            string::size_type pos = sp_name.find(spec);
            if (pos != string::npos)
            {
                if (pos == 0)
                {
                    // We prefer prefixes over partial matches.
                    sp = si;
                    break;
                }
                else if (!initial_only)
                    sp = si;
            }
        }

        return sp;
    }


    species_type from_abbrev(const char *abbrev)
    {
        if (lowercase_string(abbrev) == "dr")
            return SP_BASE_DRACONIAN;

        for (auto& entry : species_data)
            if (lowercase_string(abbrev) == lowercase_string(entry.second.abbrev))
                return entry.first;

        return SP_UNKNOWN;
    }

    const char *get_abbrev(species_type which_species)
    {
        return get_species_def(which_species).abbrev;
    }

    bool is_elven(mc_species species)
    {
        return species.genus() == SP_DEEP_ELF;
    }

    bool is_orcish(mc_species species)
    {
        return species.genus() == SP_HILL_ORC;
    }

    bool is_undead(mc_species species)
    {
        return undead_type(species.genus()) != US_ALIVE;
    }

    bool is_draconian(mc_species species)
    {
        // TODO: fix for monster dr
        return bool(get_species_def(species.genus()).flags & SPF_DRACONIAN);
    }

    // A random non-base draconian colour appropriate for the player.
    species_type random_draconian_colour()
    {
        species_type species;
        do {
            species =
                static_cast<species_type>(random_range(0, NUM_SPECIES - 1));
        } while (!is_draconian(species)
               || is_removed(species)
               || species == SP_BASE_DRACONIAN);

        return species;
    }

    /**
     * Where does a given species fall on the Undead Spectrum?
     *
     * @param species   The species in question.
     * @return          What class of undead the given species falls on, if any.
     */
    undead_state_type undead_type(mc_species species)
    {
        // TODO: monster undead
        return get_species_def(species.genus()).undeadness;
    }

    monster_type to_mons_species(species_type species)
    {
        return get_species_def(species).monster_species;
    }

    bool is_player_species_equiv(monster_type m)
    {
        for (auto& entry : species_data)
            if (entry.second.monster_species == m)
                return true;
        return false;
    }

    // XX non-draconians, unify with skin names?
    const char* scale_type(mc_species species)
    {
        // TODO: other dragon species
        if (species == MONS_BAI_SUZHEN_DRAGON)
            return "glossy black";
        switch (species.genus())
        {
            case SP_RED_DRACONIAN:
                return "fiery red";
            case SP_WHITE_DRACONIAN:
                return "icy white";
            case SP_GREEN_DRACONIAN:
                return "lurid green";
            case SP_YELLOW_DRACONIAN:
                return "golden yellow";
            case SP_GREY_DRACONIAN:
                return "dull iron-grey";
            case SP_BLACK_DRACONIAN:
                return "glossy black";
            case SP_PURPLE_DRACONIAN:
                return "rich purple";
            case SP_PALE_DRACONIAN:
                return "pale cyan-grey";
            case SP_BASE_DRACONIAN:
                return "plain brown";
            default:
                return "";
        }
    }

    monster_type dragon_form(mc_species s)
    {
        if (s.is_genus_monster()
                        && mons_genus(s) == MONS_DRAGON)
        {
            // TODO: probably disable dragon form for most of these cases
            return mons_species(s);
        }

        // does this happen automatically?
        if (s == MONS_BAI_SUZHEN)
            return MONS_STORM_DRAGON;

        switch (s.genus())
        {
        case SP_WHITE_DRACONIAN:
            return MONS_ICE_DRAGON;
        case SP_GREEN_DRACONIAN:
            return MONS_SWAMP_DRAGON;
        case SP_YELLOW_DRACONIAN:
            return MONS_GOLDEN_DRAGON;
        case SP_GREY_DRACONIAN:
            return MONS_IRON_DRAGON;
        case SP_BLACK_DRACONIAN:
            return MONS_STORM_DRAGON;
        case SP_PURPLE_DRACONIAN:
            return MONS_QUICKSILVER_DRAGON;
        case SP_PALE_DRACONIAN:
            return MONS_STEAM_DRAGON;
        case SP_RED_DRACONIAN:
        default:
            return MONS_FIRE_DRAGON;
        }
    }

    ability_type draconian_breath(mc_species species)
    {
        switch (species.genus())
        {
        case SP_GREEN_DRACONIAN:   return ABIL_BREATHE_MEPHITIC;
        case SP_RED_DRACONIAN:     return ABIL_BREATHE_FIRE;
        case SP_WHITE_DRACONIAN:   return ABIL_BREATHE_FROST;
        case SP_YELLOW_DRACONIAN:  return ABIL_BREATHE_ACID;
        case SP_BLACK_DRACONIAN:   return ABIL_BREATHE_LIGHTNING;
        case SP_PURPLE_DRACONIAN:  return ABIL_BREATHE_POWER;
        case SP_PALE_DRACONIAN:    return ABIL_BREATHE_STEAM;
        case SP_BASE_DRACONIAN: case SP_GREY_DRACONIAN:
        default: return ABIL_NON_ABILITY;
        }
    }

    /// Does the species have (real) mutation `mut`? Not for demonspawn.
    /// @return the first xl at which the species gains the mutation, or 0 if it
    ///         does not ever gain it.
    int mutation_level(mc_species species, mutation_type mut, int mut_level)
    {
        int total = 0;
        // relies on levels being in order -- I think this is safe?
        for (const auto& lum : get_species_def(species).level_up_mutations)
            if (mut == lum.mut)
            {
                total += lum.mut_level;
                if (total >= mut_level)
                    return lum.xp_level;
            }

        return 0;
    }

    const vector<string> fake_mutations(mc_species species, bool terse)
    {
        vector<string> result;

        if (species.is_monster() && you.monster_instance)
        {
            if ((mons_class_itemuse(species) <= MONUSE_OPEN_DOORS
                    || mons_intel(*you.monster_instance) <= I_ANIMAL)
                // specific monsters that this is not flavorful on.
                && species != MONS_ROYAL_JELLY
                && species != MONS_DISSOLUTION
                && mons_genus(species) != MONS_DRAGON)
            {
                // just some lore to explain why they can do things like open doors
                // and maybe read
                result.push_back(terse ? "uplifted" : "You are unnaturally intelligent for one of your kind.");
            }

            if (you.heads() != 1)
                result.push_back(terse ? make_stringf("%d heads", you.heads()) : make_stringf("You have %d heads.", you.heads()));

            if (!terse)
            {
                // sort of hacky...
                // TODO: this is pretty clumsy for multi-attack monsters
                const auto attacks = monster_attacks_description(monster_info(you.monster_instance.get()));
                for (auto &a : split_string("\n", attacks))
                    result.push_back(a);
            }
            else if (mons_class_flag(species, M_ACID_SPLASH))
                result.push_back("acid splash");  // TODO: a ton of other attack flavors?

            // Only juggernaut has custom values for this, but many monsters will
            // have an attack speed modifier based on their base speed.
            const int attack_delay = mons_energy_to_delay(*you.monster_instance, EUT_ATTACK);
            if (attack_delay > 18)
                result.push_back(terse ? "very slow attacks" : "You attack very slowly.");
            else if (attack_delay > 10)
                result.push_back(terse ? "slow attacks" : "You attack slowly.");
            else if (attack_delay < 5)
                result.push_back(terse ? "very quick attacks" : "You attack very quickly.");
            else if (attack_delay < 10)
                result.push_back(terse ? "quick attacks" : "You attack quickly.");

            // spell energy: only orb spider has a custom value for this
            // TODO: maybe this should just apply to special species abilities?
            const int move_delay = mons_energy_to_delay(*you.monster_instance, EUT_MOVE);
            const int spell_delay = mons_energy_to_delay(*you.monster_instance, EUT_SPELL);
            if (spell_delay > move_delay)
                result.push_back(terse ? "slow casting" : "You cast spells slowly.");
            else if (spell_delay < move_delay)
                result.push_back(terse ? "quick casting" : "You cast spells quickly.");

            if (!you.has_mutation(MUT_NO_GRASPING))
            {
                // only DE master archer has a custom value for this, but some monsters
                // may gain this by having a higher base speed.
                const int missile_delay = mons_energy_to_delay(*you.monster_instance, EUT_MISSILE);
                if (missile_delay > 10)
                    result.push_back(terse ? "slow shooting" : "You fire missiles slowly.");
                else if (missile_delay < 10)
                    result.push_back(terse ? "quick shooting" : "You fire missiles quickly.");
            }

            // TODO: is there a better way to check for no rings at all?
            // reconcile with mutation.cc ring mut code?
            if (you_can_wear(EQ_RING_ONE) == MB_FALSE && you_can_wear(EQ_RIGHT_RING) == MB_FALSE)
                result.push_back(terse ? "no rings" : "You cannot wear rings.");
            // right now, everything can wear an amulet, somehow
            // TODO: a bit more variation in ring possibilities

            // generalize mummies
            if (you.monster_instance->res_poison() < 0)
                result.push_back(terse ? "rPois-" : "You are vulnerable to poison.");

            if (you.monster_instance->how_chaotic())
                result.push_back(terse ? "chaotic" : "You are vulnerable to silver and hated by Zin.");

            // a bunch of energy-related things are implemented as fakemuts, plus
            // custom logic in player.cc. The only exception is movement speed,
            // which is mapped on to the fast/slow muts.
            const int swim_delay = mons_energy_to_delay(*you.monster_instance, EUT_SWIM);
            if (swim_delay < move_delay)
                result.push_back(terse ? "fast swimming" : "You swim quickly.");
            else if (swim_delay > move_delay)
                result.push_back(terse ? "slow swimming" : "You swim slowly.");

            if (mons_class_flag(you.species, M_CONFUSED))
                result.push_back(terse ? "confused" : "You are permanently confused.");

            // invis is already shown as a status
            if (mons_class_flag(you.species, M_INVIS) && !terse)
                result.push_back("You are permanently invisible.");

            // species-specific stuff
            if (species == MONS_SILENT_SPECTRE)
                result.push_back(terse ? "silence" : "You are surrounded by an aura of silence.");
            if (species == MONS_ELEMENTAL_WELLSPRING)
                result.push_back(terse ? "watery" : "You exude water.");
            else if (species == MONS_WATER_NYMPH)
                result.push_back(terse ? "flood" : "You exude a watery aura.");
            else if (species == MONS_TORPOR_SNAIL)
                result.push_back(terse ? "slowing aura" : "Your aura slows creatures around you.");
            else if (species == MONS_ANCIENT_ZYME)
                result.push_back(terse ? "sickening aura" : "Your aura sickens creatures around you.");
            else if (species == MONS_ASMODEUS)
                result.push_back(terse ? "flames" : "You are wreathed in flames.");
            else if (species == MONS_GUARDIAN_GOLEM)
                result.push_back(terse ? "injury bond" : "You share your allies injuries.");
            else if (species == MONS_SHADOW_DRAGON) // no good realmut for this
                result.push_back(terse ? "shadowy" : "Your umbral scales blend in with the shadows (Stealth++++).");

            return result;
        }

        if (!species.is_genus_monster())
        {
            // XX do these ever conflict with monster muts?
            const auto &genus_muts = terse
                     ? get_species_def(species.genus()).terse_fake_mutations
                     : get_species_def(species.genus()).verbose_fake_mutations;
            result.insert(result.end(), genus_muts.begin(), genus_muts.end());
        }

        return result;
    }

    bool has_hair(mc_species species)
    {
        return !bool(get_species_def(species.genus()).flags & (SPF_NO_HAIR | SPF_DRACONIAN));
    }

    bool has_bones(mc_species species)
    {
        return !bool(get_species_def(species.genus()).flags & SPF_NO_BONES);
    }

    bool can_throw_large_rocks(mc_species species)
    {
        return size(species) >= SIZE_LARGE;
    }

    bool wears_barding(mc_species species)
    {
        return bool(get_species_def(species.genus()).flags & SPF_SMALL_TORSO);
    }

    bool has_claws(mc_species species)
    {
        return mutation_level(species.genus(), MUT_CLAWS) == 1;
    }

    bool is_nonliving(mc_species species)
    {
        // XXX: move to data?
        return species.genus() == SP_GARGOYLE || species.genus() == SP_DJINNI;
    }

    bool can_swim(mc_species species)
    {
        if (species.is_genus_monster() && you.monster_instance)
        {
            // amphibious monsters are treated a bit different than amphibious
            // player species (which don't actually exist): assume that amphibious
            // monsters can swim by default. If there are any that shouldn't for
            // flavor reasons, that could be overridden here.
            // giant players wading are handled elsewhere
            auto ht = mons_habitat(*you.monster_instance, false);
            switch (ht)
            {
            case HT_AMPHIBIOUS:
            case HT_WATER:
                return true;
            default:
                return false; // Lava not handled here!
            }
        }

        return get_species_def(species).habitat == HT_WATER;
    }

    bool likes_water(mc_species species)
    {
        return can_swim(species)
               || get_species_def(species).habitat == HT_AMPHIBIOUS;
    }

    bool likes_lava(mc_species species)
    {
        const habitat_type ht = species.is_genus_monster() && you.monster_instance
                    ? mons_habitat(*you.monster_instance, false)
                    : get_species_def(species).habitat;
        switch (ht)
        {
        case HT_AMPHIBIOUS_LAVA:
        case HT_LAVA:
            return true;
        default:
            return false;
        }
        // n.b. no regular species currently likes lava
    }

    // it's a bit gimmicky to bother implementing this, but so be it
    bool likes_land(mc_species species)
    {
        if (!species.is_genus_monster() || !you.monster_instance)
            return true;

        const habitat_type ht = mons_habitat(*you.monster_instance, false);

        switch (ht)
        {
        case HT_AMPHIBIOUS_LAVA:
        case HT_AMPHIBIOUS:
        case HT_LAND:
            return true;
        default:
            return false;
        }
        // n.b. no regular species dislikes land
    }

    size_type size(mc_species species, size_part_type psize)
    {
        const size_type size = species.is_genus_monster()
                    ? you.monster_instance->body_size() // TODO: psize?
                    : get_species_def(species).size;
        if (psize == PSIZE_TORSO
            && bool(get_species_def(species).flags & SPF_SMALL_TORSO))
        {
            return static_cast<size_type>(static_cast<int>(size) - 1);
        }
        return size;
    }


    /** What walking-like thing does this species do?
     *
     *  @param sp what kind of species to look at
     *  @returns a "word" to which "-er" or "-ing" can be appended.
     */
    string walking_verb(mc_species sp)
    {
        auto verb = get_species_def(sp.genus()).walking_verb;
        return verb ? verb : "Walk";
    }

    /**
     * What message should be printed when a character of the specified species
     * prays at an altar, if not in some form?
     * To be inserted into "You %s the altar of foo."
     *
     * @param species   The species in question.
     * @return          An action to be printed when the player prays at an altar.
     *                  E.g., "coil in front of", "kneel at", etc.
     */
    string prayer_action(mc_species species)
    {
      auto action = get_species_def(species.genus()).altar_action;
      return action ? action : "kneel at";
    }

    static const string shout_verbs[] = {"shout", "yell", "scream"};
    static const string felid_shout_verbs[] = {"meow", "yowl", "caterwaul"};
    static const string frog_shout_verbs[] = {"croak", "ribbit", "bellow"};
    static const string dog_shout_verbs[] = {"bark", "howl", "screech"};

    /**
     * What verb should be used to describe the species' shouting?
     * @param sp a species
     * @param screaminess a loudness level; in range [0,2]
     * @param directed with this is to be directed at another actor
     * @return A shouty kind of verb
     */
    string shout_verb(mc_species sp, int screaminess, bool directed)
    {
        screaminess = max(min(screaminess,
                                static_cast<int>(sizeof(shout_verbs) - 1)), 0);
        switch (sp.genus())
        {
        case SP_GNOLL:
            if (screaminess == 0 && directed && coinflip())
                return "growl";
            return dog_shout_verbs[screaminess];
        case SP_BARACHI:
            return frog_shout_verbs[screaminess];
        case SP_FELID:
            if (screaminess == 0 && directed)
                return "hiss"; // hiss at, not meow at
            return felid_shout_verbs[screaminess];
        default:
            return shout_verbs[screaminess];
        }
    }

    /**
     * Return an adjective or noun for the species' skin.
     * @param adj whether to provide an adjective (if true), or a noun (if false).
     * @return a non-empty string. Nouns will be pluralised if they are count nouns.
     *         Right now, plurality can be determined by `ends_with(noun, "s")`.
     */
    string skin_name(mc_species s, bool adj)
    {
        const species_type species = s.genus();
        // Aside from direct uses, some flavor stuff checks the strings
        // here. TODO: should some of these be species flags a la hair?
        // Also, some skin mutations should have a way of overriding these perhaps
        if (is_draconian(species) || species == SP_NAGA)
            return adj ? "scaled" : "scales";
        else if (species == SP_TENGU)
            return adj ? "feathered" : "feathers";
        else if (species == SP_FELID)
            return adj ? "furry" : "fur";
        else if (species == SP_MUMMY)
            return adj ? "bandage-wrapped" : "bandages";
        else
            return adj ? "fleshy" : "skin";
    }

    string arm_name(mc_species s)
    {
        const species_type species = s.genus();
        if (mutation_level(species, MUT_TENTACLE_ARMS))
            return "tentacle";
        else if (species == SP_FELID)
            return "leg";
        else
            return "arm";
    }

    string hand_name(mc_species s)
    {
        const species_type species = s.genus();
        // see also player::hand_name
        if (mutation_level(species, MUT_PAWS))
            return "paw";
        else if (mutation_level(species, MUT_TENTACLE_ARMS))
            return "tentacle";
        else if (mutation_level(species, MUT_CLAWS))
            return "claw"; // overridden for felids by first check
        else
            return "hand";
    }

    int arm_count(mc_species species)
    {
        return species.genus() == SP_OCTOPODE ? 8 : 2;
    }

    equipment_type sacrificial_arm(mc_species species)
    {
        // this is a bit special-case-y because the sac slot doesn't follow
        // from the enum; for 2-armed species it is the left ring (which is first),
        // but for 8-armed species it is ring 8 (which is last).
        // XX maybe swap the targeted sac hand? But this requires some painful
        // save compat
        return arm_count(species) == 2 ? EQ_LEFT_RING : EQ_RING_EIGHT;
    }

    /**
     *  Checks some species-level equipment slot constraints. Anything hard-coded
     *  per species, but not handled by a mutation should be here. See also
     *  player.cc::you_can_wear and item-use.cc::can_wear_armour for the full
     *  division of labor. This function is guaranteed to handle species ring
     *  slots.
     *
     *  @param species the species type to check
     *  @param eq the equipment slot to check
     *  @return true if the equipment slot is not used by the species; false
     *          indicates only that nothing in this check bans the slot. For
     *          example, this function does not check felid mutations.
     */
    bool bans_eq(mc_species species, equipment_type eq)
    {
        const int arms = arm_count(species);
        // only handles 2 or 8
        switch (eq)
        {
        case EQ_LEFT_RING:
        case EQ_RIGHT_RING:
            return arms > 2;
        case EQ_RING_ONE:
        case EQ_RING_TWO:
        case EQ_RING_THREE:
        case EQ_RING_FOUR:
        case EQ_RING_FIVE:
        case EQ_RING_SIX:
        case EQ_RING_SEVEN:
        case EQ_RING_EIGHT:
            return arms <= 2;
        // not banned by any species
        case EQ_AMULET:
        case EQ_RING_AMULET:
        // not handled here:
        case EQ_WEAPON:
        case EQ_STAFF:
        case EQ_RINGS:
        case EQ_RINGS_PLUS: // what is this stuff
        case EQ_ALL_ARMOUR:
            return false;
        default:
            break;
        }
        // remaining should be armour only
        if (species.genus() == SP_OCTOPODE && eq != EQ_HELMET && eq != EQ_SHIELD)
            return true;

        if (is_draconian(species) && eq == EQ_BODY_ARMOUR)
            return true;

        // for everything else that is handled by mutations, including felid
        // restrictions, see item-use.cc::can_wear_armour. (TODO: move more of the
        // code here to mutations?)
        return false;
    }

    /**
     * Get ring slots available to a species.
     * @param species the species to check
     * @param missing_hand if true, removes a designated hand from the result
     */
    vector<equipment_type> ring_slots(mc_species species, bool missing_hand)
    {
        vector<equipment_type> result;

        const equipment_type missing = missing_hand
                            ? sacrificial_arm(species) : EQ_NONE;

        for (int i = EQ_FIRST_JEWELLERY; i <= EQ_LAST_JEWELLERY; i++)
        {
            const auto eq = static_cast<equipment_type>(i);
            if (eq != EQ_AMULET
                && eq != EQ_RING_AMULET
                && eq != missing
                && !bans_eq(species, eq))
            {
                result.push_back(eq);
            }
        }
        return result;
    }

    int get_exp_modifier(mc_species species)
    {
        return get_species_def(species.genus()).xp_mod;
    }

    int get_hp_modifier(mc_species species)
    {
        // n.b. this can be called when the player is not initialized
        if (species.is_monster() && you.monster_instance)
        {
            // the following calculation is aimed at getting hp comparable to
            // monster hp at an XL given by the monster's HD, with a +2 adjustment.
            // This seems to generally produce a good balance of progression and
            // absurdity.
            // Monsters corresponding to player species are generally
            // under-HP'd relative to what the player would be like,
            // so that is where the +2 adjustment comes from. There is a fair
            // amount of variation and +2 was selected by eyeballing a bunch of
            // cases. For e.g. Vashnia it produces identical results, and also for
            // the strongest deep elves like blademasters, but for others, it still
            // undershoots relative to the player species. Most of these creatures
            // will have special abilities, though.
            //
            // I considered two other options:
            // 1. starting with monster hp at xl 1. 1a: don't change hp with
            //    progression. This could be an interesting challenge mode, but
            //    I think it'll get boring. 1b: calculate the modifier from that
            //    hp. This gets amusing, but silly results in extreme cases. It
            //    could be fun, perhaps?
            // 2. getting to monster hp at xl 27. This is workable but I found that
            //    in general it produces lower hp than you would expect.
            // I also considered maxing the multiplier with the player species
            // multiplier, but didn't end up doing that so far.
            const int m_hp = 100 * you.monster_instance->max_hit_points;
            // we do this min so that orbs of fire can get max hp
            // if it's ever possible to drain the monster instance, or apply
            // ENCH_WRETCHED, this could affect player hp.
            const int m_hd = min(you.monster_instance->get_hit_dice(), 27);
            const int multiplier = (m_hp / (m_hd * 11 / 2 + 80) - 80) / 10;
            return multiplier;
        }
        return get_species_def(species).hp_mod;
    }

    int get_mp_modifier(mc_species species)
    {
        // TODO: what's a reasonable modifier for this for nonplayer species?
        // given that abilities are free, maybe not important?
        if (species.is_monster() && you.monster_instance)
        {
            return get_species_def(species).mp_mod
                + you.monster_instance->is_actual_spellcaster()
                + you.monster_instance->is_priest();
        }
        return get_species_def(species).mp_mod;
    }

    int get_wl_modifier(mc_species species)
    {
        if (species.is_monster() && you.monster_instance)
        {
            // TODO: check negative MR
            // Use either the player-species modifier, or the monster-derived
            // modifier, whichever is higher. This will give a substantial boost
            // to, for example, monster deep elf sorcerers. Note that magic immune
            // monsters start magic immune, but everything else develops
            // MR as it goes.
            return max(you.monster_instance->willpower() / 10,
                        get_species_def(species.genus()).wl_mod);
        }

        return get_species_def(species).wl_mod;
    }

    int get_stat_gain_multiplier(mc_species species)
    {
        // TODO: is this worth dataifying? Currently matters only for
        // player-stats.cc:attribute_increase
        return species.genus() == SP_DEMIGOD ? 4 : 1;
    }

    /**
     *  Does this species have (relatively) low strength?
     *  Used to generate the title for UC ghosts.
     *
     *  @param species the speciecs to check.
     *  @returns whether the starting str is lower than the starting dex.
     */
    bool has_low_str(mc_species species)
    {
        return get_species_def(species.genus()).d >= get_species_def(species.genus()).s;
    }


    bool recommends_job(species_type species, job_type job)
    {
        return find(get_species_def(species).recommended_jobs.begin(),
                    get_species_def(species).recommended_jobs.end(),
                    job) != get_species_def(species).recommended_jobs.end();
    }

    bool recommends_weapon(species_type species, weapon_type wpn)
    {
        const skill_type sk =
              wpn == WPN_THROWN  ? SK_THROWING :
              wpn == WPN_UNARMED ? SK_UNARMED_COMBAT :
                                   item_attack_skill(OBJ_WEAPONS, wpn);

        return find(get_species_def(species).recommended_weapons.begin(),
                    get_species_def(species).recommended_weapons.end(),
                    sk) != get_species_def(species).recommended_weapons.end();
    }

    bool is_valid(species_type species)
    {
        // n.b. this is distinct from mc_species::is_valid, which is more
        // restrictive (checks monster species).
        return 0 <= species && species < NUM_SPECIES;
    }


    // Ensure the species isn't SP_RANDOM/SP_VIABLE and it has recommended jobs
    // (old disabled species have none).
    bool is_starting_species(species_type species)
    {
        return is_valid(species)
            && !get_species_def(species).recommended_jobs.empty();
    }

    // A random valid (selectable on the new game screen) species.
    species_type random_starting_species()
    {
        const auto species = playable_species();
        return species[random2(species.size())];
    }

    bool is_removed(species_type species)
    {
    #if TAG_MAJOR_VERSION == 34
        if (species == SP_MOTTLED_DRACONIAN)
            return true;
    #endif
        // all other derived Dr are ok and don't have recommended jobs
        if (is_draconian(species))
            return false;
        if (get_species_def(species).recommended_jobs.empty())
            return true;
        return false;
    }


    /** All non-removed species, including base and derived species */
    vector<species_type> get_all_species()
    {
        vector<species_type> species;
        for (int i = 0; i < NUM_SPECIES; ++i)
        {
            const auto sp = static_cast<species_type>(i);
            if (!is_removed(sp))
                species.push_back(sp);
        }
        return species;
    }
}

// returns a value in auts
int mons_energy_to_delay(monster &m, energy_use_type et)
{
    const int energy = m.action_energy(et);
    const int base_speed = m.speed;
    return energy * 100 / (base_speed * 10);
}

void give_basic_mutations(mc_species species)
{
    // Don't perma_mutate since that gives messages.
    for (const auto& lum : get_species_def(species).level_up_mutations)
        if (lum.xp_level == 1)
            you.mutation[lum.mut] = you.innate_mutation[lum.mut] = lum.mut_level;

    if (species.is_monster() && you.monster_instance)
    {
        // use mod_imut if you only want to override for cases where a player
        // species has not contributed.
#define set_imut(m, v) you.mutation[m] = you.innate_mutation[m] = (v)
#define mod_imut(m, v) if (you.mutation[m] == 0) set_imut(m, v)

        if (mons_class_itemuse(species) < MONUSE_STARTING_EQUIPMENT)
        {
            // use the felid muts for this
            // TODO: right now no weapons entails no throwing, should this always
            // be so? Some weird monsters seem like they could probably throw,
            // even though they don't.
            set_imut(MUT_NO_ARMOUR, 1);
            set_imut(MUT_NO_GRASPING, 1);
        }

        // let this override base, e.g. for DE variants
        if (you.monster_instance->can_see_invisible())
            set_imut(MUT_ACUTE_VISION, 1);

        // TODO: how to handle Ds variants?
        if (mons_class_flag(you.species, M_COLD_BLOOD))
            set_imut(MUT_COLD_BLOODED, 1);
        if (mons_class_flag(you.species, M_FAST_REGEN))
            set_imut(MUT_REGENERATION, 1);
        if (mons_class_flag(you.species, M_NO_REGEN))
            set_imut(MUT_NO_REGENERATION, 1);
        if (mons_class_flag(you.species, M_SPINY))
            set_imut(MUT_SPINY, 1);

        int resist = you.monster_instance->res_negative_energy(true);
        // vulnerabilities?
        if (resist > 0)
            mod_imut(MUT_NEGATIVE_ENERGY_RESISTANCE, resist);
        set_imut(MUT_TORMENT_RESISTANCE, you.monster_instance->res_torment());
        // TODO: resists: tornado, constrict
        set_imut(MUT_PETRIFICATION_RESISTANCE,
                                         you.monster_instance->res_petrify());
        set_imut(MUT_ACID_RESISTANCE, you.monster_instance->res_acid());
        set_imut(MUT_SHOCK_RESISTANCE, you.monster_instance->res_elec());
        // TODO: immunity for these where appropriate?
        resist = you.monster_instance->res_fire();
        if (resist < 0)
            set_imut(MUT_HEAT_VULNERABILITY, -resist);
        else
            mod_imut(MUT_HEAT_RESISTANCE, resist);
        resist = you.monster_instance->res_cold();
        if (resist < 0)
            set_imut(MUT_COLD_VULNERABILITY, -resist);
        else
            mod_imut(MUT_COLD_RESISTANCE, resist);
        // TODO: poison vulnerability
        resist = you.monster_instance->res_poison();
        // negative poison resistance is handled as a fakemut
        if (resist > 0)
            set_imut(MUT_POISON_RESISTANCE, resist);

        // Would it be better to handle these as a fakemut?
        const int base_speed = mons_energy_to_delay(*you.monster_instance,
                                                                    EUT_MOVE);
        if (base_speed < 10)
        {
            ASSERT(base_speed > 0);
            // convert monster speed into levels of the fast mutation. This is
            // a bit coarser than monsters, and requires more levels than 3.
            // For example, a speed 30 bat ends up with a level 8 mut, leading
            // to a move delay of 3aut. It is least accurate for slightly fast
            // monsters: e.g. an ugly thing ends up with an 8aut move delay
            // instead of a 9.
            set_imut(MUT_FAST, 10 - base_speed);
        }
        else if (base_speed > 10)
        {
            // do something a little simpler and hacky for speed. There aren't
            // a lot of slow monsters, and the slow mutation is multiplicative
            // unlike monster energy calcs; this mostly gets the
            // correspondences about right except for very slightly slow
            // monsters
            set_imut(MUT_SLOW, (base_speed + 1) / 2 - 5);
              // base_speed >= 18 ? 4 // basically just gastronok
              //                  : base_speed >= 16 ? 3
              //                  : base_speed >= 14 ? 2
              //                  : 1);
        }
#undef set_imut
#undef mod_imut
    }
}

void give_level_mutations(mc_species species, int xp_level)
{
    for (const auto& lum : get_species_def(species).level_up_mutations)
        if (lum.xp_level == xp_level)
        {
            perma_mutate(lum.mut, lum.mut_level,
                         species::name(species.genus()) + " growth");
        }
}

void species_stat_init(mc_species species)
{
    if (species.is_monster() && you.monster_instance)
    {
        //non-player genus
        if (mons_intel(*you.monster_instance) == I_BRAINLESS)
            you.base_stats[STAT_INT] = -1; // lowest possible value
        else if (mons_class_itemuse(you.species) < MONUSE_OPEN_DOORS)
        {
            // example: worker ant
            you.base_stats[STAT_INT] = 1;
        }
        else if (mons_class_itemuse(you.species) == MONUSE_OPEN_DOORS
            || mons_intel(*you.monster_instance) == I_ANIMAL)
        {
            // example: howler monkey
            you.base_stats[STAT_INT] = 3;
        }
        else
            you.base_stats[STAT_INT] = 8; // TODO: boosts in some cases? casters?
    }
    else
        you.base_stats[STAT_INT] = get_species_def(species).i;

    // TODO: how to decide on str/dex for monsters?
    // heuristic: larger animals should be strong
    // more?
    you.base_stats[STAT_STR] = get_species_def(species).s;
    you.base_stats[STAT_DEX] = get_species_def(species).d;
}

void species_stat_gain(mc_species species)
{
    const species_def& sd = get_species_def(species);
    if (sd.level_stats.size() > 0 && you.experience_level % sd.how_often == 0)
    {
        modify_stat(*random_iterator(sd.level_stats),
                        species::get_stat_gain_multiplier(species), false);
    }
}

static void _swap_equip(equipment_type a, equipment_type b)
{
    swap(you.equip[a], you.equip[b]);
    bool tmp = you.melded[a];
    you.melded.set(a, you.melded[b]);
    you.melded.set(b, tmp);
}

species_type mons_species_to_player_species(monster_type mons)
{
    // TODO: need to revisit this
    // salamanders are genus naga, but they shouldn't be turned into naga
    // players.
    monster_type genus = mons_species(mons) == MONS_SALAMANDER
                                        ? MONS_SALAMANDER : mons_genus(mons);
    if (genus == MONS_DRACONIAN)
        genus = mons_species(mons);
    // high elf is early in the enum, so we have to hardcode some cases here.
    // Everything that is genus Elf, with the exception of MONS_ELF itself
    // corresponds to deep elf. High elf apts are still kicking around,
    // so why not make MONS_ELF use them.
    if (mons == MONS_ELF)
        return SP_HIGH_ELF;
    else if (genus == MONS_ELF)
        return SP_DEEP_ELF;
    for (int i = SP_HUMAN; i < NUM_SPECIES; i++)
    {
        auto s = static_cast<species_type>(i);
        if (get_species_def_raw(s).monster_species == genus)
            return s;
    }
    return SP_MONSTER;
}

/// Change monster species to mt, saving the current one in base_monster_instance
void specialize_species_to(monster_type mt)
{
    ASSERT(you.species.is_monster());
    you.base_monster_instance = you.monster_instance;
    // TODO: good or bad idea to use change_species_to? duplicate code with
    // setup code?
    change_species_to(mc_species(mt));
}

/// restore monster species from base_monster_instance
void despecialize_species()
{
    ASSERT(you.base_monster_instance);
    change_species_to(mc_species(you.base_monster_instance->type),
        you.base_monster_instance);
    you.base_monster_instance.reset();
}

/**
 * Change the player's species to something else.
 *
 * This is used primarily in wizmode, but is also used for extreme
 * cases of save compatibility (see `files.cc:_convert_obsolete_species`).
 * This does *not* check for obsoleteness -- as long as it's in
 * species_data it'll do something.
 *
 * @param sp the new species.
 */
void change_species_to(mc_species sp, shared_ptr<monster> minstance)
{
    ASSERT(sp.is_valid());
    ASSERT(sp == SP_MONSTER || !minstance);

    // TODO: kind of ugly
    const bool temp_species = sp == MONS_BAI_SUZHEN_DRAGON
                && you.base_monster_instance
                && you.base_monster_instance->type == MONS_BAI_SUZHEN
            || sp == MONS_BAI_SUZHEN && minstance
                && minstance->type == MONS_BAI_SUZHEN
            || minstance && minstance->is_shapeshifter();

    // Monster apts get recalculated on species change, so we need to cache
    // the old ones for a bit
    map<skill_type, float> old_apt_factors;
    // for temp species, just don't change apts. (TODO: does this work in
    // general?)
    if (!temp_species)
        for (skill_type sk = SK_FIRST_SKILL; sk < NUM_SKILLS; ++sk)
            old_apt_factors[sk] = species_apt_factor(sk);

    mc_species old_sp = you.species;
    you.species = sp;
    if (minstance)
        you.monster_instance = minstance;
    else if (you.species.is_monster())
        setup_monster_player(false); // reinit you.monster_instance
    else
        you.monster_instance.reset();
    you.chr_species_name = species::name(sp);

    // Re-scale skill-points. This has to be done here for monsters because
    // the monster instance has to be correctly set up

    if (!temp_species)
        for (skill_type sk = SK_FIRST_SKILL; sk < NUM_SKILLS; ++sk)
        {
            you.skill_points[sk] = you.skill_points[sk] * species_apt_factor(sk)
                                / old_apt_factors[sk];
        }

    // TODO: how to count these for bai suzhen?
    // reset monster-specific abilities; these will just produce weird
    // results across monsters -- there's no way to retrieve the correct
    // name after a species change
    for (int i = ABIL_MONSTER_SPECIES_1; i <= ABIL_MONSTER_SPECIES_10; i++)
    {
        const pair<caction_type, int> abilcount(CACT_ABIL, caction_compound(i));
        if (you.action_count.count(abilcount))
            you.action_count.erase(abilcount);
    }

    // Change permanent mutations, but preserve non-permanent ones.
    uint8_t prev_muts[NUM_MUTATIONS];

    // remove all innate mutations
    for (int i = 0; i < NUM_MUTATIONS; ++i)
    {
        if (you.has_innate_mutation(static_cast<mutation_type>(i)))
        {
            you.mutation[i] -= you.innate_mutation[i];
            you.innate_mutation[i] = 0;
        }
        prev_muts[i] = you.mutation[i];
    }
    // add the appropriate innate mutations for the new species and xl
    give_basic_mutations(sp);
    for (int i = 2; i <= you.experience_level; ++i)
        give_level_mutations(sp, i);

    for (int i = 0; i < NUM_MUTATIONS; ++i)
    {
        // TODO: why do previous non-innate mutations override innate ones?  Shouldn't this be the other way around?
        if (prev_muts[i] > you.innate_mutation[i])
            you.innate_mutation[i] = 0;
        else
            you.innate_mutation[i] -= prev_muts[i];
    }

    if (sp == SP_DEMONSPAWN)
    {
        roll_demonspawn_mutations();
        for (int i = 0; i < int(you.demonic_traits.size()); ++i)
        {
            mutation_type m = you.demonic_traits[i].mutation;

            if (you.demonic_traits[i].level_gained > you.experience_level)
                continue;

            ++you.mutation[m];
            ++you.innate_mutation[m];
        }
    }

    update_vision_range(); // for Ba, and for Ko

    // XX not general if there are ever any other options
    // XX monster species
    if ((old_sp == SP_OCTOPODE) != (sp == SP_OCTOPODE))
    {
        _swap_equip(EQ_LEFT_RING, EQ_RING_ONE);
        _swap_equip(EQ_RIGHT_RING, EQ_RING_TWO);
        // All species allow exactly one amulet.
    }

    // FIXME: this checks only for valid slots, not for suitability of the
    // item in question. This is enough to make assertions happy, though.

    // Bai Suzhen in dragon form melds equipment, not drops it. (Does this
    // trip any asserts? This should only happen in combination with
    // dragonform, so the melding code runs too.)
    // TODO: does this handle other specialization cases ok?
    if (!temp_species)
        for (int i = EQ_FIRST_EQUIP; i < NUM_EQUIP; ++i)
            if (you_can_wear(static_cast<equipment_type>(i)) == MB_FALSE
                && you.equip[i] != -1)
            {
                mprf("%s fall%s away.",
                     you.inv[you.equip[i]].name(DESC_YOUR).c_str(),
                     you.inv[you.equip[i]].quantity > 1 ? "" : "s");
                // Unwear items without the usual processing.
                you.equip[i] = -1;
                you.melded.set(i, false);
            }

    // Sanitize skills.
    fixup_skills();

    calc_hp(true, false);
    calc_mp(true);

    // The player symbol depends on species.
    update_player_symbol();
#ifdef USE_TILE
    init_player_doll();
#endif
    redraw_screen();
    update_screen();
}

#include "AppHdr.h"

#include "ability.h"
#include "database.h"
#include "describe.h"
#include "dungeon.h"
#include "items.h"
#include "mapdef.h"
#include "mon-death.h"
#include "monster.h"
#include "mpr.h"
#include "ng-setup.h"
#include "player.h"
#include "skills.h"
#include "species.h"
#include "species-monster.h"
#include "stringutil.h"

static bool _skill_needs_usable_hands(skill_type sk)
{
    switch (sk)
    {
    case SK_SHORT_BLADES:
    case SK_LONG_BLADES:
    case SK_AXES:
    case SK_MACES_FLAILS:
    case SK_POLEARMS:
    case SK_STAVES:
    case SK_SLINGS:
    case SK_BOWS:
    case SK_CROSSBOWS:
    case SK_THROWING:
        // TODO: evocations? felid vs others?
        return true;
    default:
        return false;
    }
}

// TODO: move code from describe_mutations into this file?

namespace species
{
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

    void setup_monster_player(bool game_start)
    {
        // XX function would be cleaner if it is part of mc_species or player?

        rng::generator gameplay(rng::GAMEPLAY);

        ASSERT(you.species.mon_species < NUM_MONSTERS);
        dprf("mon_species is %d", (int) you.species.mon_species);
        mons_spec spec = mons_spec(you.species);
        spec.attitude = ATT_FRIENDLY;

        // hacky, but it's hard to get around all the code that has to work this
        // way: create a real monster, place it, and make a copy.
        // TODO: packs?
        if (you.unique_creatures[you.species.mon_species])
            you.unique_creatures.set(you.species.mon_species, false);
        monster *tmp_mons = dgn_place_monster(spec, coord_def(0,0), true, true, false);
        ASSERT(tmp_mons);

        // these get their name from a base monster, use the player
        if (you.species == MONS_BLOCK_OF_ICE || you.species == MONS_PILLAR_OF_SALT)
            tmp_mons->base_monster = MONS_PLAYER;

        // some inherent enchantments for summoned monsters that are normally
        // handled when casting a spell, not on monster creation
        if (you.species == MONS_BALL_LIGHTNING
            || you.species == MONS_BALLISTOMYCETE_SPORE
            || you.species == MONS_FOXFIRE)
        {
            tmp_mons->add_ench(ENCH_SHORT_LIVED);
        }
        else if (you.species == MONS_BRIAR_PATCH)
        {
            // not sure it matters but this has a custom duration
            tmp_mons->add_ench(
                mon_enchant(ENCH_SHORT_LIVED, 1, nullptr, 80 + random2(100)));
        }

        // player inv should be empty before this call.
        // first, we copy the items off the monster, and clear the monster inventory.
        vector<item_def> mon_items;
        for (mon_inv_iterator ii(*tmp_mons); ii; ++ii)
        {
            const auto slot = ii.slot();
            mon_items.emplace_back(*ii);
            // don't fully unlink the item
            destroy_item(*ii);
            tmp_mons->inv[slot] = NON_ITEM;
        }

        // then we set up monster instance. This needs to be in place before
        // giving the player any items from the monster. Uses the copy constructor.
        you.monster_instance = make_shared<monster>(*tmp_mons);
        // TODO: 0,0 is not a great position for this, but everything else triggers
        // crashes when you try to do anything substantive with the monster.
        tmp_mons->flags |= MF_HARD_RESET; // prevents any items, corpses
        monster_die(*tmp_mons, KILL_DISMISSED, NON_MONSTER);

        if (game_start)
        {
            // Now move everything to the inventory.
            for (auto &i : mon_items)
                move_item_to_inv(i);

            // Finally, do the usual newgame stuff of wielding any weapons this has
            // provided to the player.
            // set up some skill levels based on these items
            // TODO: magic skills? does it matter?
            set<skill_type> item_sks;
            int fighting = -1;
            bool found_armour = false;
            for (int slot = 0; slot < ENDOFPACK; ++slot)
            {
                item_def& item = you.inv[slot];
                if (item.defined())
                {
                    newgame_setup_item(item, slot);
                    item_skills(item, item_sks);

                    // armour not handled by item_skills except for shields?
                    if (item.base_type == OBJ_ARMOUR
                        && get_armour_slot(item) == EQ_BODY_ARMOUR)
                    {
                        if (property(item, PARM_AC) > 3) // med-heavy body armour
                            item_sks.insert(SK_ARMOUR);
                        else // robe, hide, leather
                            item_sks.insert(SK_DODGING);
                        found_armour = true;
                    }

                    if (is_weapon(item))
                        fighting++;
                }
            }

            for (auto sk : item_sks)
                you.skills[sk]++;

            if (mons_class_itemuse(you.species) < MONUSE_STARTING_EQUIPMENT)
            {
                fighting++;
                you.skills[SK_UNARMED_COMBAT] += 2;
            }
            // TODO: where is the extra 1 point coming from?
            if (fighting > 0)
                you.skills[SK_FIGHTING] += fighting;

            if (!found_armour)
            {
                you.skills[SK_DODGING]++;
                you.skills[SK_STEALTH]++;
            }

            // religion
            // conditioning this on is_priest is a little too quirky; for example
            // dissolution is a priest but TRJ is not.
            const god_type mons_god = you.monster_instance->deity();
            if (mons_god > GOD_NO_GOD && mons_god < NUM_GODS)
            {
                // TODO: nameless gods?? choose randomly? or maybe use monk behavior?
                // init gift timeout? lugonu in abyss?
                you.religion = you.monster_instance->deity();
                you.piety = 38; // zealot piety start

                // not sure this is necessary, but it is based on lugonu
                if (invo_skill(you.religion) != SK_NONE)
                    you.skills[invo_skill(you.religion)]++;
            }
            // unclear whether this matters or is a good idea:
            if (you.monster_instance->spells.size() > 0)
                you.skills[SK_SPELLCASTING]++;
        }

        if (you.species == MONS_BENNU || you.species == MONS_SPRIGGAN_RIDER)
            you.lives = 1;

        // start flying creatures in the air
        if (you.racial_permanent_flight())
            you.attribute[ATTR_PERM_FLIGHT] = 1;
        // TODO: unhandled flags: batty, unblindable, blood scent, submerges, no_skeleton, web sense

        // some spell special cases
        if (you.species == MONS_DRACONIAN_STORMCALLER)
        {
            // these start off worshipping qazlal, so let them use piety-dependent
            // upheaval. They still start with smite and summon drakes, so aren't
            // exactly hurting...
            erase_if(you.monster_instance->spells, [](const mon_spell_slot &t) {
                return t.spell == SPELL_UPHEAVAL;
            });
        }
        // aura of brilliance is at a unfortunate combo of buggy for players to
        // cast + extremely useless, so disable it for now
        erase_if(you.monster_instance->spells, [](const mon_spell_slot &t) {
            return t.spell == SPELL_AURA_OF_BRILLIANCE;
        });
    }

    map<skill_type,int> get_monster_apts()
    {
        ASSERT(you.species == SP_MONSTER && you.monster_instance);

        // TODO: mummy variants? naga warrior / nagaraja? ogre mage? salamanders?
        species_type genus = you.species.genus();
        const bool player_genus = genus != SP_MONSTER;
        if (genus == SP_BASE_DRACONIAN)
        {
            // for draconians, sub in their base type to get the right color
            // aptitudes (draconians with job-like specializations use
            // MONS_DRACONIAN as their genus, even though they have a color)
            auto base_drac = mons_species_to_player_species(
                        draco_or_demonspawn_subspecies(*you.monster_instance));
            if (base_drac != SP_MONSTER)
                genus = base_drac;
        }

        // if genus is SP_MONSTER, then this resets aptitudes to 0.
        dprf("Setting apts from genus %s", species::name(genus).c_str());
        map<skill_type,int> result = get_species_skill_apt(genus);

        // deep trolls get a flat boost relative to regular trolls
        // uniques get a flat boost over their base species
        // TODO: revisit
        if (mons_species(you.species) == MONS_DEEP_TROLL
            || mons_is_unique(you.species))
        {
            for (int sk = 0; sk < NUM_SKILLS; ++sk)
                if (result[static_cast<skill_type>(sk)] != UNUSABLE_SKILL)
                    result[static_cast<skill_type>(sk)] += 1;
        }

        if (you.monster_instance->is_priest())
            result[SK_INVOCATIONS] += 1;

        // --- fighting/weapon skills
        if (you.monster_instance->is_fighter() || you.monster_instance->is_archer())
            result[SK_FIGHTING] += player_genus ? 1 : 2;

        // mark weapon/armor skills unusable as necessary
        if (you_can_wear(EQ_WEAPON) == MB_FALSE)
        {
            for (int sk = 0; sk < NUM_SKILLS; ++sk)
                if (_skill_needs_usable_hands(static_cast<skill_type>(sk)))
                    result[static_cast<skill_type>(sk)] = UNUSABLE_SKILL;

            // non-wielding species get a boost to unarmed. Probably should
            // further differentiate?
            // maybe scale by default attack damage?
            if (!player_genus) // felids are already taken care of
                result[SK_UNARMED_COMBAT] += 1;
        }
        if (you_can_wear(EQ_BODY_ARMOUR) == MB_FALSE)
        {
            result[SK_ARMOUR] = UNUSABLE_SKILL;
            result[SK_SHIELDS] = UNUSABLE_SKILL;
        }

        auto holiness = you.monster_instance->holiness();

        // only give the archer boost to non-player species; but "master archers"
        // uniformly get a boost
        if (!player_genus && you.monster_instance->is_archer()
            || mons_class_flag(you.species, M_PREFER_RANGED))
        {
            ASSERT(you_can_wear(EQ_WEAPON) != MB_FALSE);
            // not very nuanced, might want to handle differently depending on
            // starting ammo?
            result[SK_BOWS] += 1;
            result[SK_CROSSBOWS] += 1;
            result[SK_THROWING] += 1;
            result[SK_SLINGS] += 1;
        }

        // the draconian principle: resistance == affinity
        // TODO: maybe take monster spells into account?
        // These are applied to player species as well, so that e.g. hell knights
        // get a fire magic boost.

        if (holiness & (MH_EVIL | MH_DEMONIC | MH_UNDEAD))
            result[SK_NECROMANCY] += 1;
        else if (holiness & MH_HOLY) // or maybe they *do* have a necro affinity, and just choose not to use it?
            result[SK_NECROMANCY] -= 2;
        else if (holiness & MH_NATURAL)
            result[SK_NECROMANCY] -= 1;

        // tweak some elemental skills over and above current base levels, but
        // don't give a boost if there is already one (e.g. skipping player
        // genus species that already have some boost)
        if (you.monster_instance->res_negative_energy(true)
                                && result[SK_NECROMANCY] <= 0)
        {
           result[SK_NECROMANCY]++;
        }
        if (you.monster_instance->res_fire() && result[SK_FIRE_MAGIC] <= 0)
            result[SK_FIRE_MAGIC]++;

        if (you.monster_instance->res_cold() && result[SK_ICE_MAGIC] <= 0)
            result[SK_ICE_MAGIC]++;

        if (you.monster_instance->res_poison() && result[SK_POISON_MAGIC] <= 0)
            result[SK_POISON_MAGIC]++;

        if (you.monster_instance->res_elec() && result[SK_AIR_MAGIC] <= 0)
            result[SK_AIR_MAGIC]++;

        if (holiness & MH_PLANT)
            result[SK_POISON_MAGIC] += 1;

        if (!player_genus)
        {
            // stuff to skip for species genus characters

            if (you.monster_instance->is_actual_spellcaster())
                result[SK_SPELLCASTING] += 1;

            result[SK_ICE_MAGIC] -= you.get_mutation_level(MUT_COLD_BLOODED);

            switch (you.monster_instance->body_size())
            {
                case SIZE_TINY:
                    result[SK_STEALTH] += 3; break;
                case SIZE_LITTLE:
                    result[SK_STEALTH] += 2; break;
                case SIZE_SMALL:
                    result[SK_STEALTH] += 1; break;
                case SIZE_LARGE:
                    result[SK_STEALTH] -= 1; break;
                case SIZE_BIG:
                    result[SK_STEALTH] -= 2; break;
                case SIZE_GIANT:
                    result[SK_STEALTH] -= 3; break;
                default: break;
            }
            if (holiness & (MH_UNDEAD | MH_NATURAL))
                result[SK_STEALTH] += 1; // doesn't override mummies
            else if (holiness & MH_NONLIVING)
                result[SK_STEALTH] -= 1;
        }

        if (   you.species == MONS_SHADOW
            || you.species == MONS_SHADOW_WRAITH
            || you.species == MONS_SHADOW_DRAGON) // this leaves them at +3
        {
            result[SK_STEALTH] += 5;
        }
        else if (mons_genus(you.species) == MONS_SHADOW_IMP)
            result[SK_STEALTH] += 2;
        else if (you.monster_instance->is_insubstantial())
            result[SK_STEALTH] += 1;

        // TODO: what other tweaks to make? genus modifiers?
        return result;
    }

    void give_basic_monster_mutations(mc_species species)
    {
        ASSERT(species.is_monster() && you.monster_instance);

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
        if (base_speed > 0 && base_speed < 10)
        {
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

    const vector<string> monster_fake_mutations(mc_species species, bool terse)
    {
        ASSERT(species.is_monster() && you.monster_instance);
        vector<string> result;

        if (mons_intel(*you.monster_instance) <= I_ANIMAL
            // specific monsters that this is not flavorful on.
            && species != MONS_ROYAL_JELLY // brainless
            && mons_genus(species) != MONS_DRAGON) // animal int
        {
            // just some lore to explain why they can do things like open doors
            // and maybe read
            result.push_back(terse ?
                "uplifted" :
                "You are unnaturally intelligent for one of your kind.");
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

        // move to describe_mutations?
        if (!you.can_mutate())
            result.push_back(terse ? "genetically immutable" : "You cannot be mutated.");

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

    void monster_stat_init(mc_species species)
    {
        ASSERT(species.is_monster() && you.monster_instance);

        if (species.genus() != SP_MONSTER)
            species_stat_init(species.genus()); // recursion alert
        else
        {
            // construct base values for non-player-genus species

            // base int derived from monster intelligence
            if (mons_intel(*you.monster_instance) == I_BRAINLESS)
                you.base_stats[STAT_INT] = -1; // lowest possible value
            else if (mons_class_itemuse(you.species) == MONUSE_OPEN_DOORS
                || mons_intel(*you.monster_instance) == I_ANIMAL)
            {
                you.base_stats[STAT_INT] = 3;

                // monster-only modifiers
                if (mons_class_itemuse(you.species) > MONUSE_NOTHING)
                    you.base_stats[STAT_INT] += 1; // example: howler monkey

                switch (mons_genus(you.species))
                {
                // a few things that seem like they should be smarter
                case MONS_HOUND:
                case MONS_HOG:
                case MONS_ELEPHANT:
                case MONS_BEAR:
                case MONS_DRAGON:
                    you.base_stats[STAT_INT] += 1;
                    break;
                // and a few things that seem like they should be dumber
                case MONS_SCORPION:
                case MONS_SPIDER:
                case MONS_WORM:
                case MONS_ELEPHANT_SLUG:
                case MONS_KILLER_BEE:
                case MONS_VAMPIRE_MOSQUITO:
                case MONS_HORNET:
                case MONS_MOTH:
                case MONS_GIANT_COCKROACH:
                    you.base_stats[STAT_INT] -= 1;
                default:
                    break;
                }
            }
            else // I_HUMAN
            {
                you.base_stats[STAT_INT] = 8; // TODO: boosts in some more cases?
                if (mons_class_flag(you.species, M_SPEAKS))
                    you.base_stats[STAT_INT] += 2;
            }
            // strength/dex come primarily from size
            // TODO: is this any good?
            switch (you.monster_instance->body_size(PSIZE_TORSO))
            {
            case SIZE_TINY:
                you.base_stats[STAT_STR] = 1;
                you.base_stats[STAT_DEX] = 10;
                break;
            case SIZE_LITTLE:
                you.base_stats[STAT_STR] = 1;
                you.base_stats[STAT_DEX] = 8;
                break;
            case SIZE_SMALL:
                you.base_stats[STAT_STR] = 2;
                you.base_stats[STAT_DEX] = 6;
                break;
            case SIZE_MEDIUM:
                you.base_stats[STAT_STR] = 4;
                you.base_stats[STAT_DEX] = 4;
                break;
            case SIZE_LARGE:
                you.base_stats[STAT_STR] = 6;
                you.base_stats[STAT_DEX] = 2;
                break;
            case SIZE_BIG:
                you.base_stats[STAT_STR] = 8;
                you.base_stats[STAT_DEX] = 1;
                break;
            case SIZE_GIANT:
                you.base_stats[STAT_STR] = 10;
                you.base_stats[STAT_DEX] = 1;
                break;
            default:
                break;
            }
        }

        // modifiers that can apply to player genus monsters as well

        // not very fine-grained:
        if (you.monster_instance->spells.size() > 0)
            you.base_stats[STAT_INT] += 2;

        if (you.monster_instance->has_attack_flavour(AF_TRAMPLE))
            you.base_stats[STAT_STR] += 2;

        // maybe remove, redundant with trample
        if (mons_class_flag(you.species, M_CRASH_DOORS))
            you.base_stats[STAT_STR] += 1;

        if (you.species == MONS_DEEP_ELF_BLADEMASTER)
            you.base_stats[STAT_DEX] += 3; // blademasters get dex, not str
        else if (mons_class_flag(you.species, M_TWO_WEAPONS))
            you.base_stats[STAT_STR] += 2;

        // does this actually make sense?
        if (mons_class_flag(you.species, M_BATTY))
            you.base_stats[STAT_DEX] += 2;
        else if (mons_class_flag(you.species, M_FLIES))
            you.base_stats[STAT_DEX] += 1;

        // would it make sense to use speed and or ev to impact dex?

        // monsters whose flavor text implies bonus strength/dex:
        if (you.species == MONS_WAR_GARGOYLE)
        {
            // large boost because regular gargoyle is pretty mediocre at the
            // moment
            you.base_stats[STAT_STR] += 4;
            you.base_stats[STAT_DEX] += 4;
        }
        if (you.species == MONS_ETTIN || you.species == MONS_LINDWURM)
            you.base_stats[STAT_STR] += 3;

        if (mons_genus(you.species) == MONS_SNAKE
            || you.species == MONS_WOLF)
        {
            you.base_stats[STAT_DEX] += 2;
        }

        if (you.species == MONS_SONJA)
            you.base_stats[STAT_DEX] += 3;

        // and the reverse:
        if (you.species == MONS_SALAMANDER_MYSTIC)
        {
            you.base_stats[STAT_STR] -= 1;
            you.base_stats[STAT_DEX] -= 1;
        }
    }
}

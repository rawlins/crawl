#include "AppHdr.h"

#include "monster.h"
#include "mpr.h"
#include "player.h"
#include "skills.h"
#include "species.h"
#include "species-monster.h"

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

namespace species
{
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
}

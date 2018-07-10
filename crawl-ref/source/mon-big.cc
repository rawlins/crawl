/**
 * @file
 * @brief Code for big monsters.
**/

#include "AppHdr.h"

#include "mon-big.h"
#include "mon-tentacle.h"

#include <functional>

#include "act-iter.h"
#include "coordit.h"
#include "delay.h"
#include "env.h"
#include "fprop.h"
#include "ghost.h"
#include "libutil.h" // map_find
#include "losglobal.h"
#include "mgen-data.h"
#include "mon-death.h"
#include "mon-place.h"
#include "nearby-danger.h"
#include "showsymb.h"
#include "stringutil.h"
#include "terrain.h"
#include "view.h"

#define BIGMONS_KEY "big_monster_data"
#define BIGMONS_TYPE_KEY "big_monster_type"
#define BIGMONS_MONS_TYPE_KEY "big_monster_mons_type"
#define BIGMONS_PARTS_KEY "big_monster_parts"
#define BIGMONS_HEAD_KEY "big_monster_head"
#define BIGMONS_HEAD "head"
#define BIGMONS_PART "part"

#define LINEMONS_LENGTH_KEY "line_monster_length"

#define BIGMONS_UNKNOWN "unknown_monster"
#define BIGMONS_LINE_MONSTER "line_monster"

static void _sync_part(monster &mon, monster &part)
{
    //part.type               = mon.type;
    //part.base_monster       = mon.base_monster;
    part.experience         = 0;
    part.summoner           = mon.summoner;
    part.foe_memory         = mon.foe_memory;
    part.god                = mon.god;
    part.attitude           = mon.attitude;

    part.seen_context       = mon.seen_context; // TODO: seen flags?
    // TODO: flags, props??

    ghost_demon details;
    details.init_monster_part(mon);
    // max hp, hd, speed handled here
    part.set_ghost(details);
    part.ghost_demon_init();

    // after setting the ghost_demon, which forces maxhp
    part.hit_points         = mon.hit_points;
    const monster_info mi(&mon);
    // also needs to happen after ghost, which uses (??) an unsigned char for
    // colour.
    part.colour             = get_mons_colour(mi);
}

monster* mons_get_parent_monster(monster* mons)
{
    for (monster_iterator mi; mi; ++mi)
    {
        if (mi->is_parent_monster_of(mons))
            return *mi;
    }

    return nullptr;
}

bool monster::is_part() const
{
    return bigmon != nullptr;
}

shared_ptr<big_monster> monster::get_big_monster() const
{
    return bigmon;
}

bool monster::is_part_of(const big_monster *b) const
{
    return is_part() && b && *get_big_monster() == *b;
}

bool monster::is_part_of(const shared_ptr<big_monster> &b) const
{
    return is_part() && b != nullptr && *get_big_monster() == *b;
}

bool monster::is_head() const
{
    return is_part() && mid == get_big_monster()->get_head().mid;
}

bool big_monster::operator==(const big_monster &rhs) const
{
    return get_head().mid == rhs.get_head().mid;
}

string big_monster::describe() const
{
    string s;
    s += make_stringf("Part of big_monster type '%s'. Head at %d,%d is '%s', mid %d.\n",
                get_serialized_type().c_str(),
                get_head().pos().x, get_head().pos().y,
                get_head().name(DESC_A).c_str(), get_head().mid);
    if (parts.size() == 0)
        s += "No parts.\n";
    else
    {
        s += "Parts:\n";
        for (auto p : parts)
        {
            s += make_stringf("    part at %d,%d is '%s', mid %d.\n",
                p->pos().x, p->pos().y, p->name(DESC_A).c_str(), p->mid);
        }
    }
    return s;
}

// for debugging
/* static */ string big_monster::describe_bigmon_props(const monster &m)
{
    // n.b. this data is only synced on saving, so the props can get out of date
    if (!m.props.exists(BIGMONS_KEY))
        return "No big_monster props are set.\n";
    const CrawlHashTable &data = m.props[BIGMONS_KEY].get_table();
    string s = "big_monster props: ";

    const string bigmons_type = data.exists(BIGMONS_TYPE_KEY)
                    ? data[BIGMONS_TYPE_KEY].get_string()
                    : "MISSING";
    s += make_stringf("Member of big_monster of type '%s'.\n",
                                                    bigmons_type.c_str());
    const string mons_type = data.exists(BIGMONS_MONS_TYPE_KEY)
                    ? data[BIGMONS_MONS_TYPE_KEY].get_string()
                    : "MISSING";
    if (mons_type == BIGMONS_HEAD)
    {
        s += "    Monster is a head. Part mids:\n";
        if (!data.exists(BIGMONS_PARTS_KEY))
            s += "    MISSING\n";
        else
        {
            for (auto p_mid : data[BIGMONS_PARTS_KEY].get_vector())
                s += make_stringf("    %d\n", p_mid.get_int());
        }

    }
    else if (mons_type == BIGMONS_PART)
    {
        s += "    Monster is a part.\n";
        const int head_mid = data.exists(BIGMONS_HEAD_KEY)
                    ? data[BIGMONS_HEAD_KEY].get_int()
                    : MID_NOBODY;
        auto head = monster_by_mid(head_mid);
        if (!head)
            s += make_stringf("    Null head (mid %d).\n", head_mid);
        else if (invalid_monster(head))
            s += make_stringf("    Invalid head (mid %d).\n", head_mid);
        else
        {
            s += make_stringf("    Head is '%s' (mid %d).\n",
                head->name(DESC_A).c_str(), head_mid);
        }
    }
    else
        s += make_stringf("    Unknown monster type '%s'.\n", mons_type.c_str());
    return s;
}

void monster::set_big_monster(big_monster *b)
{
    bigmon = shared_ptr<big_monster>(b);
}

void monster::set_big_monster(shared_ptr<big_monster> b)
{
    bigmon = b;
}

static mgen_data _part_data(const monster& head, coord_def pos)
{
    mgen_data mg(MONS_PART, SAME_ATTITUDE((&head)), pos, head.foe, MG_FORCE_PLACE);
    if (mons_is_zombified(head))
        mg.base_type = head.type;
    mg.set_summoned(&head, 0, 0, head.god)
      .set_col(head.colour);
    return mg;
}

monster& big_monster::new_part_at(const coord_def& pos)
{
    // does not add to parts!
    mgen_data mg = _part_data(*head, pos);
    monster *part = create_monster(mg);
    ASSERT(part && !invalid_monster(part));
    _sync_part(get_head(), *part);

    // need to use the same shared_ptr
    part->set_big_monster(head->get_big_monster());
    return *part;
}

static bool _delete_part_at(const coord_def& pos)
{
    monster *m = monster_at(pos);
    if (!m || m->type != MONS_PART)
        return false;
    monster_die(*m, KILL_MISC, NON_MONSTER, true);
    return true;
}

big_monster::big_monster()
    : head(nullptr), parts()
{
}

big_monster::big_monster(monster &h)
    : head(&h), parts()
{
}

/**
 * Is any part of this big_monster located at pos?
 */
bool big_monster::located_at(const coord_def& pos) const
{
    if (head->pos() == pos)
        return true;
    for (auto m : parts)
        if (m->pos() == pos)
            return true;
    return false;
}

/**
 * Is mon a part of this big_monster?
 */
bool big_monster::part_of(const monster& mon) const
{
    if (mon.mid == head->mid)
        return true;
    for (auto m : parts)
        if (m->mid == mon.mid)
            return true;
    return false;
}

/**
 * Get a vector of the parts of this big_monster.
 *
 * @param include_head should the head be included?
 */
vector<monster *> big_monster::get_parts(bool include_head) const
{
    vector <monster *> result = parts;
    if (include_head)
        result.push_back(head); // TODO: order
    return result;
}

/**
 * Get the head of the big_monster.
 */
monster &big_monster::get_head() const
{
    return *head;
}

/**
 * Provides a 'reference monster' that should be used to determine the
 * behavior of `part`.  Could be `part` itself, but also could be any other
 * arbitrary monster, such as the head.
 */
monster &big_monster::get_reference_monster(const monster &part) const
{
    return *head;
}

bool big_monster::is_reference_monster(const monster &part) const
{
    return part.mid == get_reference_monster(part).mid;
}

/**
 * Destroy all parts safely.
 */
void big_monster::destroy_parts()
{
    for (auto m : parts)
        if (m->alive()) // may already have been killed
            monster_die(*m, KILL_MISC, NON_MONSTER, true);
    parts.clear();
}

/**
 * Do any necessary adjustments to a glyph before displaying it. This is called
 * after the glyph is already set up, probably based on the reference monster.
 *
 * @param g an (already-populated) glyph
 * @param part the part of the monster being displayed
 * @param part_info the monster_info used to initially setup `g`
 */
void big_monster::adjust_part_glyph(cglyph_t &g, const monster *part,
                                                const monster_info *part_info)
{
    // re-override any glyph changes inherited from the head
    if (!part->is_head() && part->type == MONS_PART)
        g.ch = mons_char(MONS_PART);
}

/**
 * A line_monster is a flexible line-shaped monster that can be as long as the
 * provided length. It has a snake-like movement, where each part moves through
 * the space of the part in front of it, except that it can move through itself
 * in various ways. It is head-focused, and implements damage sharing among
 * parts, with the hp (and everything else) determined by the head.
 */
line_monster::line_monster(monster &mon_head, int length)
        : big_monster(mon_head), max_length(length)
{
}

/* static */ bool big_monster::stores_big_monster(const monster &mons)
{
    if (!mons.props.exists(BIGMONS_KEY))
        return false;
    const CrawlHashTable &data = mons.props[BIGMONS_KEY].get_table();

    // for now, everything is always stored on the head
    return data.exists(BIGMONS_MONS_TYPE_KEY)
            && data[BIGMONS_MONS_TYPE_KEY].get_string() == BIGMONS_HEAD;
}

/* static */ void big_monster::load_big_monster(monster &mons)
{
    dprf("loading big monster (mid %d)", mons.mid);
    CrawlHashTable &data = mons.props[BIGMONS_KEY].get_table();
    const string bigmons_type = data[BIGMONS_TYPE_KEY].get_string();
    shared_ptr<big_monster> b;
    if (bigmons_type == BIGMONS_LINE_MONSTER)
        b = make_shared<line_monster>(data);
    else
        die("Unknown big monster '%s'", bigmons_type.c_str());

    ASSERT(mons.mid == b->get_head().mid);
    mons.set_big_monster(b);
    for (auto part : b->get_parts())
        part->set_big_monster(mons.get_big_monster());
}

big_monster::big_monster(CrawlHashTable &data) : big_monster()
{
    // generic code for loading the head / parts

    // TODO: ASSERTs => error message eventually, to head off unloadable saves
    ASSERT(data[BIGMONS_MONS_TYPE_KEY].get_string() == BIGMONS_HEAD);

    const int head_mid = data[BIGMONS_HEAD_KEY].get_int();
    head = monster_by_mid(head_mid);
    ASSERT(head && !invalid_monster(head));

    const CrawlVector &part_vec = data[BIGMONS_PARTS_KEY].get_vector();

    for (auto part_mid_store : part_vec)
    {
        const int part_mid = part_mid_store.get_int();
        monster *p = monster_by_mid(part_mid);
        ASSERT(p && !invalid_monster(p));
        CrawlHashTable &part_data = p->props[BIGMONS_KEY].get_table();
        ASSERT(part_data[BIGMONS_TYPE_KEY].get_string() == BIGMONS_LINE_MONSTER);
        ASSERT(part_data[BIGMONS_MONS_TYPE_KEY].get_string() == BIGMONS_PART);
        ASSERT(part_data[BIGMONS_HEAD_KEY].get_int() == head_mid);
        parts.push_back(p);
    }

    // this does not set up the big_monster pointers in the actual monsters!!    
}

line_monster::line_monster(CrawlHashTable &data) : big_monster(data)
{
    // TODO: ASSERTs => error message eventually, to head off unloadable saves
    ASSERT(data[BIGMONS_TYPE_KEY].get_string() == BIGMONS_LINE_MONSTER);

    max_length = data[LINEMONS_LENGTH_KEY];

    for (auto part : parts)
        _sync_part(*head, *part);

    // this does not set up the big_monster pointers in the actual monsters!!
}

string big_monster::get_serialized_type() const
{
    return BIGMONS_UNKNOWN;
}

string line_monster::get_serialized_type() const
{
    return BIGMONS_LINE_MONSTER;
}

void big_monster::sync_monster_props() const
{
    if (!head->props.exists(BIGMONS_KEY))
        head->props[BIGMONS_KEY].new_table();
    CrawlHashTable &data = head->props[BIGMONS_KEY].get_table();
    data[BIGMONS_TYPE_KEY] = get_serialized_type();
    data[BIGMONS_MONS_TYPE_KEY] = BIGMONS_HEAD;
    // TODO: based on the tentacle code, this stores mid_t (which is uint32)
    // by casting it to int. This is hacky at best.
    data[BIGMONS_HEAD_KEY].get_int() = head->mid;
    
    // serialize the part mids
    if (!data.exists(BIGMONS_PARTS_KEY))
        data[BIGMONS_PARTS_KEY].new_vector(SV_INT);
    CrawlVector &part_vec = data[BIGMONS_PARTS_KEY].get_vector();
    part_vec.clear();
    for (auto part : parts)
        part_vec.push_back((int) part->mid);

    // serialize mappings to the head; not strictly necessary but will help
    // check for coherence.
    for (auto part : parts)
    {
        if (!part->props.exists(BIGMONS_KEY))
            part->props[BIGMONS_KEY].new_table();
        CrawlHashTable &part_data = part->props[BIGMONS_KEY].get_table();
        part_data[BIGMONS_TYPE_KEY] = get_serialized_type();
        part_data[BIGMONS_MONS_TYPE_KEY] = BIGMONS_PART;
        part_data[BIGMONS_HEAD_KEY].get_int() = head->mid;
    }
}

void line_monster::sync_monster_props() const
{
    big_monster::sync_monster_props();
    CrawlHashTable &data = head->props[BIGMONS_KEY].get_table();
    data[LINEMONS_LENGTH_KEY] = max_length;    
}

/**
 * Returns the tail of the line_monster.
 */
monster &line_monster::get_tail() const
{
    if (parts.size() == 0)
        return *head;
    else
        return *parts.back();
}

/**
 * Returns the current length of the line_monster.
 */
int line_monster::length() const
{
    return parts.size() + 1;
}

bool line_monster::head_fits_at(const coord_def& pos, bool actors) const
{
    if (!in_bounds(pos)) // allow 0,0?
        return false;
    const monster *m = monster_at(pos);
    // line_monsters can move into arbitrary parts of themselves.
    if (actors && m && m->is_part_of(this))
        return true;
    return ((!actors || !actor_at(pos)) && mons_can_traverse(get_head(), pos));
}

void line_monster::snake_movement_raw(const coord_def& pos)
{
    // snake behavior -- parts follow position in front of them.
    coord_def last = head->pos();
    int last_index = head->mindex();
    head->moveto(pos);
    mgrd(pos) = last_index;
    for (auto m : parts)
    {
        coord_def tmp_last = m->pos();
        m->moveto(last);
        last_index = m->mindex();
        mgrd(last) = last_index;
        last = tmp_last;
    }
    // if pos was the old tail position, the head will now be in `last`,
    // so don't overwrite the already-replaced mindex in mgrd.
    if (in_bounds(last) && mgrd(last) == last_index)
        mgrd(last) = NON_MONSTER;
}

bool line_monster::move_head_to_part(monster *part)
{
    // logic for moving head through parts of itself.
    ASSERT(part && part->is_part_of(this));
    if (part == &get_tail()) // length 1 or 2
    {
        if (length() <= 2)
            invert(); // noop on length 1
        else
            snake_movement_raw(part->pos()); // loop
    }
    else
    {
        // contract from head until head replaces part
        const coord_def part_pos = part->pos();
        while (head->pos() != part_pos)
            if (!contract(true))
                return false;
    }
    return true;
}

bool line_monster::move_head_to(const coord_def& pos)
{
    // TODO: nets, force param for move_to_pos (submerging?)
    dprf("moving head to %d,%d", pos.x, pos.y);
    if (!head_fits_at(pos) || !monster_habitable_grid(head->type, grd(pos)))
        return false;
    monster *m = monster_at(pos);
    if (m && m->is_part_of(this))
        return move_head_to_part(m);
    if (!adjacent(pos, head->pos()))
    {
        // more than one step away.
        // simplest solution for the first pass -- just shrink the monster
        // down to one space. The case where the target pos is a part of this
        // monster is handled differently, in move_head_to_part.
        contract_all();
        ASSERT(length() == 1);
        const coord_def old_pos = head->pos();
        head->moveto(pos);
        mgrd(pos) = head->mindex();
        if (in_bounds(old_pos) && mgrd(old_pos) == head->mindex())
            mgrd(old_pos) = NON_MONSTER;
    }
    else
    {
        // do this first so the expanded piece obeys snake movement. TODO:
        // requires space at tail (will just silently fail if there is none.)
        if (length() < max_length)
            expand();
        snake_movement_raw(pos);
    }
    return true;
}

bool line_monster::on_part_died(monster *part, killer_type killer,
                      int killer_index, bool silent)
{
    // shift the death to the head, which will eventually clean up this part
    monster_die(*head, killer, killer_index, silent);

    // no line_monster part ever counts
    return false;
}

void line_monster::on_head_died(killer_type killer, int killer_index,
                                                                bool silent)
{
    destroy_parts();
}

bool line_monster::on_part_damage(monster *part, const actor *oppressor,
                    int damage, beam_type flavour, kill_method_type kill_type)
{
    get_head().hurt(oppressor, damage, flavour, kill_type);
    // the above will kill this part if necessary, so no cleanup is required.
    return false;
}

void line_monster::on_head_damage(const actor *oppressor,
                    int damage, beam_type flavour, kill_method_type kill_type)
{
    // don't actually kill the parts here -- if the head dies, they'll be
    // taken care of.
    for (auto m : parts)
        m->hit_points = max(1, get_head().hit_points);
}

/**
 * Add a new part to the tail of the line_monster.
 */
bool line_monster::expand()
{
    if (length() >= max_length)
        return false;
    const coord_def tail_pos = get_tail().pos();

    vector<coord_def> adj_squares;

    // Collect open adjacent squares. Candidate squares must be
    // unoccupied.
    for (adjacent_iterator adj_it(tail_pos); adj_it; ++adj_it)
    {
        if (monster_habitable_grid(head->type, grd(*adj_it))
            && !actor_at(*adj_it))
        {
            adj_squares.push_back(*adj_it);
        }
    }
    if (adj_squares.size() == 0)
        return false;
    // TODO: find direction of rest of body?
    const coord_def new_spot = adj_squares[random2(adj_squares.size())];
    monster& new_part = new_part_at(new_spot);
    parts.push_back(&new_part);
    return true;
}

/**
 * Shorten the line monster by 1.
 *
 * @param from_head whether to contract from the head or the tail.
 */
bool line_monster::contract(bool from_head)
{
    if (length() == 1)
        return false;
    if (from_head)
    {
        const coord_def new_head_pos = parts[0]->pos();
        const coord_def old_pos = head->pos();
        // TODO: deque? this is probably O(n) on a vector
        // maybe parent class should just maintain a set, with child
        // responsible for any orderings at all?
        parts.erase(parts.begin());
        _delete_part_at(new_head_pos);
        head->moveto(new_head_pos);
        mgrd(new_head_pos) = head->mindex();
        if (in_bounds(old_pos) && mgrd(old_pos) == head->mindex())
            mgrd(old_pos) = NON_MONSTER;
    }
    else
    {
        const coord_def tail_pos = get_tail().pos();
        parts.pop_back();
        _delete_part_at(tail_pos);
    }
    return true;
}

bool line_monster::contract_all()
{
    // TODO: just use destroy?
    while (length() > 1)
        if (!contract(false))
            return false;
    return true;
}

/**
 * Turn the monster around, leaving the positions intact.
 */
void line_monster::invert()
{
    vector<coord_def> new_positions;

    // save the current positions
    for (auto m : get_parts())
        new_positions.push_back(m->pos());
    for (auto m : get_parts())
    {
        // consume the saved positions in reversed order
        m->moveto(new_positions.back());
        mgrd(new_positions.back()) = m->mindex();
        new_positions.pop_back();
    }
}




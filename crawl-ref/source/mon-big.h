/**
 * @file
 * @brief Big monster-related code
**/

#pragma once

#include "beam-type.h"
#include "ouch.h" // kill_method_type

/**
 * This class implements a general API for monsters that take up more than one
 * space.
 */
class big_monster
{
public:
    big_monster();
    big_monster(monster &head);
    bool operator==(const big_monster &rhs) const;

    virtual string describe() const;
    static string describe_bigmon_props(const monster &m);

    // serialization
    big_monster(CrawlHashTable &data);
    virtual string get_serialized_type() const;
    virtual void sync_monster_props() const = 0;
    static bool stores_big_monster(const monster &mons);
    static void load_big_monster(monster &mons);

    /**
     * can the head be moved to pos?
     *
     * @param pos the position to consider
     * @param actors whether to care about other actors at pos.
     */
    virtual bool head_fits_at(const coord_def& pos, bool actors=true) const = 0;

    /**
     * Move the head to pos, along with any other consequences.
     */
    virtual bool move_head_to(const coord_def& pos) = 0;

    /**
     * Do any cleanup necessary when a part does, not including the head.
     */
    virtual bool on_part_died(monster *part, killer_type killer,
                        int killer_index, bool silent) = 0;

    /**
     * Do any cleanup necessary when the head dies.
     */
    virtual void on_head_died(killer_type killer, int killer_index,
                        bool silent) = 0;

    /**
     * Do any necessary reactions to damage to a part.
     */
    virtual bool on_part_damage(monster *part, const actor *oppressor,
                                            int damage, beam_type flavour,
                                            kill_method_type kill_type) = 0;

    /**
     * Do any necessary reactions to damage to the head.
     */
    virtual void on_head_damage(const actor *oppressor,
                                            int damage, beam_type flavour,
                                            kill_method_type kill_type) = 0;

    bool located_at(const coord_def& pos) const;
    bool part_of(const monster& mon) const;
    vector<monster *> get_parts(bool include_head = true) const;
    monster& get_reference_monster(const monster &part) const;
    bool is_reference_monster(const monster &part) const;
    monster& get_head() const;
    void destroy_parts();

    void adjust_part_glyph(cglyph_t &g, const monster *part,
                                                const monster_info *part_info);

protected:
    monster& new_part_at(const coord_def& pos);

    monster *head;
    vector<monster *> parts;
};

monster *mons_get_parent_monster(monster* mons);

class line_monster : public big_monster
{
public:
    line_monster(monster &mon_head, int length);
    line_monster(CrawlHashTable &data);

    string get_serialized_type() const override;
    void sync_monster_props() const override;

    bool head_fits_at(const coord_def& pos, bool actors=true) const override;
    bool move_head_to(const coord_def& pos) override;
    bool on_part_died(monster *part, killer_type killer,
                      int killer_index, bool silent) override;
    void on_head_died(killer_type killer, int killer_index, bool silent)
                                                                    override;
    bool on_part_damage(monster *part, const actor *oppressor,
                                    int damage, beam_type flavour,
                                    kill_method_type kill_type) override;
    void on_head_damage(const actor *oppressor,
                                    int damage, beam_type flavour,
                                    kill_method_type kill_type) override;

    bool expand();
    bool contract(bool from_head);
    bool contract_all();
    void invert();
    monster &get_tail() const;
    int length() const;

    // void adjust_part_glyph(cglyph_t &g, const monster *part,
    //                                 const monster_info *part_info) override;


private:
    void snake_movement_raw(const coord_def& pos);
    bool move_head_to_part(monster *part);

    int max_length;

};
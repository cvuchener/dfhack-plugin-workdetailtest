/*
 * Copyright (c) 2023 Clement Vuchener
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */
#include "UnitsEx.h"
#include "modules/Units.h"

#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/histfig_hf_link.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/occupation.h"
#include "df/plotinfost.h"

#include <algorithm>

using namespace DFHack;
using df::global::plotinfo;

static bool match_position(df::historical_figure *histfig, int group_id, df::entity_position_flags flag)
{
    for (auto link: histfig->entity_links) {
        auto epos = strict_virtual_cast<df::histfig_entity_link_positionst>(link);
        if (!epos)
            continue;
        auto entity = df::historical_entity::find(epos->entity_id);
        if (!entity)
            continue;
        auto assignment = binsearch_in_vector(entity->positions.assignments, epos->assignment_id);
        if (!assignment)
            continue;
        auto position = binsearch_in_vector(entity->positions.own, assignment->position_id);
        if (!position)
            continue;
        if (entity->id == group_id && position->flags.is_set(flag))
            return true;
    }
    return false;
}

bool UnitsEx::hasMenialWorkExemption(df::unit *u, int group_id)
{
    using namespace df::enums::entity_position_flags;
    auto histfig = df::historical_figure::find(u->hist_figure_id);
    if (!histfig)
        return false;
    if (match_position(histfig, group_id, MENIAL_WORK_EXEMPTION))
        return true;
    for (auto link: histfig->histfig_links) {
        if (link->getType() == df::histfig_hf_link_type::SPOUSE) {
            auto spouse_hf = df::historical_figure::find(link->target_hf);
            if (spouse_hf && match_position(spouse_hf, group_id, MENIAL_WORK_EXEMPTION_SPOUSE))
                return true;
        }
    }
    return false;
}

bool UnitsEx::canLearn(df::unit *u)
{
    if (u->curse.rem_tags1.bits.CAN_LEARN)
        return false;
    if (u->curse.add_tags1.bits.CAN_LEARN)
        return true;
    return Units::casteFlagSet(u->race, u->caste, df::caste_raw_flags::CAN_LEARN);
}

bool UnitsEx::canWork(df::unit *u)
{
    if (u->flags1.bits.inactive)
        return false;
    if (!Units::isFortControlled(u))
        return false;
    if (Units::isTamable(u))
        return false;
    if (UnitsEx::hasMenialWorkExemption(u, plotinfo->group_id))
        return false;
    if (!Units::isAdult(u))
        return false;
    if (u->enemy.undead)
        return false;
    if (!UnitsEx::canLearn(u))
        return false;
    if (!Units::isOwnGroup(u) && std::ranges::any_of(u->occupations, [](df::occupation *occ) {
                using namespace df::enums::occupation_type;
                return occ->type == PERFORMER || occ->type == SCHOLAR;
            }))
        return false;
    if (std::ranges::any_of(u->occupations, [](df::occupation *occ) {
                using namespace df::enums::occupation_type;
                return occ->type == MERCENARY || occ->type == MONSTER_SLAYER;
            }))
        return false;
    return true;
}

bool UnitsEx::canBeAdopted(df::unit *u)
{
    return u->flags1.bits.tame
        && !Units::casteFlagSet(u->race, u->caste, df::caste_raw_flags::ADOPTS_OWNER);
}

bool UnitsEx::isSlaughterable(df::unit *u)
{
    return !Units::isPet(u);
    // TODO: check being traded
}

bool UnitsEx::isGeldable(df::unit *u)
{
    // TODO: check u->flags2.bits.calculated_bodyparts and recompute "gelded" if necessary
    if (u->flags3.bits.ghostly || u->flags3.bits.gelded || u->enemy.undead)
        return false;
    return Units::casteFlagSet(u->race, u->caste, df::caste_raw_flags::GELDABLE);
}


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

#include "Labor.h"

#include "modules/Units.h"
#include "modules/Job.h"
#include "UnitsEx.h"

#include "df/gamest.h"
#include "df/job.h"
#include "df/job_item_ref.h"
#include "df/occupation.h"
#include "df/plotinfost.h"
#include "df/work_detail.h"

using namespace DFHack;
using df::global::game;
using df::global::plotinfo;

static constexpr int LaborCount = std::extent_v<decltype(df::unit::T_status::labors)>;

static void cancel_pickup_mismatched_equipment(df::unit *u)
{
    auto job = u->job.current_job;
    if (!job || job->job_type != df::job_type::PickupEquipment)
        return;
    if (job->items.empty())
        return;
    auto item = job->items.front()->item;
    if (item->getType() != df::item_type::WEAPON)
        return;
    auto weapon_skill = static_cast<df::job_skill>(item->getRangedSkill());
    if (weapon_skill == df::job_skill::NONE)
        weapon_skill = static_cast<df::job_skill>(item->getMeleeSkill());
    auto labor_skill = df::job_skill::NONE;
    if (u->status.labors[df::unit_labor::MINE])
        labor_skill = df::job_skill::MINING;
    if (u->status.labors[df::unit_labor::CUTWOOD])
        labor_skill = df::job_skill::AXE;
    if (weapon_skill == labor_skill && !(labor_skill == df::job_skill::AXE && item->getSharpness() <= 0))
        return;
    // TODO: cancel using df::job_cancel_reason::EQUIPMENT_MISMATCH
    Job::removeJob(job);
}

void Labor::updateUnitLabor(df::unit *u)
{
    if (game->external_flag & 1)
        return;
    if (u->profession == df::profession::BABY
            || Units::isTamable(u)
            || !Units::isFortControlled(u)) {
        std::memset(u->status.labors, 0, LaborCount*sizeof(bool));
    }
    else if (u->profession == df::profession::CHILD) {
        if (!plotinfo->labor_info.flags.bits.children_do_chores
                || vector_contains(plotinfo->labor_info.chores_exempted_children, u->id))
            std::memset(u->status.labors, 0, LaborCount*sizeof(bool));
        else
            std::memcpy(u->status.labors, plotinfo->labor_info.chores, LaborCount*sizeof(bool));
    }
    else { // adult citizens
        // save tool-using labors
        bool old_mine = u->status.labors[df::unit_labor::MINE];
        bool old_cutwood = u->status.labors[df::unit_labor::CUTWOOD];
        bool old_hunt = u->status.labors[df::unit_labor::HUNT];

        // set default labors
        auto no_default_labors = UnitsEx::hasMenialWorkExemption(u, plotinfo->group_id) || u->flags4.bits.only_do_assigned_jobs;
        memset(u->status.labors, !no_default_labors, LaborCount*sizeof(bool));
        u->status.labors[df::unit_labor::MINE] = false;
        u->status.labors[df::unit_labor::CUTWOOD] = false;
        u->status.labors[df::unit_labor::HUNT] = false;
        u->status.labors[df::unit_labor::FISH] = false;
        u->status.labors[df::unit_labor::DIAGNOSE] = false;
        u->status.labors[df::unit_labor::SURGERY] = false;
        u->status.labors[df::unit_labor::BONE_SETTING] = false;

        // clear labors from disabled/limited work details
        for (auto work_detail: plotinfo->labor_info.work_details) {
            switch (work_detail->work_detail_flags.bits.mode) {
            case df::work_detail_mode::EverybodyDoesThis:
                break;
            default:
                for (int i = 0; i < LaborCount; ++i)
                    if (work_detail->allowed_labors[i])
                        u->status.labors[i] = false;
                break;
            }
        }
        // set labors from work details
        for (auto work_detail: plotinfo->labor_info.work_details) {
            switch (work_detail->work_detail_flags.bits.mode) {
            case df::work_detail_mode::OnlySelectedDoesThis:
                if (vector_contains(work_detail->assigned_units, u->id))
                    for (int i = 0; i < LaborCount; ++i)
                        if (work_detail->allowed_labors[i])
                            u->status.labors[i] = true;
                break;
            case df::work_detail_mode::EverybodyDoesThis:
                if (!no_default_labors || vector_contains(work_detail->assigned_units, u->id))
                    for (int i = 0; i < LaborCount; ++i)
                        if (work_detail->allowed_labors[i])
                            u->status.labors[i] = true;
                break;
            default:
                break;
            }
        }
        // set labors for medical occupations
        for (auto o: u->occupations) {
            switch (o->type) {
            case df::occupation_type::DOCTOR:
                u->status.labors[df::unit_labor::DIAGNOSE] = true;
                u->status.labors[df::unit_labor::SURGERY] = true;
                u->status.labors[df::unit_labor::BONE_SETTING] = true;
                break;
            case df::occupation_type::DIAGNOSTICIAN:
                u->status.labors[df::unit_labor::DIAGNOSE] = true;
                break;
            case df::occupation_type::SURGEON:
                u->status.labors[df::unit_labor::SURGERY] = true;
                break;
            case df::occupation_type::BONE_DOCTOR:
                u->status.labors[df::unit_labor::BONE_SETTING] = true;
                break;
            default:
                break;
            }
        }
        // update tool if labors were changed
        if (old_mine != u->status.labors[df::unit_labor::MINE]
                || old_cutwood != u->status.labors[df::unit_labor::CUTWOOD]
                || old_hunt != u->status.labors[df::unit_labor::HUNT]) {
            cancel_pickup_mismatched_equipment(u);
            u->military.pickup_flags.bits.update = true;
        }
    }
}


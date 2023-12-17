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
#include "PluginManager.h"
#include "RemoteServer.h"
#include "MiscUtils.h"
#include "DataDefs.h"
#include "modules/Units.h"
#include "modules/Translation.h"
#include "modules/Job.h"

#include "df/world.h"
#include "df/gamest.h"
#include "df/plotinfost.h"
#include "df/unit.h"
#include "df/historical_figure.h"
#include "df/historical_entity.h"
#include "df/histfig_entity_link_positionst.h"
#include "df/entity_position.h"
#include "df/entity_position_assignment.h"
#include "df/histfig_hf_link.h"
#include "df/work_detail.h"
#include "df/occupation.h"
#include "df/job_item_ref.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/interfacest.h"

#include "workdetailtest.pb.h"

#include <random>
#include <format>

#if defined(WIN32)
#   include <windows.h>
#elif defined(_LINUX)
#   include <unistd.h>
#endif

using namespace DFHack;
using namespace dfproto::workdetailtest;

DFHACK_PLUGIN("workdetailtest");
REQUIRE_GLOBAL(world);
REQUIRE_GLOBAL(game);
REQUIRE_GLOBAL(plotinfo);

static constexpr int LaborCount = std::extent_v<decltype(df::unit::T_status::labors)>;

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

static bool has_menial_work_exemption(df::unit *u, int group_id)
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

static bool can_learn(df::unit *u)
{
    if (u->curse.rem_tags1.bits.CAN_LEARN)
        return false;
    if (u->curse.add_tags1.bits.CAN_LEARN)
        return true;
    return Units::casteFlagSet(u->race, u->caste, df::caste_raw_flags::CAN_LEARN);
}

static bool can_assign_work_detail(df::unit *u)
{
    if (u->flags1.bits.inactive)
        return false;
    if (!Units::isFortControlled(u))
        return false;
    if (Units::isTamable(u))
        return false;
    if (has_menial_work_exemption(u, plotinfo->group_id))
        return false;
    if (!Units::isAdult(u))
        return false;
    if (u->enemy.undead)
        return false;
    if (!can_learn(u))
        return false;
    if (!Units::isOwnGroup(u) && std::any_of(u->occupations.begin(), u->occupations.end(), [](df::occupation *occ) {
                using namespace df::enums::occupation_type;
                return occ->type == PERFORMER || occ->type == SCHOLAR;
            }))
        return false;
    if (std::any_of(u->occupations.begin(), u->occupations.end(), [](df::occupation *occ) {
                using namespace df::enums::occupation_type;
                return occ->type == MERCENARY || occ->type == MONSTER_SLAYER;
            }))
        return false;
    return true;
}

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

static void update_unit_labor(df::unit *u)
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
        auto no_default_labors = has_menial_work_exemption(u, plotinfo->group_id) || u->flags4.bits.only_do_assigned_jobs;
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

static command_result do_labor_update_test(color_ostream &out, std::vector<std::string> &parameters)
{
    bool saved_labors[LaborCount];
    for (auto u: world->units.active) {
        std::memcpy(saved_labors, u->status.labors, LaborCount*sizeof(bool));
        out.print("Updating labor for %d %s\n",
                u->id,
                DF2CONSOLE(Translation::TranslateName(&u->name, false)).c_str());
        update_unit_labor(u);
        for (int i = 0; i < LaborCount; ++i) {
            if (saved_labors[i] != u->status.labors[i]) {
                out.printerr("labor %s was %d, updated %d\n",
                        DFHack::enum_item_key(df::unit_labor(i)).c_str(),
                        saved_labors[i],
                        u->status.labors[i]);
            }
        }
        std::memcpy(u->status.labors, saved_labors, LaborCount*sizeof(bool));
    }
    return CR_OK;
}

DFhackCExport command_result plugin_init(color_ostream &out, std::vector<PluginCommand> &commands)
{
    commands.push_back(PluginCommand("laborupdatetest", "test labor update", do_labor_update_test));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
    return CR_OK;
}

static command_result get_process_info(color_ostream &out, const EmptyMessage *, ProcessInfo *info)
{
    static uint32_t cookie = std::random_device{}();
#if defined(WIN32)
    info->set_pid(GetCurrentProcessId());
#elif defined(_LINUX)
    info->set_pid(getpid());
#endif
    info->set_cookie_address(reinterpret_cast<uintptr_t>(&cookie));
    info->set_cookie_value(cookie);
    return CR_OK;
}

static command_result get_game_state(color_ostream &out, const EmptyMessage *, GameState *state)
{
    auto &core = Core::getInstance();
    if (core.isWorldLoaded())
        state->set_world_loaded(reinterpret_cast<uintptr_t>(world->world_data));
    if (core.isMapLoaded())
        state->set_map_loaded(reinterpret_cast<uintptr_t>(world->map.block_index));
    for (auto view = df::global::gview->view.child; view; view = view->child) {
        if (df::viewscreen_setupdwarfgamest::_identity.is_direct_instance(view)) {
            state->set_viewscreen(Viewscreen::SetupDwarfGame);
            break;
        }
    }
    return CR_OK;
}

static command_result edit_unit(color_ostream &out, const UnitProperties *props, Result *result)
{
    if (auto unit = df::unit::find(props->id())) {
        if (props->has_nickname()) {
            unit->name.nickname = props->nickname();
        }
        if (props->has_only_do_assigned_jobs()) {
            unit->flags4.bits.only_do_assigned_jobs = props->only_do_assigned_jobs();
        }
        result->set_success(true);
    }
    else {
        result->set_success(false);
        result->set_error(std::format("invalid unit id: {}", props->id()));
    }
    return CR_OK;
}

static command_result set_work_detail_properties(
        color_ostream &out,
        df::work_detail *work_detail,
        const WorkDetailProperties &props,
        WorkDetailResult *result)
{
    bool labor_changed = false;
    // Name
    if (props.has_name()) {
        work_detail->name = props.name();
    }
    // Mode
    if (props.has_mode()) {
        auto r = result->mutable_mode();
        r->set_success(true);
        auto old_mode = work_detail->work_detail_flags.bits.mode;
        switch (props.mode()) {
        case WorkDetailMode::EverybodyDoesThis:
            work_detail->work_detail_flags.bits.mode = df::work_detail_mode::EverybodyDoesThis;
            break;
        case WorkDetailMode::NobodyDoesThis:
            work_detail->work_detail_flags.bits.mode = df::work_detail_mode::NobodyDoesThis;
            break;
        case WorkDetailMode::OnlySelectedDoesThis:
            work_detail->work_detail_flags.bits.mode = df::work_detail_mode::OnlySelectedDoesThis;
            break;
        default:
            r->set_success(false);
            r->set_error(std::format("Invalid work detail mode: {}", static_cast<int>(props.mode())));
            break;
        }
        if (work_detail->work_detail_flags.bits.mode != old_mode)
            labor_changed = true;
    }
    // Assignments
    if (auto s = props.assignments_size())
        result->mutable_assignments()->Reserve(s);
    for (const auto &assign: props.assignments()) {
        auto r = result->mutable_assignments()->Add();
        auto unit = df::unit::find(assign.unit_id());
        if (!unit) {
            r->set_success(false);
            r->set_error(std::format("unit {} not found", assign.unit_id()));
            continue;
        }
        if (!can_assign_work_detail(unit)) {
            r->set_success(false);
            r->set_error(std::format("unit {} can not be assigned to a work detail", unit->id));
            continue;
        }
        r->set_success(true);
        if (assign.enable()) {
            bool inserted;
            insert_into_vector(work_detail->assigned_units, unit->id, &inserted);
            if (!inserted)
                r->set_error(std::format("unit {} already assigned", unit->id));
        }
        else {
            if (!erase_from_vector(work_detail->assigned_units, unit->id))
                r->set_error(std::format("unit {} already not assigned", unit->id));
        }
        update_unit_labor(unit);
    }
    // Labors
    if (auto s = props.labors_size())
        result->mutable_labors()->Reserve(s);
    for (const auto &labor: props.labors()) {
        auto r = result->mutable_labors()->Add();
        if (labor.labor() < 0 || labor.labor() >= LaborCount) {
            r->set_success(false);
            r->set_error(std::format("Invalid labor value: {}", labor.labor()));
            continue;
        }
        r->set_success(true);
        work_detail->allowed_labors[labor.labor()] = labor.enable();
        labor_changed = true;
    }
    // Icon
    if (props.has_icon()) {
        auto r = result->mutable_icon();
        auto icon_id = props.icon();
        r->set_success(df::enum_traits<decltype(work_detail->icon)>::is_valid(icon_id));
        if (r->success())
            work_detail->icon = static_cast<decltype(work_detail->icon)>(icon_id);
        else
            r->set_error(std::format("Invalid icon value: {}", icon_id));
    }
    // Update all labors in case of global work detail changes
    if (labor_changed) {
        for (auto unit: world->units.active)
            update_unit_labor(unit);
    }
    return CR_OK;
}

// Returns iterator in plotinfo->labor_info.work_details (end if not found)
static auto find_work_detail(const WorkDetailId &id, Result *result)
{
    auto &work_details = plotinfo->labor_info.work_details;
    if (id.index() >= work_details.size()) {
        result->set_success(false);
        result->set_error(std::format("invalid work detail index: {}", id.index()));
        return work_details.end();
    }
    auto work_detail = work_details.begin() + id.index();
    if ((*work_detail)->name != id.name()) {
        result->set_success(false);
        result->set_error(std::format("invalid work detail name: {} is named {}, parameter was {}",
                    id.index(), (*work_detail)->name, id.name()));
        return work_details.end();
    }
    result->set_success(true);
    return work_detail;
}

static command_result edit_work_detail(
        color_ostream &out,
        const EditWorkDetail *edit,
        WorkDetailResult *result)
{
    // Find Work detail
    auto work_detail = find_work_detail(edit->id(), result->mutable_work_detail());
    if (work_detail == plotinfo->labor_info.work_details.end())
        return CR_OK;
    // Apply changes
    return set_work_detail_properties(out, *work_detail, edit->changes(), result);
}

static command_result add_work_detail(
        color_ostream &out,
        const AddWorkDetail *add,
        WorkDetailResult *result)
{
    // Create new work detail
    auto new_work_detail = new df::work_detail;
    new_work_detail->name = "New work detail";
    auto &work_details = plotinfo->labor_info.work_details;
    // Insert in work detail vector
    auto insert_pos = add->has_position() && add->position() < work_details.size()
        ? work_details.begin() + add->position()
        : work_details.end();
    work_details.insert(insert_pos, new_work_detail);
    result->mutable_work_detail()->set_success(true);
    // Set new work detail properties
    return set_work_detail_properties(out, new_work_detail, add->properties(), result);
}

static command_result remove_work_detail(
        color_ostream &out,
        const RemoveWorkDetail *remove,
        Result *result)
{
    // Find Work detail
    auto work_detail = find_work_detail(remove->id(), result);
    if (work_detail == plotinfo->labor_info.work_details.end())
        return CR_OK;
    // Delete
    delete *work_detail;
    plotinfo->labor_info.work_details.erase(work_detail);
    return CR_OK;
}

static command_result move_work_detail(
        color_ostream &out,
        const MoveWorkDetail *move,
        Result *result)
{
    auto &work_details = plotinfo->labor_info.work_details;
    // Find Work detail
    auto work_detail = find_work_detail(move->id(), result);
    if (work_detail == plotinfo->labor_info.work_details.end())
        return CR_OK;
    std::size_t old_position = distance(work_details.begin(), work_detail);
    // Check new position
    if (!move->has_new_position()) {
        result->set_success(false);
        result->set_error("Missing new position");
        return CR_OK;
    }
    std::size_t new_position = move->new_position();
    if (new_position >= work_details.size()) {
        result->set_success(false);
        result->set_error(std::format("Invalid new position: {}, size is {}",
                new_position, work_details.size()));
        return CR_OK;
    }
    // Apply move
    if (new_position == old_position) {
        // Nothing to do
        return CR_OK;
    }
    auto new_pos_it = next(work_details.begin(), new_position);
    if (new_position > old_position)
        std::rotate(work_detail, next(work_detail), new_pos_it);
    else
        std::rotate(new_pos_it, work_detail, next(work_detail));
    return CR_OK;
}

DFhackCExport RPCService *plugin_rpcconnect(color_ostream &out)
{
    RPCService *svc = new RPCService();
    svc->addFunction("GetProcessInfo", get_process_info, SF_ALLOW_REMOTE | SF_DONT_SUSPEND);
    svc->addFunction("GetGameState", get_game_state, SF_ALLOW_REMOTE);
    svc->addFunction("EditUnit", edit_unit, SF_ALLOW_REMOTE);
    svc->addFunction("EditWorkDetail", edit_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("AddWorkDetail", add_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("RemoveWorkDetail", remove_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("MoveWorkDetail", move_work_detail, SF_ALLOW_REMOTE);
    return svc;
}

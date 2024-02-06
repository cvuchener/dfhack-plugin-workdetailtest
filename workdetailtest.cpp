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
#include "modules/Translation.h"
#include "modules/Job.h"
#include "UnitsEx.h"
#include "Labor.h"

#include "df/interfacest.h"
#include "df/plotinfost.h"
#include "df/unit.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/work_detail.h"
#include "df/world.h"

#include "workdetailtest.pb.h"

#include <random>
#include <format>
#include <cstring>

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

static command_result do_labor_update_test(color_ostream &out, std::vector<std::string> &parameters)
{
    bool saved_labors[LaborCount];
    for (auto u: world->units.active) {
        std::memcpy(saved_labors, u->status.labors, LaborCount*sizeof(bool));
        out.print("Updating labor for %d %s\n",
                u->id,
                DF2CONSOLE(Translation::TranslateName(&u->name, false)).c_str());
        Labor::updateUnitLabor(u);
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

template <typename... Args>
static void set_error(Result *result, std::format_string<Args...> fmt, Args &&...args)
{
        result->set_success(false);
        result->set_error(std::format(fmt, std::forward<Args>(args)...));
}

static df::unit *find_unit(const UnitId &id, Result *result)
{
    if (auto unit = df::unit::find(id.id())) {
        result->set_success(true);
        return unit;
    }
    else {
        set_error(result, "Invalid unit id: {}", id.id());
        return nullptr;
    }
}

static void set_geld(df::unit *u, bool geld);
static void set_slaughter(df::unit *u, bool slaughter)
{
    if (slaughter) {
        u->flags2.bits.slaughter = true;
        u->flags3.bits.available_for_adoption = false;
        if (u->flags3.bits.marked_for_gelding)
            set_geld(u, false);
    }
    else {
        u->flags2.bits.slaughter = false;
        for (auto job_item = world->jobs.list.next; job_item; job_item = job_item->next) {
            auto job = job_item->item;
            if (job->job_type != df::job_type::SlaughterAnimal)
                continue;
            auto slaughteree = Job::getGeneralRef(job, df::general_ref_type::UNIT_SLAUGHTEREE);
            if (slaughteree && slaughteree->getUnit() == u) {
                Job::removeJob(job);
                break;
            }
        }
    }
}
static void set_geld(df::unit *u, bool geld)
{
    if (geld) {
        u->flags3.bits.marked_for_gelding = true;
        if (u->flags2.bits.slaughter)
            set_slaughter(u, false);
    }
    else {
        u->flags3.bits.marked_for_gelding = false;
        for (auto job_item = world->jobs.list.next; job_item; job_item = job_item->next) {
            auto job = job_item->item;
            if (job->job_type != df::job_type::GeldAnimal)
                continue;
            auto geldee = Job::getGeneralRef(job, df::general_ref_type::UNIT_GELDEE);
            if (geldee && geldee->getUnit() == u) {
                Job::removeJob(job);
                break;
            }
        }
    }
}

static void set_adoption(df::unit *u, bool available_for_adoption)
{
    if (available_for_adoption) {
        if (u->flags2.bits.slaughter)
            set_slaughter(u, false);
        u->flags3.bits.available_for_adoption = true;
    }
    else {
        u->flags3.bits.available_for_adoption = false;
    }
}

static command_result set_unit_properties(
        color_ostream &out,
        df::unit *unit,
        const UnitProperties &props,
        UnitResult *result)
{
    if (props.has_nickname()) {
        unit->name.nickname = props.nickname();
    }
    result->mutable_flags()->Reserve(props.flags_size());
    for (const auto &flag: props.flags()) {
        auto flag_result = result->mutable_flags()->Add();
        flag_result->set_flag(flag.flag());
        auto r = flag_result->mutable_result();
        switch (flag.flag()) {
        case OnlyDoAssignedJobs:
            if (UnitsEx::canWork(unit)) {
                unit->flags4.bits.only_do_assigned_jobs = flag.value();
                r->set_success(true);
            }
            else {
                set_error(r, "Unit cannot work");
            }
            break;
        case AvailableForAdoption:
            if (UnitsEx::canBeAdopted(unit)) {
                set_adoption(unit, flag.value());
                r->set_success(true);
            }
            else {
                set_error(r, "Unit cannot be adopted");
            }
            break;
        case MarkedForSlaughter:
            if (UnitsEx::isSlaughterable(unit)) {
                set_slaughter(unit, flag.value());
                r->set_success(true);
            }
            else {
                set_error(r, "Unit cannot be slaughtered");
            }
            break;
        case MarkedForGelding:
            if (UnitsEx::isGeldable(unit)) {
                set_geld(unit, flag.value());
                r->set_success(true);
            }
            else {
                set_error(r, "Unit cannot be gelded");
            }
            break;
        default:
            set_error(r, "Unknown unit flag");
            break;
        }

    }
    return CR_OK;
}

static command_result edit_unit(color_ostream &out, const EditUnit *edit, UnitResult *result)
{
    if (auto unit = find_unit(edit->id(), result->mutable_unit()))
        return set_unit_properties(out, unit, edit->changes(), result);
    return CR_OK;
}

static command_result edit_units(color_ostream &out, const EditUnits *edit, UnitResults *results)
{
    results->mutable_results()->Reserve(edit->units().size());
    for (const auto &edit: edit->units()) {
        auto result = results->mutable_results()->Add();
        edit_unit(out, &edit, result);
    }
    return CR_OK;
}

static command_result set_work_detail_properties(
        color_ostream &out,
        df::work_detail *work_detail,
        const WorkDetailProperties &props,
        WorkDetailResult *result,
        bool update_labors_for_all = false)
{
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
            set_error(r, "Invalid work detail mode: {}", static_cast<int>(props.mode()));
            break;
        }
        if (work_detail->work_detail_flags.bits.mode != old_mode)
            update_labors_for_all = true;
    }
    // Assignments
    if (auto s = props.assignments_size())
        result->mutable_assignments()->Reserve(s);
    for (const auto &assign: props.assignments()) {
        auto r = result->mutable_assignments()->Add();
        auto unit = df::unit::find(assign.unit_id());
        if (!unit) {
            set_error(r, "unit {} not found", assign.unit_id());
            continue;
        }
        if (!UnitsEx::canWork(unit)) {
            set_error(r, "unit {} can not be assigned to a work detail", unit->id);
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
        Labor::updateUnitLabor(unit);
    }
    // Labors
    if (auto s = props.labors_size())
        result->mutable_labors()->Reserve(s);
    for (const auto &labor: props.labors()) {
        auto r = result->mutable_labors()->Add();
        if (labor.labor() < 0 || labor.labor() >= LaborCount) {
            set_error(r, "Invalid labor value: {}", labor.labor());
            continue;
        }
        r->set_success(true);
        work_detail->allowed_labors[labor.labor()] = labor.enable();
        update_labors_for_all = true;
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
    // Other flags
    if (props.has_no_modify())
        work_detail->work_detail_flags.bits.no_modify = props.no_modify();
    if (props.has_cannot_be_everybody())
        work_detail->work_detail_flags.bits.cannot_be_everybody = props.cannot_be_everybody();
    // Update all labors in case of global work detail changes
    if (update_labors_for_all) {
        for (auto unit: world->units.active)
            Labor::updateUnitLabor(unit);
    }
    return CR_OK;
}

// Returns iterator in plotinfo->labor_info.work_details (end if not found)
static auto find_work_detail(const WorkDetailId &id, Result *result)
{
    auto &work_details = plotinfo->labor_info.work_details;
    if (id.index() >= work_details.size()) {
        set_error(result, "invalid work detail index: {}", id.index());
        return work_details.end();
    }
    auto work_detail = work_details.begin() + id.index();
    if ((*work_detail)->name != id.name()) {
        set_error(result, "invalid work detail name: {} is named {}, parameter was {}",
                    id.index(), (*work_detail)->name, id.name());
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
    if (*df::global::gamemode != df::game_mode::DWARF)
        return CR_FAILURE;
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
    if (*df::global::gamemode != df::game_mode::DWARF)
        return CR_FAILURE;
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
    return set_work_detail_properties(out, new_work_detail, add->properties(), result, true);
}

static command_result remove_work_detail(
        color_ostream &out,
        const RemoveWorkDetail *remove,
        Result *result)
{
    if (*df::global::gamemode != df::game_mode::DWARF)
        return CR_FAILURE;
    // Find Work detail
    auto work_detail = find_work_detail(remove->id(), result);
    if (work_detail == plotinfo->labor_info.work_details.end())
        return CR_OK;
    // Delete
    delete *work_detail;
    plotinfo->labor_info.work_details.erase(work_detail);
    // Update labors
    for (auto unit: world->units.active)
        Labor::updateUnitLabor(unit);
    return CR_OK;
}

static command_result move_work_detail(
        color_ostream &out,
        const MoveWorkDetail *move,
        Result *result)
{
    if (*df::global::gamemode != df::game_mode::DWARF)
        return CR_FAILURE;
    auto &work_details = plotinfo->labor_info.work_details;
    // Find Work detail
    auto work_detail = find_work_detail(move->id(), result);
    if (work_detail == plotinfo->labor_info.work_details.end())
        return CR_OK;
    std::size_t old_position = distance(work_details.begin(), work_detail);
    // Check new position
    if (!move->has_new_position()) {
        set_error(result, "Missing new position");
        return CR_OK;
    }
    std::size_t new_position = move->new_position();
    if (new_position >= work_details.size()) {
        set_error(result, "Invalid new position: {}, size is {}",
                new_position, work_details.size());
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
    svc->addFunction("EditUnits", edit_units, SF_ALLOW_REMOTE);
    svc->addFunction("EditWorkDetail", edit_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("AddWorkDetail", add_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("RemoveWorkDetail", remove_work_detail, SF_ALLOW_REMOTE);
    svc->addFunction("MoveWorkDetail", move_work_detail, SF_ALLOW_REMOTE);
    return svc;
}

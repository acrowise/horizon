#include "tool_edit_board_hole.hpp"
#include "core_board.hpp"
#include "imp/imp_interface.hpp"
#include <iostream>

namespace horizon {

ToolEditBoardHole::ToolEditBoardHole(Core *c, ToolID tid) : ToolBase(c, tid)
{
}

bool ToolEditBoardHole::can_begin()
{
    return get_holes().size() > 0;
}

std::set<BoardHole *> ToolEditBoardHole::get_holes()
{
    std::set<BoardHole *> holes;
    for (const auto &it : selection) {
        if (it.type == ObjectType::BOARD_HOLE) {
            holes.emplace(&core.b->get_board()->holes.at(it.uuid));
        }
    }
    return holes;
}

ToolResponse ToolEditBoardHole::begin(const ToolArgs &args)
{
    auto holes = get_holes();
    auto r = imp->dialogs.edit_board_hole(holes, core.r->get_pool(), core.b->get_block());
    if (r) {
        return ToolResponse::commit();
    }
    else {
        return ToolResponse::revert();
    }
}
ToolResponse ToolEditBoardHole::update(const ToolArgs &args)
{
    return ToolResponse();
}
} // namespace horizon

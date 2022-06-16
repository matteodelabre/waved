/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "defs.hpp"

namespace Waved
{

std::string mode_kind_to_string(ModeKind kind)
{
    switch (kind) {
    case ModeKind::INIT:
        return "INIT";

    case ModeKind::DU:
        return "DU";

    case ModeKind::DU4:
        return "DU4";

    case ModeKind::A2:
        return "A2";

    case ModeKind::GC16:
        return "GC16";

    case ModeKind::GLR16:
        return "GLR16";

    default:
        return "UNKNOWN";
    }
}

}

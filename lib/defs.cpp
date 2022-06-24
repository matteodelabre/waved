/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "defs.hpp"

namespace Waved
{

auto mode_kind_to_string(ModeKind kind) -> std::string
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

auto mode_kind_from_string(const std::string& str) -> ModeKind
{
    if (str == "INIT") {
        return ModeKind::INIT;
    }

    if (str == "DU") {
        return ModeKind::DU;
    }

    if (str == "DU4") {
        return ModeKind::DU4;
    }

    if (str == "A2") {
        return ModeKind::A2;
    }

    if (str == "GC16") {
        return ModeKind::GC16;
    }

    if (str == "GLR16") {
        return ModeKind::GLR16;
    }

    return ModeKind::UNKNOWN;
}

} // namespace Waved

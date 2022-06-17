/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "defs.hpp"

namespace Waved
{

void Region::extend(const Region& region)
{
    if (region.width == 0 && region.height == 0) {
        return;
    }

    if (this->width == 0 && this->height == 0) {
        *this = region;
        return;
    }

    auto top = std::min(this->top, region.top);
    auto left = std::min(this->left, region.left);
    auto width = std::max(
        this->left + this->width,
        region.left + region.width
    ) - left;
    auto height = std::max(
        this->top + this->height,
        region.top + region.height
    ) - top;

    this->top = top;
    this->left = left;
    this->width = width;
    this->height = height;
}

void Region::extend(std::uint32_t x, std::uint32_t y)
{
    this->extend(Region{
        /* top = */ y,
        /* left = */ x,
        /* width = */ 1,
        /* height = */ 1
    });
}

auto Region::contains(const Region& region) -> bool
{
    return (
        this->left <= region.left
        && this->top <= region.top
        && this->left + this->width >= region.left + region.width
        && this->top + this->height >= region.top + region.height
    );
}

auto Region::contains(std::uint32_t x, std::uint32_t y) -> bool
{
    return this->contains(Region{
        /* top = */ y,
        /* left = */ x,
        /* width = */ 1,
        /* height = */ 1
    });
}

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

}

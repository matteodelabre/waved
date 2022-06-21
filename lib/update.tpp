/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "update.hpp"

namespace Waved
{

template<typename Coordinate>
void Region<Coordinate>::extend(const Region<Coordinate>& region)
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
    auto right = std::max(
        this->left + this->width,
        region.left + region.width
    );
    auto bottom = std::max(
        this->top + this->height,
        region.top + region.height
    );

    this->top = top;
    this->left = left;
    this->width = right - left;
    this->height = bottom - top;
}

template<typename Coordinate>
void Region<Coordinate>::extend(Coordinate x, Coordinate y)
{
    this->extend(Region{
        /* top = */ y,
        /* left = */ x,
        /* width = */ 1,
        /* height = */ 1
    });
}

} // namespace Waved

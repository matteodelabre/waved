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

template<typename Coordinate>
auto Region<Coordinate>::contains(const Region<Coordinate>& region) -> bool
{
    return (
        this->left <= region.left
        && this->top <= region.top
        && this->left + this->width >= region.left + region.width
        && this->top + this->height >= region.top + region.height
    );
}

template<typename Coordinate>
auto Region<Coordinate>::contains(Coordinate x, Coordinate y) -> bool
{
    return this->contains(Region{
        /* top = */ y,
        /* left = */ x,
        /* width = */ 1,
        /* height = */ 1
    });
}

} // namespace Waved

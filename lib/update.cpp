/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "update.hpp"

namespace Waved
{

namespace
{

/**
 * Copy a rectangle from a buffer to another.
 *
 * @param source Source buffer.
 * @param source_region Rectangle specifying the region to copy from.
 * @param source_width Total width of the source buffer.
 * @param dest Destination buffer.
 * @param dest_top Top position to start copying in.
 * @param dest_left Left position to start copying in.
 * @param dest_width Total width of the destination buffer.
 */
template<typename Unit, typename Coordinate>
void copy_rect(
    const Unit* source,
    const Region<Coordinate>& source_region,
    Coordinate source_width,
    Unit* dest,
    Coordinate dest_top,
    Coordinate dest_left,
    Coordinate dest_width
)
{
    source += source_region.left + source_width * source_region.top;
    dest += dest_left + dest_width * dest_top;

    for (Coordinate y = 0; y < source_region.height; ++y) {
        std::copy(source, source + source_region.width, dest);
        source += source_width;
        dest += dest_width;
    }
}

} // anonymous namespace

void Update::apply(Intensity* target, std::uint32_t target_width)
{
    copy_rect(
        /* source = */ this->buffer.data(),
        /* source_region = */ UpdateRegion{
            /* top = */ 0u,
            /* left = */ 0u,
            /* width = */ this->region.width,
            /* height = */ this->region.height
        },
        /* source_width = */ this->region.width,
        /* dest = */ target,
        /* dest_top = */ this->region.top,
        /* dest_left = */ this->region.left,
        /* dest_width = */ target_width
    );
}

} // namespace Waved

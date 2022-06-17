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

template<typename Unit, typename Coordinate>
bool equal_rect(
    const Unit* buf1,
    const Region<Coordinate>& region1,
    Coordinate width1,
    Unit* buf2,
    Coordinate top2,
    Coordinate left2,
    Coordinate width2
)
{
    buf1 += region1.left + width1 * region1.top;
    buf2 += left2 + width2 * top2;

    for (Coordinate y = 0; y < region1.height; ++y) {
        if (!std::equal(buf1, buf1 + region1.width, buf2)) {
            return false;
        }

        buf1 += width1;
        buf2 += width2;
    }

    return true;
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

bool Update::merge(Update& update)
{
    if (this->immediate != update.immediate) {
        return false;
    }

    if (this->mode != update.mode) {
        return false;
    }

    UpdateRegion merged_region = this->region;
    merged_region.extend(update.region);

    /* UpdateRegion shared_region = this->region; */
    /* shared_region.intersect(update.region); */
    // TODO: Check that there’s no conflict in shared_region

    std::copy(
        update.id.cbegin(), update.id.cend(),
        std::back_inserter(this->id)
    );

    this->region = merged_region;
    return true;
}

void Update::crop(const UpdateRegion& target)
{
    std::vector<Intensity> cropped_buffer(target.width * target.height);

    UpdateRegion relative_target = target;
    relative_target.left -= this->region.left;
    relative_target.top -= this->region.top;

    copy_rect(
        /* source = */ this->buffer.data(),
        /* source_region = */ relative_target,
        /* source_width = */ this->region.width,
        /* dest = */ cropped_buffer.data(),
        /* dest_top = */ 0u,
        /* dest_left = */ 0u,
        /* dest_width = */ target.width
    );

    this->region = target;
    this->buffer = std::move(cropped_buffer);
}

} // namespace Waved

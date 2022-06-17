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

bool Update::merge(
    Update& update,
    const Intensity* background,
    std::uint32_t background_width
)
{
    if (this->immediate != update.immediate) {
        return false;
    }

    if (this->mode != update.mode) {
        return false;
    }

    UpdateRegion merged_region{};
    merged_region.extend(this->region);
    merged_region.extend(update.region);

    // TODO: Reduce copies and reallocations

    // Create merged buffer with overlayed current intensities,
    // current update and merged update
    std::vector<Intensity> merged_buffer(
        merged_region.width * merged_region.height
    );

    copy_rect(
        /* source = */ background,
        /* source_region = */ merged_region,
        /* source_width = */ background_width,
        /* dest = */ merged_buffer.data(),
        /* dest_top = */ 0u,
        /* dest_left = */ 0u,
        /* dest_width = */ merged_region.width
    );

    copy_rect(
        /* source = */ this->buffer.data(),
        /* source_region = */ UpdateRegion{
            0, 0,
            this->region.width, this->region.height
        },
        /* source_width = */ this->region.width,
        /* dest = */ merged_buffer.data(),
        /* dest_top = */ this->region.top - merged_region.top,
        /* dest_left = */ this->region.left - merged_region.left,
        /* dest_width = */ merged_region.width
    );

    copy_rect(
        /* source = */ update.buffer.data(),
        /* source_region = */ UpdateRegion{
            0, 0,
            update.region.width, update.region.height
        },
        /* source_width = */ update.region.width,
        /* dest = */ merged_buffer.data(),
        /* dest_top = */ update.region.top - merged_region.top,
        /* dest_left = */ update.region.left - merged_region.left,
        /* dest_width = */ merged_region.width
    );

    if (!equal_rect(
        /* buf1 = */ this->buffer.data(),
        /* region1 = */ UpdateRegion{
            0, 0,
            this->region.width, this->region.height
        },
        /* width1 = */ this->region.width,
        /* buf2 = */ merged_buffer.data(),
        /* top2 = */ this->region.top - merged_region.top,
        /* left2 = */ this->region.left - merged_region.left,
        /* width2 = */ merged_region.width
    )) {
        // Conflicting update contents
        return false;
    }

    std::copy(
        update.id.cbegin(), update.id.cend(),
        std::back_inserter(this->id)
    );

    this->region = std::move(merged_region);
    this->buffer = std::move(merged_buffer);
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

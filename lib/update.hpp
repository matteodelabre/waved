/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_UPDATE_HPP
#define WAVED_UPDATE_HPP

#include "defs.hpp"
#include "waveform_table.hpp"

namespace Waved
{

/**
 * Rectangular region.
 *
 * For compatibility with other drivers, note that the “top” (y) attribute
 * comes before the “left” (x) attribute, even though the “width” attribute
 * comes before the “height” attribute.
 */
template<typename Coordinate>
struct Region
{
    Coordinate top;
    Coordinate left;
    Coordinate width;
    Coordinate height;

    /** Extend this region to encompass another one. */
    void extend(const Region<Coordinate>& region);

    /** Extend this region to encompass a point. */
    void extend(Coordinate x, Coordinate y);

    /** Intersect this region with another one. */
    void intersect(const Region<Coordinate>& region);

    /** Check if this region contains another one. */
    bool contains(const Region<Coordinate>& region);

    /** Check if this region contains a point. */
    bool contains(Coordinate x, Coordinate y);
};

/** Region type used for updates. */
using UpdateRegion = Region<std::uint32_t>;

/** Identifier for updates. */
using UpdateID = std::uint32_t;

/** Display update request. */
struct Update
{
    // List of IDs for this update. Usually contains just a single ID,
    // but can contain more if several updates are merged together
    std::vector<UpdateID> id;

    // Update mode
    ModeID mode;

    // Whether to process this update in immediate mode or in batch mode
    bool immediate;

    // Coordinates of the region affected by the update
    UpdateRegion region{};

    // Buffer containing the target intensities of the region
    std::vector<Intensity> buffer;

#ifdef ENABLE_PERF_REPORT
    // Time of addition to the update queue
    std::chrono::steady_clock::time_point queue_time;

    // Time of removal from the update queue
    std::chrono::steady_clock::time_point dequeue_time;

    // Frame generation start time and individual frame end times
    std::vector<std::chrono::steady_clock::time_point> generate_times;

    // Vsync start time and individual frame end times
    std::vector<std::chrono::steady_clock::time_point> vsync_times;
#endif // ENABLE_PERF_REPORT

    /** Copy the update buffer to a destination buffer. */
    void apply(Intensity* target, std::uint32_t target_width);
};

} // namespace Waved

#include "update.tpp"

#endif // WAVED_UPDATE_HPP

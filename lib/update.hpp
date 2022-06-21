/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_UPDATE_HPP
#define WAVED_UPDATE_HPP

#include "defs.hpp"
#include "waveform_table.hpp"
#include <chrono>

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
    bool contains(const Region<Coordinate>& region) const;

    /** Check if this region contains a point. */
    bool contains(Coordinate x, Coordinate y) const;
};

using UpdateRegion = Region<std::uint32_t>;
using UpdateID = std::uint32_t;

/**
 * Request for an EPD update.
 */
class Update
{
public:
    /** Initialize an empty update. */
    Update();

    /** Initialize an update. */
    Update(
        ModeID mode,
        bool immediate,
        UpdateRegion region,
        std::vector<Intensity> buffer
    );

    const std::vector<UpdateID>& get_id() const;

    ModeID get_mode() const;

    bool get_immediate() const;

    const UpdateRegion& get_region() const;
    void set_region(const UpdateRegion& region);

    const std::vector<Intensity>& get_buffer() const;

    /** Copy the update buffer to a destination buffer. */
    void apply(Intensity* target, std::uint32_t target_width) const;

    /**
     * Merge this update’s metadata with another update.
     *
     * Only the metadata is merged, buffers are left as-is.
     */
    void merge_with(const Update& update);

    /** Record update enqueue time as now. */
    void record_enqueue();

    /** Record update dequeue time as now. */
    void record_dequeue();

    /** Record the start of a frame generation as now. */
    void record_generate_start();

    /** Record the end of a frame generation as now. */
    void record_generate_end();

    /** Record the start of a frame vsync as now. */
    void record_vsync_start();

    /** Record the end of a frame vsync as now. */
    void record_vsync_end();

    /**
     * Export a CSV line containing this update’s performance record.
     *
     * This method is a no-op unless ENABLE_PERF_REPORT is set at compile time.
     */
    void dump_perf_record(std::ostream& out) const;

private:
    static UpdateID next_id;

    // List of IDs for this update. Usually contains just a single ID,
    // but can contain more if several updates are merged together
    std::vector<UpdateID> id;

    // Update mode
    ModeID mode = 0;

    // Whether to process this update in immediate mode or in batch mode
    bool immediate = false;

    // Coordinates of the region affected by the update
    UpdateRegion region{};

    // Buffer containing the target intensities of the region
    std::vector<Intensity> buffer;

#ifdef ENABLE_PERF_REPORT
    // Time points at which each update was added to the queue
    std::vector<std::chrono::steady_clock::time_point> enqueue_times;

    // Time points at which each update was removed from the queue
    std::vector<std::chrono::steady_clock::time_point> dequeue_times;

    // Frame generation start time points
    std::vector<std::chrono::steady_clock::time_point> generate_start_times;

    // Frame generation end time points
    std::vector<std::chrono::steady_clock::time_point> generate_end_times;

    // Frame vsync start time points
    std::vector<std::chrono::steady_clock::time_point> vsync_start_times;

    // Frame vsync end time points
    std::vector<std::chrono::steady_clock::time_point> vsync_end_times;
#endif // ENABLE_PERF_REPORT
};

} // namespace Waved

#include "update.tpp"

#endif // WAVED_UPDATE_HPP

/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "update.hpp"
#include <chrono>

namespace chrono = std::chrono;

namespace Waved
{

namespace
{

#ifdef ENABLE_PERF_REPORT
std::ostream& operator<<(std::ostream& out, chrono::steady_clock::time_point t)
{
    out << chrono::duration_cast<chrono::microseconds>(t.time_since_epoch())
        .count();
    return out;
}

template<typename Elem>
std::ostream& operator<<(std::ostream& out, const std::vector<Elem>& ts)
{
    for (auto it = ts.cbegin(); it != ts.cend(); ++it) {
        out << *it;

        if (std::next(it) != ts.cend()) {
            out << ':';
        }
    }

    return out;
}
#endif // ENABLE_PERF_REPORT

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

UpdateID Update::next_id = 0;

Update::Update()
{}

Update::Update(
    ModeID mode,
    bool immediate,
    UpdateRegion region,
    std::vector<Intensity> buffer
)
: id{this->next_id++}
, mode(mode)
, immediate(immediate)
, region(region)
, buffer(std::move(buffer))
{}

const std::vector<UpdateID>& Update::get_id() const
{
    return this->id;
}

ModeID Update::get_mode() const
{
    return this->mode;
}

bool Update::get_immediate() const
{
    return this->immediate;
}

const UpdateRegion& Update::get_region() const
{
    return this->region;
}

void Update::set_region(const UpdateRegion& region)
{
    this->region = region;
}

const std::vector<Intensity>& Update::get_buffer() const
{
    return this->buffer;
}

void Update::apply(Intensity* target, std::uint32_t target_width) const
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

void Update::merge_with(const Update& update)
{
    this->region.extend(update.region);
    std::copy(
        update.id.cbegin(), update.id.cend(),
        back_inserter(this->id)
    );

#ifdef ENABLE_PERF_REPORT
    std::copy(
        update.enqueue_times.cbegin(), update.enqueue_times.cend(),
        std::back_inserter(this->enqueue_times)
    );
    std::copy(
        update.dequeue_times.cbegin(), update.dequeue_times.cend(),
        std::back_inserter(this->dequeue_times)
    );
#endif // ENABLE_PERF_REPORT
}

void Update::record_enqueue()
{
#ifdef ENABLE_PERF_REPORT
    this->enqueue_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::record_dequeue()
{
#ifdef ENABLE_PERF_REPORT
    this->dequeue_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::record_generate_start()
{
#ifdef ENABLE_PERF_REPORT
    this->generate_start_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::record_generate_end()
{
#ifdef ENABLE_PERF_REPORT
    this->generate_end_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::record_vsync_start()
{
#ifdef ENABLE_PERF_REPORT
    this->vsync_start_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::record_vsync_end()
{
#ifdef ENABLE_PERF_REPORT
    this->vsync_end_times.emplace_back(chrono::steady_clock::now());
#endif
}

void Update::dump_perf_record(std::ostream& out) const
{
#ifdef ENABLE_PERF_REPORT
    out << this->id << ','
        << static_cast<int>(this->mode) << ','
        << this->immediate << ','
        << this->region.width << ','
        << this->region.height << ','
        << this->enqueue_times << ','
        << this->dequeue_times << ','
        << this->generate_start_times << ','
        << this->generate_end_times << ','
        << this->vsync_start_times << ','
        << this->vsync_end_times << '\n';
#endif
}

} // namespace Waved

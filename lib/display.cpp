/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.hpp"
#include <system_error>
#include <chrono>
#include <cstring>
#include <cmath>
#include <endian.h>
#include <iostream>
#include <linux/fb.h>

namespace chrono = std::chrono;

namespace
{

constexpr int fbioblank_off = FB_BLANK_POWERDOWN;
constexpr int fbioblank_on = FB_BLANK_UNBLANK;

}

namespace Waved
{

Generator::Generator(
    Controller& controller,
    WaveformTable& waveform_table
)
: controller(&controller)
, table(&waveform_table)
, current_intensity(controller.get_dimensions().real_size)
, next_intensity(controller.get_dimensions().real_size)
, waveform_steps(controller.get_dimensions().real_size)
{}

Generator::~Generator()
{
    this->stop();
}

void Generator::start()
{
    // Open the framebuffer and temperature devices
    this->controller->start();

    // Start the processing threads
    this->stopping_generator = false;
    this->generator_thread = std::thread(&Generator::run_generator_thread, this);
    pthread_setname_np(this->generator_thread.native_handle(), "waved_generator");

    this->stopping_vsync = false;
    this->vsync_thread = std::thread(&Generator::run_vsync_thread, this);
    pthread_setname_np(this->vsync_thread.native_handle(), "waved_vsync");

    this->started = true;
}

void Generator::stop()
{
    if (this->started) {
        // Wait for the current update to be processed then terminate
        {
            std::unique_lock<std::mutex> lock(this->updates_lock);
            this->stopping_generator = true;
            this->updates_cv.notify_one();
        }
        this->generator_thread.join();

        // Terminate the vsync thread
        {
            std::unique_lock<std::mutex> lock(this->vsync_read_lock);
            this->stopping_vsync = true;
            this->vsync_can_read_cv.notify_one();
        }
        this->vsync_thread.join();
        this->started = false;
    }

    this->controller->stop();
}

std::optional<UpdateID> Generator::push_update(
    ModeKind mode,
    bool immediate,
    UpdateRegion region,
    const std::vector<Intensity>& buffer
)
{
    return this->push_update(
        this->table->get_mode_id(mode),
        immediate,
        region,
        buffer
    );
}

std::optional<UpdateID> Generator::push_update(
    ModeID mode,
    bool immediate,
    UpdateRegion region,
    const std::vector<Intensity>& buffer
)
{
    if (buffer.size() != region.width * region.height) {
        return {};
    }

    // The EPD coordinate system is different from the usual reMarkable
    // coordinate system. The EPD’s origin is at the bottom right corner of the
    // usual reMarkable coordinate system, with the X and Y axes swapped and
    // flipped (see the diagram below, representing a tablet in portrait
    // orientation)
    //
    //       ^+--------------+
    //       ||              | - X = epd_width
    //       ||              |
    //       ||              |
    //       ||              |
    //       ||  reMarkable  |
    //       ||              |
    //       ||              |
    //       ||              | ^
    //       ||              | | X = 0
    //       ++--------------+ |
    //         |         <-----+
    //       Y = height . Y = 0

    // Transform from reMarkable coordinates to EPD coordinates:
    // transpose to swap X and Y and flip X and Y

    // TODO: Make this configurable somewhere
    std::vector<Intensity> trans_buffer(buffer.size());
    const auto& dims = this->controller->get_dimensions();

    for (std::size_t k = 0; k < buffer.size(); ++k) {
        std::size_t i = region.height - (k % region.height) - 1;
        std::size_t j = region.width - (k / region.height) - 1;
        trans_buffer[k] = buffer[i * region.width + j] & (intensity_values - 1);
    }

    region = UpdateRegion{
        /* top = */ dims.real_height - region.left - region.width,
        /* left = */ dims.real_width - region.top - region.height,
        /* width = */ region.height,
        /* height = */ region.width
    };

    if (
        region.left >= dims.real_width
        || region.top >= dims.real_height
        || region.left + region.width > dims.real_width
        || region.top + region.height > dims.real_height
    ) {
        return {};
    }

    Update update(mode, immediate, region, std::move(trans_buffer));
    auto id = update.get_id().back();

    {
        std::lock_guard<std::mutex> lock(this->processing_lock);
        this->processing_updates.emplace(id);
    }

    {
        std::lock_guard<std::mutex> lock(this->updates_lock);

        this->pending_updates.emplace(std::move(update));
        this->pending_updates.back().record_enqueue();

        this->updates_cv.notify_one();
    }

    return {id};
}

void Generator::wait_for(UpdateID id)
{
    std::unique_lock<std::mutex> lock(this->processing_lock);
    this->processed_cv.wait(lock, [this, id] {
        return this->processing_updates.count(id) == 0;
    });
}

void Generator::wait_for_all()
{
    std::unique_lock<std::mutex> lock(this->processing_lock);
    this->processed_cv.wait(lock, [this] {
        return this->processing_updates.size() == 0;
    });
}

void Generator::run_generator_thread()
{
    while (!this->stopping_generator) {
        auto maybe_update = this->pop_update();

        if (maybe_update) {
            this->generator_update = std::move(*maybe_update);

            if (this->generator_update.get_immediate()) {
                this->generate_immediate();
            } else {
                this->generate_batch();
            }
        }
    }
}

std::optional<Update> Generator::pop_update()
{
    std::unique_lock<std::mutex> lock(this->updates_lock);
    this->updates_cv.wait(lock, [this] {
        return !this->pending_updates.empty() || this->stopping_generator;
    });

    if (this->stopping_generator) {
        return {};
    }

    auto update = std::move(this->pending_updates.front());
    this->pending_updates.pop();
    update.record_dequeue();
    return {std::move(update)};
}

void Generator::merge_updates()
{
    const auto& dims = this->controller->get_dimensions();
    Update& cur_update = this->generator_update;
    std::lock_guard<std::mutex> lock(this->updates_lock);

    while (!this->pending_updates.empty()) {
        Update& next_update = this->pending_updates.front();

        // Check that update modes are compatible
        if (
            cur_update.get_immediate() != next_update.get_immediate()
            || cur_update.get_mode() != next_update.get_mode()
        ) {
            return;
        }

        if (cur_update.get_immediate()) {
            // Check that the merged update does not change the target value
            // of a pixel which is currently in a transition
            auto next_region = next_update.get_region();
            auto start_offset = next_region.top * dims.real_width
                + next_region.left;
            auto mid_offset = dims.real_width - next_region.width;

            auto* step = this->waveform_steps.data() + start_offset;
            const auto* cand = this->next_intensity.data() + start_offset;
            const auto* next = next_update.get_buffer().data();

            for (
                auto y = next_region.top;
                y < next_region.top + next_region.height;
                ++y
            ) {
                for (
                    auto x = next_region.left;
                    x < next_region.left + next_region.width;
                    ++x
                ) {
                    if (*cand != *next && *step > 0) {
                        return;
                    }
                }

                step += mid_offset;
                cand += mid_offset;
            }
        }

        // Merge updates
        next_update.apply(this->next_intensity.data(), dims.real_width);
        cur_update.merge_with(next_update);
        cur_update.record_dequeue();
        this->pending_updates.pop();
    }
}

UpdateRegion Generator::align_region(UpdateRegion region) const
{
    const auto& dims = this->controller->get_dimensions();
    const auto mask = dims.packed_pixels - 1;

    if ((region.width & mask) == 0 && (region.left & mask) == 0) {
        return region;
    }

    UpdateRegion result = region;

    result.left = region.left & ~mask;
    auto pad_left = region.left & mask;
    result.width = (pad_left + region.width + mask) & ~mask;
    return result;
}

void Generator::generate_batch()
{
    const auto& dims = this->controller->get_dimensions();
    const auto& blank_frame = this->controller->get_blank_frame();

    auto& update = this->generator_update;
    const auto& waveform = this->table->lookup(
        update.get_mode(),
        this->controller->get_temperature()
    );

    this->next_intensity = this->current_intensity;
    update.apply(this->next_intensity.data(), dims.real_width);
    this->merge_updates();

    const auto& region = update.get_region();
    const auto aligned_region = this->align_region(region);

    auto start_offset = aligned_region.top * dims.real_width
        + aligned_region.left;
    auto mid_offset = dims.real_width - aligned_region.width;

    const auto* prev_base = this->current_intensity.data() + start_offset;
    const auto* next_base = this->next_intensity.data() + start_offset;

    this->generate_buffer.clear();
    this->generate_buffer.reserve(waveform.size());

    for (std::size_t k = 0; k < waveform.size(); ++k) {
        update.record_generate_start();
        this->generate_buffer.emplace_back(blank_frame);

        std::uint16_t* data = reinterpret_cast<std::uint16_t*>(
            this->generate_buffer.back().data()
                + (dims.upper_margin + aligned_region.top) * dims.stride
                + (dims.left_margin + aligned_region.left / dims.packed_pixels)
                * dims.depth
        );

        const auto& matrix = waveform[k];
        const auto* prev = prev_base;
        const auto* next = next_base;

        for (
            auto y = aligned_region.top;
            y < aligned_region.top + aligned_region.height;
            ++y
        ) {
            for (
                auto sx = aligned_region.left;
                sx < aligned_region.left + aligned_region.width;
                sx += dims.packed_pixels
            ) {
                std::uint16_t phases = 0;

                for (auto x = sx; x < sx + dims.packed_pixels; ++x) {
                    phases <<= 2;
                    auto phase = matrix[*prev][*next];
                    phases |= static_cast<std::uint8_t>(phase);
                    ++prev;
                    ++next;
                }

                *data = phases;
                data += 2;
            }

            prev += mid_offset;
            next += mid_offset;
            data += (
                dims.stride
                - (aligned_region.width / dims.packed_pixels) * dims.depth
            ) / sizeof(*data);
        }

        update.record_generate_end();
    }

    this->send_frames(/* finalize = */ true);
    this->current_intensity = this->next_intensity;
}

void Generator::generate_immediate()
{
    const auto& dims = this->controller->get_dimensions();
    const auto& blank_frame = this->controller->get_blank_frame();

    Update& update = this->generator_update;
    const auto& waveform = this->table->lookup(
        update.get_mode(),
        this->controller->get_temperature()
    );

    const auto step_count = waveform.size();
    std::fill(this->waveform_steps.begin(), this->waveform_steps.end(), 0);

    this->next_intensity = this->current_intensity;
    update.apply(this->next_intensity.data(), dims.real_width);

    this->generate_buffer.reserve(1);

    auto finished = false;

    while (!finished) {
        finished = true;

        // Merge compatible updates
        this->merge_updates();
        update.record_generate_start();

        // Prepare next frame and advance each pixel step
        this->generate_buffer.clear();
        this->generate_buffer.emplace_back(blank_frame);

        const auto& region = update.get_region();
        const auto aligned_region = this->align_region(region);
        UpdateRegion active_region{};

        std::uint16_t* data = reinterpret_cast<std::uint16_t*>(
            this->generate_buffer.back().data()
                + (dims.upper_margin + aligned_region.top) * dims.stride
                + (dims.left_margin + aligned_region.left / dims.packed_pixels)
                * dims.depth
        );

        auto start_offset = aligned_region.top * dims.real_width
            + aligned_region.left;
        auto mid_offset = dims.real_width - aligned_region.width;

        auto* step = this->waveform_steps.data() + start_offset;
        auto* prev = this->current_intensity.data() + start_offset;
        const auto* next = this->next_intensity.data() + start_offset;

        for (
            auto y = aligned_region.top;
            y < aligned_region.top + aligned_region.height;
            ++y
        ) {
            for (
                auto sx = aligned_region.left;
                sx < aligned_region.left + aligned_region.width;
                sx += dims.packed_pixels
            ) {
                std::uint16_t phases = 0;

                for (auto x = sx; x < sx + dims.packed_pixels; ++x) {
                    phases <<= 2;

                    auto phase = Phase::Noop;

                    if (*prev != *next) {
                        finished = false;

                        // Advance pixel to next step
                        phase = waveform[*step][*prev][*next];
                        active_region.extend(x, y);
                        ++(*step);

                        if (*step == step_count) {
                            // Pixel transition completed: reset to allow
                            // further transitions, and commit final value
                            *step = 0;
                            *prev = *next;
                        }
                    }

                    phases |= static_cast<std::uint8_t>(phase);
                    ++step;
                    ++next;
                    ++prev;
                }

                *data = phases;
                data += 2;
            }

            step += mid_offset;
            prev += mid_offset;
            next += mid_offset;
            data += (
                dims.stride
                - (aligned_region.width / dims.packed_pixels) * dims.depth
            ) / sizeof(*data);
        }

        update.record_generate_end();
        this->send_frames(finished);
        update.set_region(active_region);
    }
}

void Generator::send_frames(bool finalize)
{
    {
        std::unique_lock<std::mutex> lock(this->vsync_write_lock);
        this->vsync_can_write_cv.wait(lock, [this] {
            return this->vsync_can_write || this->stopping_generator;
        });

        if (this->stopping_generator) {
            return;
        }

        if (finalize) {
            this->vsync_update = std::move(this->generator_update);
            this->vsync_finalize = true;
        } else {
            this->vsync_finalize = false;
        }

        std::swap(this->generate_buffer, this->vsync_buffer);
    }

    {
        std::unique_lock<std::mutex> lock(this->vsync_read_lock);
        this->vsync_can_write = false;
        this->vsync_can_read = true;
        this->vsync_can_read_cv.notify_one();
    }
}

void Generator::run_vsync_thread()
{
    while (!this->stopping_vsync) {
        {
            // Wait for the next update to be ready
            std::unique_lock<std::mutex> lock(this->vsync_read_lock);
            const auto pred = [this] {
                return this->vsync_can_read || this->stopping_vsync;
            };

            if (!this->vsync_can_read_cv.wait_for(
                lock, power_off_timeout, pred
            )) {
                // Turn off power to save battery when no updates are coming
                this->controller->set_power(false);
                this->vsync_can_read_cv.wait(lock, pred);
            }
        }

        if (this->stopping_vsync) {
            return;
        }

        this->controller->set_power(true);
        this->controller->get_temperature();

        for (std::size_t k = 0; k < this->vsync_buffer.size(); ++k) {
            if (this->vsync_finalize) {
                this->vsync_update.record_vsync_start();
            } else {
                this->generator_update.record_vsync_start();
            }

            std::copy(
                this->vsync_buffer[k].begin(),
                this->vsync_buffer[k].end(),
                this->controller->get_back_buffer()
            );

            try {
                this->controller->page_flip();
            } catch (const std::exception& e) {
                // Don’t throw here, since we’re inside a background thread
                std::cerr << e.what() << '\n';
                return;
            }

            if (this->vsync_finalize) {
                this->vsync_update.record_vsync_end();
            } else {
                this->generator_update.record_vsync_end();
            }
        }

#ifdef ENABLE_PERF_REPORT
        if (this->vsync_finalize && this->perf_report_stream != nullptr) {
            this->vsync_update.dump_perf_record(*this->perf_report_stream);
        }
#endif

        if (this->vsync_finalize) {
            std::unique_lock<std::mutex> lock(this->processing_lock);

            for (const auto id : this->vsync_update.get_id()) {
                this->processing_updates.erase(id);
            }

            this->processed_cv.notify_one();
        }

        {
            std::unique_lock<std::mutex> lock(this->vsync_write_lock);
            this->vsync_can_write = true;
            this->vsync_can_read = false;
            this->vsync_can_write_cv.notify_one();
        }
    }
}

void Generator::enable_perf_report(std::ostream& out)
{
#ifdef ENABLE_PERF_REPORT
    this->perf_report_stream = &out;
    out << "id,mode,immediate,width,height,enqueue_times,dequeue_times,"
        "generate_start_times,generate_end_times,vsync_start_times,"
        "vsync_end_times\n";
#endif
}

void Generator::disable_perf_report()
{
#ifdef ENABLE_PERF_REPORT
    this->perf_report_stream = nullptr;
#endif
}

} // namespace Waved

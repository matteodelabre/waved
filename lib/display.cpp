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
#include <cerrno>
#include <endian.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

namespace
{

constexpr int fbioblank_off = FB_BLANK_POWERDOWN;
constexpr int fbioblank_on = FB_BLANK_UNBLANK;

}

namespace Waved
{

Display::Display(
    const char* framebuffer_path,
    const char* temperature_sensor_path,
    WaveformTable waveform_table
)
: table(std::move(waveform_table))
, framebuffer_fd(framebuffer_path, O_RDWR)
, temp_sensor_fd(temperature_sensor_path, O_RDONLY)
, current_intensity(epd_size)
, next_intensity(epd_size)
, waveform_steps(epd_size)
{}

auto Display::discover_framebuffer() -> std::optional<std::string>
{
    constexpr auto framebuffer_name = "mxs-lcdif";

    for (const auto& entry : fs::directory_iterator{"/sys/class/graphics"}) {
        std::ifstream name_stream{entry.path() / "name"};
        std::string name;
        std::getline(name_stream, name);

        if (name != framebuffer_name) {
            continue;
        }

        std::ifstream dev_stream{entry.path() / "dev"};
        unsigned int major;
        unsigned int minor;
        char colon;
        dev_stream >> major >> colon >> minor;

        std::string dev_path = "/dev/fb" + std::to_string(minor);

        if (fs::exists(dev_path)) {
            return dev_path;
        }
    }

    return {};
}

auto Display::discover_temperature_sensor() -> std::optional<std::string>
{
    constexpr auto sensor_name = "sy7636a_temperature";

    for (const auto& entry : fs::directory_iterator{"/sys/class/hwmon"}) {
        std::ifstream name_stream{entry.path() / "name"};
        std::string name;
        std::getline(name_stream, name);

        if (name != sensor_name) {
            continue;
        }

        auto sensor_path = entry.path() / "temp0";

        if (fs::exists(sensor_path)) {
            return sensor_path;
        }
    }

    return {};
}

Display::~Display()
{
    this->stop();
}

void Display::start()
{
#ifndef DRY_RUN
    this->set_power(true);
    this->update_temperature();

    if (
        ioctl(
            this->framebuffer_fd,
            FBIOGET_VSCREENINFO,
            &this->var_info
        ) == -1
    ) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Fetch display vscreeninfo"
        );
    }

    if (
        ioctl(
            this->framebuffer_fd,
            FBIOGET_FSCREENINFO,
            &this->fix_info
        ) == -1
    ) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Fetch display fscreeninfo"
        );
    }

    if (
        this->var_info.xres != buf_width
        || this->var_info.yres != buf_height
        || this->var_info.xres_virtual != buf_width
        || this->var_info.yres_virtual !=
            buf_height * buf_total_frames
        || this->fix_info.smem_len < buf_width * buf_height
            * buf_total_frames * buf_depth
    ) {
        throw std::runtime_error("The framebuffer has invalid dimensions");
    }

    // Map the framebuffer to memory
    void* mmap_res = mmap(
        /* addr = */ nullptr,
        /* len = */ this->fix_info.smem_len,
        /* prot = */ PROT_READ | PROT_WRITE,
        /* flags = */ MAP_SHARED,
        /* fd = */ this->framebuffer_fd,
        /* __offset = */ 0
    );

    if (mmap_res == MAP_FAILED)
    {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Map framebuffer to memory"
        );
    }

    this->framebuffer = reinterpret_cast<std::uint8_t*>(mmap_res);
#endif // DRY_RUN

    // Initialize the null frame
    std::uint8_t* null_ptr = this->null_frame.data() + 2;

    // First line
    for (std::size_t i = 0; i < 20; ++i, null_ptr += buf_depth) {
        *null_ptr = 0b01000011;
    }

    for (std::size_t i = 0; i < 20; ++i, null_ptr += buf_depth) {
        *null_ptr = 0b01000111;
    }

    for (std::size_t i = 0; i < 63; ++i, null_ptr += buf_depth) {
        *null_ptr = 0b01000101;
    }

    for (std::size_t i = 0; i < 40; ++i, null_ptr += buf_depth) {
        *null_ptr = 0b01000111;
    }

    for (std::size_t i = 0; i < 117; ++i, null_ptr += buf_depth) {
        *null_ptr = 0b01000011;
    }

    // Second and third lines
    for (std::size_t y = 1; y < 3; ++y) {
        for (std::size_t i = 0; i < 8; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000001;
        }

        for (std::size_t i = 0; i < 11; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01100001;
        }

        for (std::size_t i = 0; i < 36; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000001;
        }

        for (std::size_t i = 0; i < 200; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000011;
        }

        for (std::size_t i = 0; i < 5; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000001;
        }
    }

    // Following lines
    for (std::size_t y = 3; y < buf_height; ++y) {
        for (std::size_t i = 0; i < 8; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000001;
        }

        for (std::size_t i = 0; i < 11; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01100001;
        }

        for (std::size_t i = 0; i < 7; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01000001;
        }

        for (std::size_t i = 0; i < 29; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01010001;
        }

        for (std::size_t i = 0; i < 200; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01010011;
        }

        for (std::size_t i = 0; i < 5; ++i, null_ptr += buf_depth) {
            *null_ptr = 0b01010001;
        }
    }

    // Reset all frames
    for (std::size_t i = 0; i < buf_total_frames; ++i) {
        this->reset_frame(i);
    }

#ifndef DRY_RUN
    // Start the processing threads
    this->stopping_generator = false;
    this->generator_thread = std::thread(&Display::run_generator_thread, this);
    pthread_setname_np(this->generator_thread.native_handle(), "waved_generator");

    this->stopping_vsync = false;
    this->vsync_thread = std::thread(&Display::run_vsync_thread, this);
    pthread_setname_np(this->vsync_thread.native_handle(), "waved_vsync");
#endif // DRY_RUN

    this->started = true;
}

void Display::stop()
{
    if (this->started) {
#ifndef DRY_RUN
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

        if (this->framebuffer != nullptr) {
            munmap(this->framebuffer, this->fix_info.smem_len);
        }
#endif // DRY_RUN

        this->started = false;
    }

    this->set_power(false);
}

void Display::set_power(bool power_state)
{
#ifndef DRY_RUN
    if (power_state != this->power_state) {
        if (
            ioctl(
                this->framebuffer_fd, FBIOBLANK,
                power_state ? fbioblank_on : fbioblank_off
            ) == 0
        ) {
            this->power_state = power_state;
        }
    }
#endif // DRY_RUN
}

void Display::update_temperature()
{
#ifdef DRY_RUN
    int result = 24;
#else
    if (
        chrono::steady_clock::now() - this->temperature_last_read
        <= temperature_read_interval
    ) {
        return;
    }

    char buffer[12];
    ssize_t size = 0;

    if (lseek(this->temp_sensor_fd, 0, SEEK_SET) != 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Seek in panel temperature file"
        );
    }

    size = read(this->temp_sensor_fd, buffer, sizeof(buffer));

    if (size == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Read panel temperature"
        );
    }

    if (static_cast<size_t>(size) >= sizeof(buffer)) {
        buffer[sizeof(buffer) - 1] = '\0';
    } else {
        buffer[size] = '\0';
    }

    int result = std::stoi(buffer);
#endif // DRY_RUN
    this->temperature = result;
    this->temperature_last_read = chrono::steady_clock::now();
}

std::optional<UpdateID> Display::push_update(
    ModeKind mode,
    bool immediate,
    UpdateRegion region,
    const std::vector<Intensity>& buffer
)
{
    return this->push_update(
        this->table.get_mode_id(mode),
        immediate,
        region,
        buffer
    );
}

std::optional<UpdateID> Display::push_update(
    ModeID mode,
    bool immediate,
    UpdateRegion region,
    const std::vector<Intensity>& buffer
)
{
    if (buffer.size() != region.width * region.height) {
        return {};
    }

    // Transform from reMarkable coordinates to EPD coordinates:
    // transpose to swap X and Y and flip X and Y
    std::vector<Intensity> trans_buffer(buffer.size());

    for (std::size_t k = 0; k < buffer.size(); ++k) {
        std::size_t i = region.height - (k % region.height) - 1;
        std::size_t j = region.width - (k / region.height) - 1;
        trans_buffer[k] = buffer[i * region.width + j] & (intensity_values - 1);
    }

    region = UpdateRegion{
        /* top = */ epd_height - region.left - region.width,
        /* left = */ epd_width - region.top - region.height,
        /* width = */ region.height,
        /* height = */ region.width
    };

    if (
        region.left >= epd_width
        || region.top >= epd_height
        || region.left + region.width > epd_width
        || region.top + region.height > epd_height
    ) {
        return {};
    }

    Update update(mode, immediate, region, std::move(trans_buffer));
    auto id = update.get_id().back();

    {
        std::lock_guard<std::mutex> lock(this->processing_lock);
        this->processing_updates.emplace(id);
    }

#ifndef DRY_RUN
    {
        std::lock_guard<std::mutex> lock(this->updates_lock);
#endif

        this->pending_updates.emplace(std::move(update));
        this->pending_updates.back().record_enqueue();

#ifndef DRY_RUN
        this->updates_cv.notify_one();
    }
#endif

#ifdef DRY_RUN
    this->process_update();
#endif

    return {id};
}

void Display::wait_for(UpdateID id)
{
    std::unique_lock<std::mutex> lock(this->processing_lock);
    this->processed_cv.wait(lock, [this, id] {
        return this->processing_updates.count(id) == 0;
    });
}

void Display::wait_for_all()
{
    std::unique_lock<std::mutex> lock(this->processing_lock);
    this->processed_cv.wait(lock, [this] {
        return this->processing_updates.size() == 0;
    });
}

void Display::run_generator_thread()
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

std::optional<Update> Display::pop_update()
{
#ifdef DRY_RUN
    if (this->pending_updates.empty()) {
        return {};
    }
#else
    std::unique_lock<std::mutex> lock(this->updates_lock);
    this->updates_cv.wait(lock, [this] {
        return !this->pending_updates.empty() || this->stopping_generator;
    });

    if (this->stopping_generator) {
        return {};
    }
#endif // DRY_RUN

    auto update = std::move(this->pending_updates.front());
    this->pending_updates.pop();
    update.record_dequeue();
    return {std::move(update)};
}

void Display::merge_updates()
{
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
            auto start_offset = next_region.top * epd_width + next_region.left;
            auto mid_offset = epd_width - next_region.width;

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
        next_update.apply(this->next_intensity.data(), epd_width);
        cur_update.merge_with(next_update);
        cur_update.record_dequeue();
        this->pending_updates.pop();
    }
}

UpdateRegion Display::align_region(UpdateRegion region) const
{
    constexpr auto mask = buf_actual_depth - 1;

    if ((region.width & mask) == 0 && (region.left & mask) == 0) {
        return region;
    }

    UpdateRegion result = region;

    result.left = region.left & ~mask;
    auto pad_left = region.left & mask;
    result.width = (pad_left + region.width + mask) & ~mask;
    return result;
}

void Display::generate_batch()
{
    auto& update = this->generator_update;
    const auto& waveform = this->table.lookup(
        update.get_mode(),
        this->temperature
    );

    this->next_intensity = this->current_intensity;
    update.apply(this->next_intensity.data(), epd_width);
    this->merge_updates();

    const auto& region = update.get_region();
    const auto aligned_region = this->align_region(region);

    auto start_offset = aligned_region.top * epd_width + aligned_region.left;
    auto mid_offset = epd_width - aligned_region.width;

    const auto* prev_base = this->current_intensity.data() + start_offset;
    const auto* next_base = this->next_intensity.data() + start_offset;

    this->generate_buffer.clear();
    this->generate_buffer.reserve(waveform.size());

    for (std::size_t k = 0; k < waveform.size(); ++k) {
        update.record_generate_start();
        this->generate_buffer.emplace_back(this->null_frame);

        std::uint16_t* data = reinterpret_cast<std::uint16_t*>(
            this->generate_buffer.back().data()
                + (margin_top + aligned_region.top) * buf_stride
                + (margin_left + aligned_region.left / buf_actual_depth)
                * buf_depth
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
                sx += buf_actual_depth
            ) {
                std::uint16_t phases = 0;

                for (auto x = sx; x < sx + buf_actual_depth; ++x) {
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
                buf_stride
                - (aligned_region.width / buf_actual_depth) * buf_depth
            ) / sizeof(*data);
        }

        update.record_generate_end();
    }

    this->send_frames(/* finalize = */ true);
    this->current_intensity = this->next_intensity;
}

void Display::generate_immediate()
{
    Update& update = this->generator_update;
    const auto& waveform = this->table.lookup(
        update.get_mode(),
        this->temperature
    );

    const auto step_count = waveform.size();
    std::fill(this->waveform_steps.begin(), this->waveform_steps.end(), 0);

    this->next_intensity = this->current_intensity;
    update.apply(this->next_intensity.data(), epd_width);

    this->generate_buffer.reserve(1);

    auto finished = false;

    while (!finished) {
        finished = true;

        // Merge compatible updates
        this->merge_updates();
        update.record_generate_start();

        // Prepare next frame and advance each pixel step
        this->generate_buffer.clear();
        this->generate_buffer.emplace_back(this->null_frame);

        const auto& region = update.get_region();
        const auto aligned_region = this->align_region(region);
        UpdateRegion active_region{};

        std::uint16_t* data = reinterpret_cast<std::uint16_t*>(
            this->generate_buffer.back().data()
                + (margin_top + aligned_region.top) * buf_stride
                + (margin_left + aligned_region.left / buf_actual_depth)
                * buf_depth
        );

        auto start_offset = aligned_region.top * epd_width + aligned_region.left;
        auto mid_offset = epd_width - aligned_region.width;

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
                sx += buf_actual_depth
            ) {
                std::uint16_t phases = 0;

                for (auto x = sx; x < sx + buf_actual_depth; ++x) {
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
                buf_stride
                - (aligned_region.width / buf_actual_depth) * buf_depth
            ) / sizeof(*data);
        }

        update.record_generate_end();
        this->send_frames(finished);
        update.set_region(active_region);
    }
}

void Display::send_frames(bool finalize)
{
#ifndef DRY_RUN
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
#endif
}

void Display::run_vsync_thread()
{
#ifndef DRY_RUN
    std::size_t next_frame = 0;
    bool first_frame = true;

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
                this->set_power(false);
                this->vsync_can_read_cv.wait(lock, pred);
            }
        }

        if (this->stopping_vsync) {
            return;
        }

        this->set_power(true);
        this->update_temperature();

        for (std::size_t k = 0; k < this->vsync_buffer.size(); ++k) {
            next_frame = (next_frame + 1) % 2;

            if (this->vsync_finalize) {
                this->vsync_update.record_vsync_start();
            } else {
                this->generator_update.record_vsync_start();
            }

            std::memcpy(
                this->framebuffer + next_frame * buf_frame,
                this->vsync_buffer[k].data(),
                this->vsync_buffer[k].size()
            );

            this->var_info.yoffset = next_frame * buf_height;

            if (
                ioctl(
                    this->framebuffer_fd,
                    first_frame
                        // Schedule first frame
                        ? FBIOPUT_VSCREENINFO
                        // Schedule next frame and wait
                        // for vsync of previous frame
                        : FBIOPAN_DISPLAY,
                    &this->var_info
                ) == -1
            ) {
                // Don’t throw here, since we’re inside a background thread
                std::cerr << "Vsync and flip: " << std::strerror(errno) << '\n';
                return;
            }

            if (this->vsync_finalize) {
                this->vsync_update.record_vsync_end();
            } else {
                this->generator_update.record_vsync_end();
            }

            first_frame = false;
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
#endif // DRY_RUN
}

void Display::reset_frame(std::size_t frame_index)
{
#ifndef DRY_RUN
    std::copy(
        this->null_frame.cbegin(),
        this->null_frame.cend(),
        this->framebuffer + buf_frame * frame_index
    );
#endif // DRY_RUN
}

void Display::enable_perf_report(std::ostream& out)
{
#ifdef ENABLE_PERF_REPORT
    this->perf_report_stream = &out;
    out << "id,mode,immediate,width,height,enqueue_times,dequeue_times,"
        "generate_start_times,generate_end_times,vsync_start_times,"
        "vsync_end_times\n";
#endif
}

void Display::disable_perf_report()
{
#ifdef ENABLE_PERF_REPORT
    this->perf_report_stream = nullptr;
#endif
}

} // namespace Waved

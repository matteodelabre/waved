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

#ifdef ENABLE_PERF_REPORT
std::ostream& operator<<(std::ostream& out, chrono::steady_clock::time_point t)
{
    out << chrono::duration_cast<chrono::microseconds>(t.time_since_epoch())
        .count();
    return out;
}
#endif // ENABLE_PERF_REPORT

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

}

namespace Waved
{

UpdateID Display::next_update_id = 0;

Display::Display(
    const char* framebuffer_path,
    const char* temperature_sensor_path,
    WaveformTable waveform_table
)
: table(std::move(waveform_table))
, framebuffer_fd(framebuffer_path, O_RDWR)
, temp_sensor_fd(temperature_sensor_path, O_RDONLY)
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
        this->stopping_generator = true;
        this->updates_cv.notify_one();
        this->generator_thread.join();

        // Terminate the vsync thread
        this->stopping_vsync = true;
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

bool Display::push_update(
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

bool Display::push_update(
    ModeID mode,
    bool immediate,
    UpdateRegion region,
    const std::vector<Intensity>& buffer
)
{
    if (buffer.size() != region.width * region.height) {
        return false;
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
        return false;
    }

#ifndef DRY_RUN
    std::lock_guard<std::mutex> lock(this->updates_lock);
#endif // DRY_RUN

    this->pending_updates.emplace(Update{
        {this->next_update_id++}
        , mode
        , immediate
        , region
        , std::move(trans_buffer)
#ifdef ENABLE_PERF_REPORT
        , /* queue_time = */ chrono::steady_clock::now()
        , /* dequeue_time = */ chrono::steady_clock::now()
        , /* generate_times = */ {}
        , /* vsync_times = */ {}
#endif // ENABLE_PERF_REPORT
    });

#ifndef DRY_RUN
    this->updates_cv.notify_one();
#else
    this->process_update();
#endif // DRY_RUN
    return true;
}

void Display::run_generator_thread()
{
    while (!this->stopping_generator) {
        this->process_update();
    }
}

void Display::process_update()
{
    auto maybe_update = this->pop_update();

    if (maybe_update) {
        auto update = *maybe_update;

        if (update.immediate) {
            this->generate_immediate(update);
        } else {
            this->generate_batch(update);
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

#ifdef ENABLE_PERF_REPORT
    this->generate_update.dequeue_time = chrono::steady_clock::now();
#endif // ENABLE_PERF_REPORT
    return {std::move(update)};
}

void Display::merge_updates(Update& cur_update, IntensityArray& intensity)
{
    std::lock_guard<std::mutex> lock(this->updates_lock);

    while (!this->pending_updates.empty()) {
        Update& next_update = this->pending_updates.front();

        if (!cur_update.merge(next_update)) {
            return;
        }

        next_update.apply(intensity.data(), epd_width);
        this->pending_updates.pop();
    }
}

UpdateRegion Display::align_region(UpdateRegion region)
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

std::vector<bool> Display::check_consecutive(const Update& update)
{
    const auto& region = update.region;
    std::vector<bool> result(region.height * region.width / buf_actual_depth);

    const Intensity* prev_base = this->current_intensity.data()
        + region.top * epd_width
        + region.left;
    const Intensity* next_base = update.buffer.data();

    const Intensity* prev = prev_base;
    const Intensity* next = next_base;

    bool first = true;
    std::array<Intensity, buf_actual_depth> last_prevs;
    std::array<Intensity, buf_actual_depth> last_nexts;
    std::size_t i = 0;

    for (std::size_t y = 0; y < region.height; ++y) {
        for (std::size_t x = 0; x < region.width / buf_actual_depth; ++x) {
            result[i] = (
                !first
                && std::equal(last_prevs.cbegin(), last_prevs.cend(), prev)
                && std::equal(last_nexts.cbegin(), last_nexts.cend(), next)
            );

            first = false;
            std::copy(prev, prev + buf_actual_depth, last_prevs.begin());
            std::copy(next, next + buf_actual_depth, last_nexts.begin());

            prev += buf_actual_depth;
            next += buf_actual_depth;
            ++i;
        }

        prev += epd_width - region.width;
    }

    return result;
}

void Display::generate_batch(Update& update)
{
    const auto& waveform = this->table.lookup(update.mode, this->temperature);

    // Target intensity values
    static IntensityArray next_intensity;
    next_intensity = this->current_intensity;
    update.apply(next_intensity.data(), epd_width);

    std::vector<bool> is_consecutive = this->check_consecutive(update);

    /* this->merge_updates(update); */

    const auto& region = update.region;
    const Intensity* prev_base = this->current_intensity.data()
        + region.top * epd_width
        + region.left;
    const Intensity* next_base = next_intensity.data()
        + region.top * epd_width
        + region.left;

#if ENABLE_PERF_REPORT
    update.generate_times.resize(waveform.size() + 1);
    update.generate_times[0] = chrono::steady_clock::now();
#endif // ENABLE_PERF_REPORT

    this->generate_buffer.clear();
    this->generate_buffer.reserve(waveform.size());

    for (std::size_t k = 0; k < waveform.size(); ++k) {
        this->generate_buffer.emplace_back(this->null_frame);
        std::uint8_t* data = this->generate_buffer.back().data()
            + (margin_top + region.top) * buf_stride
            + (margin_left + region.left / buf_actual_depth) * buf_depth;

        const auto& matrix = waveform[k];
        const Intensity* prev = prev_base;
        const Intensity* next = next_base;

        std::size_t i = 0;
        std::uint8_t byte1 = 0;
        std::uint8_t byte2 = 0;

        for (std::size_t y = 0; y < region.height; ++y) {
            for (std::size_t x = 0; x < region.width / buf_actual_depth; ++x) {
                if (!is_consecutive[i]) {
                    auto phase1 = matrix[*prev++][*next++];
                    auto phase2 = matrix[*prev++][*next++];
                    auto phase3 = matrix[*prev++][*next++];
                    auto phase4 = matrix[*prev++][*next++];
                    auto phase5 = matrix[*prev++][*next++];
                    auto phase6 = matrix[*prev++][*next++];
                    auto phase7 = matrix[*prev++][*next++];
                    auto phase8 = matrix[*prev++][*next++];

                    byte1 = (
                        (static_cast<std::uint8_t>(phase5) << 6)
                        | (static_cast<std::uint8_t>(phase6) << 4)
                        | (static_cast<std::uint8_t>(phase7) << 2)
                        | static_cast<std::uint8_t>(phase8)
                    );

                    byte2 = (
                        (static_cast<std::uint8_t>(phase1) << 6)
                        | (static_cast<std::uint8_t>(phase2) << 4)
                        | (static_cast<std::uint8_t>(phase3) << 2)
                        | static_cast<std::uint8_t>(phase4)
                    );
                } else {
                    prev += buf_actual_depth;
                    next += buf_actual_depth;
                }

                *data++ = byte1;
                *data++ = byte2;
                data += 2;
                ++i;
            }

            prev += epd_width - region.width;
            next += epd_width - region.width;
            data += buf_stride - (region.width / buf_actual_depth) * buf_depth;
        }

#ifdef ENABLE_PERF_REPORT
        update.generate_times[k + 1] = chrono::steady_clock::now();
#endif // ENABLE_PERF_REPORT
    }

    this->send_frames();
    this->current_intensity = next_intensity;
}

void Display::generate_immediate(Update& update)
{
    const auto& waveform = this->table.lookup(update.mode, this->temperature);
    const auto step_count = waveform.size();

    // Tells which “step” of the waveform each pixel is on
    static std::array<std::array<std::size_t, epd_width>, epd_height> steps;
    steps.fill({});

    // Target intensity values
    static IntensityArray next_intensity;
    next_intensity = this->current_intensity;
    update.apply(next_intensity.data(), epd_width);

    this->generate_buffer.reserve(1);
    auto finished = false;

    while (!finished) {
        finished = true;

        // Merge compatible updates
        this->merge_updates(update, next_intensity);

        // Prepare next frame and advance each pixel step
        this->generate_buffer.clear();
        this->generate_buffer.emplace_back(this->null_frame);

#if ENABLE_PERF_REPORT
        update.generate_times.emplace_back(chrono::steady_clock::now());
#endif // ENABLE_PERF_REPORT

        const auto aligned_region = this->align_region(update.region);
        UpdateRegion active_region{};

        std::uint16_t* data = reinterpret_cast<std::uint16_t*>(
            this->generate_buffer.back().data()
                + (margin_top + aligned_region.top) * buf_stride
                + (margin_left + aligned_region.left / buf_actual_depth)
                * buf_depth
        );

        const Intensity* prev = this->current_intensity.data()
            + update.region.top * epd_width + update.region.left;
        const Intensity* next = next_intensity.data()
            + update.region.top * epd_width + update.region.left;

        for (
            std::size_t y = aligned_region.top;
            y < aligned_region.top + aligned_region.height;
            ++y
        ) {
            for (
                std::size_t sx = aligned_region.left;
                sx < aligned_region.left + aligned_region.width;
                sx += buf_actual_depth
            ) {
                std::uint16_t phases = 0;

                for (std::size_t x = sx; x < sx + buf_actual_depth; ++x) {
                    phases <<= 2;

                    if (update.region.contains(x, y)) {
                        auto phase = Phase::Noop;

                        if (steps[y][x] < step_count) {
                            finished = false;
                            phase = waveform[steps[y][x]][*prev][*next];
                            active_region.extend(x, y);
                            steps[y][x]++;
                        }

                        phases |= static_cast<std::uint8_t>(phase);
                        next++;
                        prev++;
                    }

                }

                *data = phases;
                data += 2;
            }

            prev += epd_width - update.region.width;
            next += epd_width - update.region.width;
            data += (
                buf_stride
                - (aligned_region.width / buf_actual_depth) * buf_depth
            ) / sizeof(*data);
        }

        if (finished) {
            break;
        }

        this->send_frames();
        update.region = active_region;
        /* std::cout << "active: top " << update.region.top << " left " << update.region.left << " width " << update.region.width << " height " << update.region.height << '\n'; */
    }

    this->current_intensity = next_intensity;
}

void Display::send_frames()
{
#ifndef DRY_RUN
    {
        std::unique_lock<std::mutex> lock(this->vsync_write_lock);
        this->vsync_can_write_cv.wait(lock, [this] {
            return this->vsync_can_write || this->stopping_generator;
        });
        this->vsync_update = this->generate_update;
        std::swap(this->generate_buffer, this->vsync_buffer);
    }

    this->vsync_can_write = false;
    this->vsync_can_read = true;
    this->vsync_can_read_cv.notify_one();
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

            if (!this->vsync_can_read_cv.wait_for(lock, power_off_timeout, pred)) {
                // Turn off power to save battery when no updates are coming
                this->set_power(false);
                this->vsync_can_read_cv.wait(lock, pred);
            }
        }

        if (this->stopping_vsync) {
            return;
        }

#if ENABLE_PERF_REPORT
        Update& update = this->vsync_update;
        update.vsync_times.resize(this->vsync_buffer.size() + 1);
        update.vsync_times[0] = chrono::steady_clock::now();
#endif // ENABLE_PERF_REPORT

        this->set_power(true);
        this->update_temperature();

        for (std::size_t k = 0; k < this->vsync_buffer.size(); ++k) {
            next_frame = (next_frame + 1) % 2;

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

            first_frame = false;

#ifdef ENABLE_PERF_REPORT
            update.vsync_times[k + 1] = chrono::steady_clock::now();
#endif // ENABLE_PERF_REPORT
        }

#ifdef ENABLE_PERF_REPORT
        this->make_perf_record();
#endif // ENABLE_PERF_REPORT

        this->vsync_can_write = true;
        this->vsync_can_read = false;
        this->vsync_can_write_cv.notify_one();
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

#ifdef ENABLE_PERF_REPORT
void Display::make_perf_record()
{
#ifdef DRY_RUN
    const auto& update = this->generate_update;
    this->perf_report << update.id << ','
        << static_cast<int>(update.mode) << ','
        << update.region.width << ','
        << update.region.height << ','
        << update.queue_time << ','
        << update.dequeue_time << ','
        << update.generate_times << ",\n";
#else
    const auto& update = this->vsync_update;
    this->perf_report << update.id << ','
        << static_cast<int>(update.mode) << ','
        << update.region.width << ','
        << update.region.height << ','
        << update.queue_time << ','
        << update.dequeue_time << ','
        << update.generate_times << ','
        << update.vsync_times << '\n';
#endif // DRY_RUN
}

std::string Display::get_perf_report() const
{
    return (
        "id,mode,width,height,queue_time,dequeue_time,"
        "generate_times,vsync_times\n"
        + this->perf_report.str()
    );
}
#endif // ENABLE_PERF_REPORT

} // namespace Waved

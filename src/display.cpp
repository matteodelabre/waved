/**
 * @file
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.hpp"
#include <system_error>
#include <chrono>
#include <valarray>
#include <cstring>
#include <cmath>
#include <cerrno>
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

    // Start the processing threads
    this->stopping_generator = false;
    this->generator_thread = std::thread(&Display::run_generator_thread, this);
    pthread_setname_np(this->generator_thread.native_handle(), "waved_generator");

    this->stopping_vsync = false;
    this->vsync_thread = std::thread(&Display::run_vsync_thread, this);
    pthread_setname_np(this->vsync_thread.native_handle(), "waved_vsync");

    this->started = true;
}

void Display::stop()
{
    if (this->started) {
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

        this->started = false;
    }

    this->set_power(false);
}

constexpr int fbioblank_off = FB_BLANK_POWERDOWN;
constexpr int fbioblank_on = FB_BLANK_UNBLANK;

void Display::set_power(bool power_state)
{
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
}

void Display::update_temperature()
{
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
    this->temperature = result;
    this->temperature_last_read = chrono::steady_clock::now();
}

bool Display::push_update(
    Mode mode,
    Region region,
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

    region = Region{
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

    std::lock_guard<std::mutex> lock(this->updates_lock);
    this->pending_updates.emplace(Update{
        mode,
        region,
        std::move(trans_buffer)
    });

    this->updates_cv.notify_one();
    return true;
}

void Display::run_generator_thread()
{
    while (!this->stopping_generator) {
        std::optional<Update> maybe_update = this->pop_update();

        if (maybe_update) {
            Update& update = *maybe_update;
            this->align_update(update);
            this->generate_frames(update);
            this->commit_update(update);
        }
    }
}

std::optional<Display::Update> Display::pop_update()
{
    std::unique_lock<std::mutex> lock(this->updates_lock);
    this->updates_cv.wait(lock, [this] {
        return !this->pending_updates.empty() || this->stopping_generator;
    });

    if (this->stopping_generator) {
        return {};
    }

    Update update = std::move(this->pending_updates.front());
    this->pending_updates.pop();
    return update;
}

void Display::align_update(Update& update)
{
    constexpr auto mask = buf_actual_depth - 1;

    if (
        update.region.width & mask == 0
        && update.region.left & mask == 0
    ) {
        return;
    }

    auto aligned_left = update.region.left & ~mask;
    auto pad_left = update.region.left & mask;
    auto new_width = (pad_left + update.region.width + mask) & ~mask;
    auto pad_right = new_width - pad_left - update.region.width;

    std::vector<Intensity> new_buffer(update.region.height * new_width);

    const Intensity* prev = this->current_intensity.data()
        + update.region.top * epd_width
        + aligned_left;

    const Intensity* old_next = update.buffer.data();
    Intensity* new_next = new_buffer.data();

    for (std::size_t y = 0; y < update.region.height; ++y) {
        for (std::size_t x = 0; x < pad_left; ++x) {
            *new_next++ = *prev++;
        }

        for (std::size_t x = 0; x < update.region.width; ++x) {
            *new_next++ = *old_next++;
        }

        prev += update.region.width;

        for (std::size_t x = 0; x < pad_right; ++x) {
            *new_next++ = *prev++;
        }

        prev += epd_width - new_width;
    }

    update.buffer = std::move(new_buffer);
    update.region.left = aligned_left;
    update.region.width = new_width;
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

#ifdef REPORT_PERF
template<typename Value>
static int valarray_avg(const std::valarray<Value>& values)
{
    return values.sum() / values.size();
}

template<typename Value>
static int valarray_stddev(const std::valarray<Value>& values)
{
    int avg = valarray_avg(values);
    int dist = 0;

    for (Value val : values) {
        dist += (val - avg) * (val - avg);
    }

    return std::sqrt(dist / values.size());
}

static void print_frame_timing(std::ostream& out, int timing)
{
    out << "\033[";

    if (timing < 7'500) {
        out << "32"; // green
    } else if (timing < 10'000) {
        out << "33"; // yellow
    } else {
        out << "31"; // red
    }

    out << "m" << std::setw(5) << timing << "\033[0m";
}
#endif

void Display::generate_frames(const Update& update)
{
    std::vector<bool> is_consecutive = this->check_consecutive(update);
    const auto& region = update.region;

    const Intensity* prev_base = this->current_intensity.data()
        + region.top * epd_width
        + region.left;
    const Intensity* next_base = update.buffer.data();
    const Waveform& waveform = this->table.lookup(
        update.mode, this->temperature
    );

#ifdef REPORT_PERF
    static int update_id = 0;
    std::valarray<std::int32_t> timings(waveform.size());
#endif

    this->work_buffer.clear();
    this->work_buffer.reserve(waveform.size());

    for (std::size_t k = 0; k < waveform.size(); ++k) {
#ifdef REPORT_PERF
        const auto start_time = chrono::steady_clock::now();
#endif

        this->work_buffer.emplace_back(this->null_frame);
        std::uint8_t* data = this->work_buffer.back().data()
            + (margin_top + region.top) * buf_stride
            + (margin_left + region.left / buf_actual_depth) * buf_depth;

        const auto& matrix = waveform[k];
        const Intensity* prev = prev_base;
        const Intensity* next = next_base;

        std::size_t i = 0;
        std::uint8_t byte1;
        std::uint8_t byte2;

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
            data += buf_stride - (region.width / buf_actual_depth) * buf_depth;
        }

#ifdef REPORT_PERF
        const auto end_time = chrono::steady_clock::now();
        timings[k] = chrono::duration_cast<chrono::microseconds>(
            end_time - start_time
        ).count();
#endif
    }

#ifdef REPORT_PERF
    std::cerr << "[perf] Update #" << std::setw(3) << update_id << " - ";
    std::cerr << "gen time " << std::setw(7) << timings.sum() << " (frame min ";
    print_frame_timing(std::cerr, timings.min());
    std::cerr << ", avg ";
    print_frame_timing(std::cerr, valarray_avg(timings));
    std::cerr << ", stddev " << valarray_stddev(timings) << ", max ";
    print_frame_timing(std::cerr, timings.max());
    std::cerr << ")\n";
    ++update_id;
#endif

    {
        std::unique_lock<std::mutex> lock(this->live_write_lock);
        this->live_can_write_cv.wait(lock, [this] {
            return this->live_can_write || this->stopping_generator;
        });
        std::swap(this->live_buffer, this->work_buffer);
    }

    this->live_can_write = false;
    this->live_can_read = true;
    this->live_can_read_cv.notify_one();
}

void Display::commit_update(const Update& update)
{
    const auto& region = update.region;

    Intensity* prev = this->current_intensity.data()
        + epd_width * region.top + region.left;
    const Intensity* next = update.buffer.data();

    for (std::size_t i = 0; i < region.height; ++i) {
        std::copy(next, next + region.width, prev);
        prev += epd_width;
        next += region.width;
    }
}

void Display::run_vsync_thread()
{
    std::size_t next_frame = 0;
    bool first_frame = true;

    while (!this->stopping_vsync) {
        {
            // Wait for the next update to be ready
            std::unique_lock<std::mutex> lock(this->live_read_lock);
            const auto pred = [this] {
                return this->live_can_read || this->stopping_vsync;
            };

            if (!this->live_can_read_cv.wait_for(lock, power_off_timeout, pred)) {
                // Turn off power to save battery when no updates are coming
                this->set_power(false);
                this->live_can_read_cv.wait(lock, pred);
            }
        }

        if (this->stopping_vsync) {
            return;
        }

        this->set_power(true);
        this->update_temperature();

        for (std::size_t k = 0; k < this->live_buffer.size(); ++k) {
            next_frame = (next_frame + 1) % 2;

            std::memcpy(
                this->framebuffer + next_frame * buf_frame,
                this->live_buffer[k].data(),
                this->live_buffer[k].size()
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
        }

        this->live_can_write = true;
        this->live_can_read = false;
        this->live_can_write_cv.notify_one();
    }
}

void Display::reset_frame(std::size_t frame_index)
{
    std::copy(
        this->null_frame.cbegin(),
        this->null_frame.cend(),
        this->framebuffer + buf_frame * frame_index
    );
}

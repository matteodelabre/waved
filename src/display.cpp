#include "display.hpp"
#include <system_error>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <bitset>

namespace chrono = std::chrono;

/** Time after which to power off the display if no updates are received. */
constexpr chrono::milliseconds power_off_timeout{1000};

Display::Display()
// TODO: Auto-detect appropriate paths rather than hardcoding them
: Display("/dev/fb0", "/sys/class/hwmon/hwmon1/temp0")
{}

Display::Display(
    const char* framebuffer_path,
    const char* temperature_sensor_path
)
: framebuffer_fd(framebuffer_path, O_RDWR)
, temperature_sensor_fd(temperature_sensor_path, O_RDONLY)
{}

Display::~Display()
{
    if (this->started) {
        this->stopping = true;
        this->generator_thread.join();
        this->vsync_thread.join();
    }

    this->set_power(false);
}

void Display::start()
{
    this->set_power(true);

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
        throw std::runtime_error("The phase buffer has invalid dimensions");
    }

    // Map the phase buffer to memory
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
            "Map phase buffer to memory"
        );
    }

    this->framebuffer = reinterpret_cast<uint8_t*>(mmap_res);

    // Initialize our null-phases buffer
    std::uint8_t* null_ptr = this->null_phases.data() + 2;

    // First line: 43 (x 20) 47 (x 20) 45 (x 63) 47 (x 40) 43 (x 117)
    for (std::size_t i = 0; i < 20; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x43;
    }

    for (std::size_t i = 0; i < 20; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x47;
    }

    for (std::size_t i = 0; i < 63; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x45;
    }

    for (std::size_t i = 0; i < 40; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x47;
    }

    for (std::size_t i = 0; i < 117; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x43;
    }

    // Second line: 41 (x 8) 61 (x 11) 41 (x 36) 43 (x 200) 41 (x 5)
    for (std::size_t i = 0; i < 8; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x41;
    }

    for (std::size_t i = 0; i < 11; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x61;
    }

    for (std::size_t i = 0; i < 36; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x41;
    }

    for (std::size_t i = 0; i < 200; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x43;
    }

    for (std::size_t i = 0; i < 5; ++i, null_ptr += buf_depth) {
        *null_ptr = 0x41;
    }

    // Other lines: 41 (x 8) 61 (x 11) 41 (x 7) 51 (x 29) 53 (x 200) 51 (x 5)
    for (std::size_t i = 2; i < buf_height; ++i) {
        for (std::size_t i = 0; i < 8; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x41;
        }

        for (std::size_t i = 0; i < 11; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x61;
        }

        for (std::size_t i = 0; i < 7; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x41;
        }

        for (std::size_t i = 0; i < 29; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x51;
        }

        for (std::size_t i = 0; i < 200; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x53;
        }

        for (std::size_t i = 0; i < 5; ++i, null_ptr += buf_depth) {
            *null_ptr = 0x51;
        }
    }

    // Start the processing threads
    this->generator_thread = std::thread(&Display::run_generator_thread, this);
    this->vsync_thread = std::thread(&Display::run_vsync_thread, this);
}

constexpr int fbioblank_off = FB_BLANK_POWERDOWN;
constexpr int fbioblank_on = FB_BLANK_UNBLANK;

void Display::set_power(bool power)
{
    if (power != this->power_on) {
        if (
            ioctl(
                this->framebuffer_fd, FBIOBLANK,
                power ? fbioblank_on : fbioblank_off
            ) == 0
        ) {
            this->power_on = power;
        }
    }
}

auto Display::get_temperature() const -> int
{
    if (!this->power_on) {
        return 0;
    }

    if (lseek(this->temperature_sensor_fd, 0, SEEK_SET) != 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Seek in panel temperature file"
        );
    }

    char buffer[12];
    ssize_t size = read(this->temperature_sensor_fd, buffer, sizeof(buffer));

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

    return std::stoi(buffer);
}

void Display::queue_update(Update&& update)
{
    std::lock_guard<std::mutex> lock(this->updates_lock);
    this->pending_updates.emplace(update);
    this->updates_cv.notify_one();
}

void Display::run_generator_thread()
{
    std::size_t next_frame = 0;

    bool has_update = false;
    Update active_update;
    std::size_t current_phase = 0;

    while (!this->stopping) {
        // Current update has finished, update intensity data
        if (has_update && current_phase == active_update.waveform->size()) {
            const auto& region = active_update.region;

            Intensity* to = this->current_intensity.data()
                + screen_width * region.top + region.left;
            Intensity* from = active_update.buffer.data();

            for (std::size_t i = 0; i < region.height; ++i) {
                std::memcpy(to, from, region.width * sizeof(Intensity));
                to += screen_width;
                from += region.width;
            }

            has_update = false;
        }

        // Fetch the next update from the queue
        if (!has_update) {
            std::unique_lock<std::mutex> lock(this->updates_lock);
            this->updates_cv.wait(lock, [this] {
                return !this->pending_updates.empty();
            });

            active_update = std::move(this->pending_updates.front());
            this->pending_updates.pop();
            lock.unlock();

            has_update = true;
            current_phase = 0;
        }

        // Generate the next phase frame
        std::unique_lock<std::mutex> lock(this->frame_locks[next_frame]);
        auto& condition = this->frame_cvs[next_frame];
        auto& ready = this->frame_readiness[next_frame];

        condition.wait(lock, [&ready] { return !ready; });

        std::uint8_t* frame_base
            = this->framebuffer + buf_frame * next_frame;

        std::memcpy(
            frame_base,
            this->null_phases.data(),
            this->null_phases.size()
        );

        const auto& region = active_update.region;
        const auto& matrix = (*active_update.waveform)[current_phase];

        for (std::size_t x = 0; x < region.width; ++x) {
            std::uint8_t* data =
                frame_base + (
                    screen_width - region.left - x - 1
                    + margin_top
                ) * buf_stride
                + buf_depth * margin_left;

            Intensity* prev = this->current_intensity.data() + region.left + x
                + region.height - 1;
            Intensity* next = active_update.buffer.data() + x
                + region.height - 1;

            for (std::size_t y = 0; y < region.height; y += 8, data += 2) {
                auto phase1 = matrix[*prev--][*next--];
                auto phase2 = matrix[*prev--][*next--];
                auto phase3 = matrix[*prev--][*next--];
                auto phase4 = matrix[*prev--][*next--];
                auto phase5 = matrix[*prev--][*next--];
                auto phase6 = matrix[*prev--][*next--];
                auto phase7 = matrix[*prev--][*next--];
                auto phase8 = matrix[*prev--][*next--];

                std::uint8_t byte1 = (
                    (static_cast<std::uint8_t>(phase8) << 6)
                    + (static_cast<std::uint8_t>(phase7) << 4)
                    + (static_cast<std::uint8_t>(phase6) << 2)
                    + static_cast<std::uint8_t>(phase5)
                );

                std::uint8_t byte2 = (
                    (static_cast<std::uint8_t>(phase4) << 6)
                    + (static_cast<std::uint8_t>(phase3) << 4)
                    + (static_cast<std::uint8_t>(phase2) << 2)
                    + static_cast<std::uint8_t>(phase1)
                );

                *data++ = byte1;
                *data++ = byte2;
            }
        }

        ready = true;
        condition.notify_one();

        ++current_phase;
        next_frame = (next_frame + 1) % buf_total_frames;
    }
}

void Display::run_vsync_thread()
{
    std::size_t next_frame = 0;

    while (!this->stopping) {
        std::unique_lock<std::mutex> lock(this->frame_locks[next_frame]);
        auto& condition = this->frame_cvs[next_frame];
        auto& ready = this->frame_readiness[next_frame];
        const auto pred = [&ready] { return ready; };

        if (!condition.wait_for(lock, power_off_timeout, pred)) {
            // Turn off power to save battery when no updates are coming
            this->set_power(false);
            condition.wait(lock, pred);
            this->set_power(true);
        }

        this->var_info.yoffset = next_frame * buf_height;

        if (
            ioctl(
                this->framebuffer_fd,
                FBIOPAN_DISPLAY,
                &this->var_info
            ) == -1
        ) {
            // Don’t throw here, since we’re inside a background thread
            std::cerr << "Vsync error: " << std::strerror(errno) << '\n';
            return;
        }

        ready = false;
        condition.notify_one();
        next_frame = (next_frame + 1) % Display::buf_total_frames;
    }
}

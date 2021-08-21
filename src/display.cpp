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
constexpr chrono::milliseconds power_off_timeout{3000};

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
    this->stop();
}

void Display::start()
{
    this->stopping = false;
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
    for (std::size_t y = 0; y < 2; ++y) {
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

    // Store a null frame as default frame
    this->reset_frame(buf_default_frame);

    // Start the processing threads
    this->generator_thread = std::thread(&Display::run_generator_thread, this);
    pthread_setname_np(this->generator_thread.native_handle(), "waved_generator");

    this->vsync_thread = std::thread(&Display::run_vsync_thread, this);
    pthread_setname_np(this->vsync_thread.native_handle(), "waved_vsync");

    this->started = true;
}

void Display::stop()
{
    if (this->started) {
        this->stopping = true;
        this->generator_thread.join();
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

auto Display::get_temperature() -> int
{
    char buffer[12];
    ssize_t size = 0;

    {
        // The controller needs to be powered on, otherwise
        // we get a temperature reading of 0 °C
        std::lock_guard<std::mutex> lock(this->power_lock);
        bool old_power_state = this->power_state;
        this->set_power(true);

        if (lseek(this->temperature_sensor_fd, 0, SEEK_SET) != 0) {
            this->set_power(old_power_state);
            throw std::system_error(
                errno,
                std::generic_category(),
                "Seek in panel temperature file"
            );
        }

        size = read(this->temperature_sensor_fd, buffer, sizeof(buffer));

        if (size == -1) {
            this->set_power(old_power_state);
            throw std::system_error(
                errno,
                std::generic_category(),
                "Read panel temperature"
            );
        }

        this->set_power(old_power_state);
    }

    if (static_cast<size_t>(size) >= sizeof(buffer)) {
        buffer[sizeof(buffer) - 1] = '\0';
    } else {
        buffer[size] = '\0';
    }

    return std::stoi(buffer);
}

void Display::push_update(Update&& update)
{
    std::lock_guard<std::mutex> lock(this->updates_lock);
    // TODO: Transpose, pad, and flip data
    this->pending_updates.emplace(update);
    this->updates_cv.notify_one();
}

void Display::run_generator_thread()
{
    std::size_t next_frame = 0;

    while (!this->stopping) {
        Update update = this->pop_update();
        this->generate_frames(next_frame, update);
        this->commit_update(update);
    }
}

Display::Update Display::pop_update()
{
    std::unique_lock<std::mutex> lock(this->updates_lock);
    this->updates_cv.wait(lock, [this] {
        return !this->pending_updates.empty();
    });

    Update update = std::move(this->pending_updates.front());
    this->pending_updates.pop();
    lock.unlock();
    return update;
}

std::vector<bool> Display::check_consecutive(const Update& update)
{
    const auto& region = update.region;
    std::vector<bool> result(region.height * region.width / buf_actual_depth);

    const Intensity* prev_base = this->current_intensity.data()
        + region.top * screen_width
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

        prev += screen_width - region.width;
    }

    return result;
}

void Display::generate_frames(std::size_t& next_frame, const Update& update)
{
    std::vector<bool> is_consecutive = this->check_consecutive(update);
    const auto& region = update.region;

    const Intensity* prev_base = this->current_intensity.data()
        + region.top * screen_width
        + region.left;
    const Intensity* next_base = update.buffer.data();

    for (const auto& matrix : *update.waveform) {
        std::unique_lock<std::mutex> lock(this->frame_locks[next_frame]);
        auto& condition = this->frame_cvs[next_frame];
        auto& ready = this->frame_readiness[next_frame];

        condition.wait(lock, [&ready] { return !ready; });

        // We have a time budget of approximately 10 ms to generate each frame
        // otherwise the vsync thread will catch up
        std::uint8_t* frame_base = this->framebuffer + buf_frame * next_frame;
        this->reset_frame(next_frame);

        std::uint8_t* data = frame_base
            + (margin_top + region.top) * buf_stride
            + (margin_left + region.left / buf_actual_depth) * buf_depth;

        const Intensity* prev = prev_base;
        const Intensity* next = next_base;

        std::size_t i = 0;
        std::uint8_t byte1;
        std::uint8_t byte2;

        for (std::size_t y = 0; y < region.height; ++y) {
            for (std::size_t x = 0; x < region.width / 8; ++x) {
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
                        (static_cast<std::uint8_t>(phase1) << 6)
                        | (static_cast<std::uint8_t>(phase2) << 4)
                        | (static_cast<std::uint8_t>(phase3) << 2)
                        | static_cast<std::uint8_t>(phase4)
                    );

                    byte2 = (
                        (static_cast<std::uint8_t>(phase5) << 6)
                        | (static_cast<std::uint8_t>(phase6) << 4)
                        | (static_cast<std::uint8_t>(phase7) << 2)
                        | static_cast<std::uint8_t>(phase8)
                    );
                }

                *data++ = byte1;
                *data++ = byte2;
                data += 2;
                ++i;
            }

            prev += screen_width - region.width;
            data += buf_stride - (region.width / buf_actual_depth) * buf_depth;
        }

        ready = true;
        condition.notify_one();
        next_frame = (next_frame + 1) % buf_usable_frames;
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

void Display::commit_update(const Update& update)
{
    const auto& region = update.region;

    Intensity* prev = this->current_intensity.data()
        + screen_width * region.top + region.left;
    const Intensity* next = update.buffer.data();

    for (std::size_t i = 0; i < region.height; ++i) {
        std::copy(next, next + region.width, prev);
        prev += screen_width;
        next += region.width;
    }
}

void Display::run_vsync_thread()
{
    bool first_frame = true;

    std::size_t cur_frame = 0;
    std::unique_lock<std::mutex> cur_lock;

    std::size_t next_frame = 0;
    std::unique_lock<std::mutex> next_lock(this->frame_locks[next_frame]);

    while (!this->stopping) {
        auto& next_condition = this->frame_cvs[next_frame];
        auto& next_ready = this->frame_readiness[next_frame];
        const auto pred = [&next_ready] { return next_ready; };

        if (!next_condition.wait_for(next_lock, power_off_timeout, pred)) {
            {
                // Turn off power to save battery when no updates are coming
                std::lock_guard<std::mutex> lock(this->power_lock);
                this->set_power(false);
            }

            next_condition.wait(next_lock, pred);
        }

        {
            std::lock_guard<std::mutex> lock(this->power_lock);
            this->set_power(true);
            this->var_info.yoffset = next_frame * buf_height;

            // Wait for vsync and flip before clearing the frame
            if (
                ioctl(
                    this->framebuffer_fd,
                    FBIOPAN_DISPLAY,
                    &this->var_info
                ) == -1
            ) {
                // Don’t throw here, since we’re inside a background thread
                std::cerr << "Vsync and flip: " << std::strerror(errno) << '\n';
                return;
            }
        }

        if (first_frame) {
            first_frame = false;
        } else {
            this->frame_readiness[cur_frame] = false;
            this->frame_cvs[cur_frame].notify_one();
            cur_lock.unlock();
        }

        cur_frame = next_frame;
        cur_lock = std::move(next_lock);

        next_frame = (next_frame + 1) % buf_usable_frames;
        next_lock = std::unique_lock<std::mutex>(this->frame_locks[next_frame]);
    }
}

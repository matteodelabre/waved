/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "controller.hpp"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/fb.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

namespace Waved
{

namespace
{

constexpr int fbioblank_off = FB_BLANK_POWERDOWN;
constexpr int fbioblank_on = FB_BLANK_UNBLANK;

} // anonymous namespace

FramebufferDimensions::FramebufferDimensions(
    std::uint32_t width,
    std::uint32_t depth,
    std::uint32_t packed_pixels,
    std::uint32_t height,
    std::uint32_t frame_count,
    std::uint32_t left_margin,
    std::uint32_t right_margin,
    std::uint32_t upper_margin,
    std::uint32_t lower_margin
)
: width(width)
, depth(depth)
, stride(width * depth)
, packed_pixels(packed_pixels)
, height(height)
, frame_size(stride * height)
, frame_count(frame_count)
, total_size(frame_size * frame_count)
, left_margin(left_margin)
, right_margin(right_margin)
, upper_margin(upper_margin)
, lower_margin(lower_margin)
, real_width((width - left_margin - right_margin) * packed_pixels)
, real_height(height - upper_margin - lower_margin)
{
    this->real_size = this->real_width * this->real_height;
}

Controller Controller::by_name(
    const char* framebuffer_name,
    const char* temperature_sensor_name,
    FramebufferDimensions dimensions
)
{
    // Look for a framebuffer device matching the given name
    std::string framebuffer_path;

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

        auto dev_path = "/dev/fb" + std::to_string(minor);

        if (fs::exists(dev_path)) {
            framebuffer_path = dev_path;
            break;
        }
    }

    if (framebuffer_path.empty()) {
        std::ostringstream message;
        message << "Could not find framebuffer device " << framebuffer_name;
        throw std::runtime_error(message.str());
    }

    // Look for a temperature sensor device matching the given name
    std::string temperature_sensor_path;

    for (const auto& entry : fs::directory_iterator{"/sys/class/hwmon"}) {
        std::ifstream name_stream{entry.path() / "name"};
        std::string name;
        std::getline(name_stream, name);

        if (name != temperature_sensor_name) {
            continue;
        }

        auto dev_path = entry.path() / "temp0";

        if (fs::exists(dev_path)) {
            temperature_sensor_path = dev_path;
            break;
        }
    }

    if (temperature_sensor_path.empty()) {
        std::ostringstream message;
        message << "Could not find temperature sensor device "
            << temperature_sensor_name;
        throw std::runtime_error(message.str());
    }

    return Controller(
        framebuffer_path.c_str(),
        temperature_sensor_path.c_str(),
        std::move(dimensions)
    );
}

Controller Controller::open_remarkable2()
{

    return Controller::by_name(
        /* framebuffer_name = */ "mxs-lcdif",
        /* temperature_sensor_name = */ "sy7636a_temperature",
        FramebufferDimensions{
            /* width = */ 260,
            /* depth = */ 4,
            /* packed_pixels = */ 8,
            /* height = */ 1408,
            /* frame_count = */ 17,
            /* left_margin = */ 26,
            /* right_margin = */ 0,
            /* upper_margin = */ 3,
            /* lower_margin = */ 1
        }
    );
}

Controller::Controller(
    const char* framebuffer_path,
    const char* temperature_sensor_path,
    FramebufferDimensions dimensions
)
: framebuffer_fd(framebuffer_path, O_RDWR)
, temp_sensor_fd(temperature_sensor_path, O_RDONLY)
, dims(std::move(dimensions))
, blank_frame(dims.frame_size, 0)
{}

Controller::~Controller()
{
    this->stop();
}

void Controller::start()
{
    this->set_power(true);
    this->get_temperature();

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
            "Get framebuffer vscreeninfo"
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
            "Get framebuffer fscreeninfo"
        );
    }

    if (
        this->var_info.xres != this->dims.width
        || this->var_info.yres != this->dims.height
        || this->var_info.xres_virtual != this->dims.width
        || this->var_info.yres_virtual !=
            this->dims.height * this->dims.frame_count
        || this->fix_info.smem_len < this->dims.total_size
    ) {
        throw std::runtime_error{"The framebuffer has invalid dimensions"};
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

    this->scanout_fb = reinterpret_cast<std::uint8_t*>(mmap_res);

    // Prepare blank frame by setting sync flags on the third byte
    // of every pixel
    // FIXME: Not really sure the constant names are correct
    // TODO: Generalize to other devices?
    constexpr std::uint8_t frame_sync = 0x1;
    constexpr std::uint8_t frame_begin = 0x2;
    constexpr std::uint8_t frame_data = 0x4;
    constexpr std::uint8_t frame_end = 0x8;
    constexpr std::uint8_t line_sync = 0x10;
    constexpr std::uint8_t line_begin = 0x20;
    constexpr std::uint8_t line_data = 0x40;
    constexpr std::uint8_t line_end = 0x80;

    auto* data = this->blank_frame.data() + 2;

    // First line
    for (std::size_t i = 0; i < 20; ++i, data += this->dims.depth) {
        *data = frame_sync | frame_begin | line_data;
    }

    for (std::size_t i = 0; i < 20; ++i, data += this->dims.depth) {
        *data = frame_sync | frame_begin | frame_data | line_data;
    }

    for (std::size_t i = 0; i < 63; ++i, data += this->dims.depth) {
        *data = frame_sync | frame_data | line_data;
    }

    for (std::size_t i = 0; i < 40; ++i, data += this->dims.depth) {
        *data = frame_sync | frame_begin | frame_data | line_data;
    }

    for (std::size_t i = 0; i < 117; ++i, data += this->dims.depth) {
        *data = frame_sync | frame_begin | line_data;
    }

    // Second and third lines
    for (std::size_t y = 1; y < 3; ++y) {
        for (std::size_t i = 0; i < 8; ++i, data += this->dims.depth) {
            *data = frame_sync | line_data;
        }

        for (std::size_t i = 0; i < 11; ++i, data += this->dims.depth) {
            *data = frame_sync | line_begin | line_data;
        }

        for (std::size_t i = 0; i < 36; ++i, data += this->dims.depth) {
            *data = frame_sync | line_data;
        }

        for (std::size_t i = 0; i < 200; ++i, data += this->dims.depth) {
            *data = frame_sync | frame_begin | line_data;
        }

        for (std::size_t i = 0; i < 5; ++i, data += this->dims.depth) {
            *data = frame_sync | line_data;
        }
    }

    // Following lines
    for (std::size_t y = 3; y < this->dims.height; ++y) {
        for (std::size_t i = 0; i < 8; ++i, data += this->dims.depth) {
            *data = frame_sync | line_data;
        }

        for (std::size_t i = 0; i < 11; ++i, data += this->dims.depth) {
            *data = frame_sync | line_begin | line_data;
        }

        for (std::size_t i = 0; i < 7; ++i, data += this->dims.depth) {
            *data = frame_sync | line_data;
        }

        for (std::size_t i = 0; i < 29; ++i, data += this->dims.depth) {
            *data = frame_sync | line_sync | line_data;
        }

        for (std::size_t i = 0; i < 200; ++i, data += this->dims.depth) {
            *data = frame_sync | frame_begin | line_sync | line_data;
        }

        for (std::size_t i = 0; i < 5; ++i, data += this->dims.depth) {
            *data = frame_sync | line_sync | line_data;
        }
    }

    // Initialize framebuffer with blank frames
    std::uint8_t* fb_data = this->scanout_fb;

    for (std::size_t frame = 0; frame < this->dims.frame_count; ++frame) {
        std::copy(
            this->blank_frame.begin(),
            this->blank_frame.end(),
            fb_data
        );
        data += this->dims.frame_size;
    }
}

void Controller::stop()
{
    if (this->scanout_fb) {
        munmap(this->scanout_fb, this->fix_info.smem_len);
        this->scanout_fb = nullptr;
    }

    this->set_power(false);
}

int Controller::get_temperature()
{
    auto now = chrono::steady_clock::now();

    if (
        now - this->temperature_last_read > temperature_read_interval
        && this->power_state
    ) {
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

    return this->temperature;
}

const FramebufferDimensions& Controller::get_dimensions() const
{
    return this->dims;
}

const std::vector<std::uint8_t>& Controller::get_blank_frame() const
{
    return this->blank_frame;
}

void Controller::set_power(bool power_state)
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

    if (!this->power_state) {
        this->front_buffer_index = -1;
    }
}

std::uint8_t* Controller::get_back_buffer()
{
    return this->scanout_fb + this->back_buffer_index * this->dims.frame_size;
}

void Controller::page_flip()
{
    this->var_info.yoffset = this->back_buffer_index * this->dims.height;
    unsigned long request = 0;

    if (this->front_buffer_index == -1) {
        // Schedule first frame
        request = FBIOPUT_VSCREENINFO;
    } else {
        // Schedule next frame and wait for vsync interval
        request = FBIOPAN_DISPLAY;
    }

    if (ioctl(this->framebuffer_fd, request, &this->var_info) == -1) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "Page flip"
        );
    }

    this->front_buffer_index = this->back_buffer_index;
    this->back_buffer_index = (this->front_buffer_index + 1) % 2;
}

} // namespace Waved

/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_CONTROLLER_HPP
#define WAVED_CONTROLLER_HPP

#include "file_descriptor.hpp"
#include <cstdint>
#include <chrono>
#include <vector>
#include <linux/fb.h>

namespace Waved
{

/** Specifies the framebuffer dimensions and margins. */
struct FramebufferDimensions
{
    // Number of pixels in a frame line
    std::uint32_t width;

    // Number of bytes per frame pixel
    std::uint32_t depth;

    // Number of bytes per frame line
    std::uint32_t stride;

    // Number of actual pixels packed inside a frame pixel
    std::uint32_t packed_pixels;

    // Number of lines in a frame of the framebuffer
    std::uint32_t height;

    // Number of bytes per frame
    std::uint32_t frame_size;

    // Number of frames allocated in the framebuffer
    std::uint32_t frame_count;

    // Number of available bytes in the framebuffer
    std::uint32_t total_size;

    // Blanking margins in each frame
    std::uint32_t left_margin;
    std::uint32_t right_margin;
    std::uint32_t upper_margin;
    std::uint32_t lower_margin;

    // Number of usable pixels in a line
    std::uint32_t real_width;

    // Number of usable lines in a frame
    std::uint32_t real_height;

    // Number of usable pixels in a frame
    std::uint32_t real_size;

    FramebufferDimensions(
        std::uint32_t width,
        std::uint32_t depth,
        std::uint32_t packed_pixels,
        std::uint32_t height,
        std::uint32_t frame_count,
        std::uint32_t left_margin,
        std::uint32_t right_margin,
        std::uint32_t upper_margin,
        std::uint32_t lower_margin
    );
};

/**
 * Interface to the EPD controller.
 *
 * This class provides an interface to the EPD controller, allowing to power it
 * on and off, to read the current temperature, to access the back buffer, and
 * to flip the back and front buffers. It assumes exclusive access to the
 * controller. Concurrent access will lead to unpredictable behavior.
 */
class Controller
{
public:
    /**
     * Open a controller with the given device paths.
     *
     * @param framebuffer_path Path to the framebuffer device.
     * @param temperature_sensor_path Path to the temperature sensor device.
     * @param dimensions Framebuffer dimensions.
     */
    Controller(
        const char* framebuffer_path,
        const char* temperature_sensor_path,
        FramebufferDimensions dimensions
    );

    // No copies, only allow moves
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller(Controller&&) = default;
    Controller& operator=(Controller&&) = default;

    /**
     * Open a controller by looking up devices by their names.
     *
     * @param framebuffer_name Name of the framebuffer device.
     * @param temperature_sensor_name Name of the temperature sensor device.
     * @param dimensions Framebuffer dimensions.
     * @throws std::runtime_error If no device with the given name can be found.
     */
    static Controller by_name(
        const char* framebuffer_name,
        const char* temperature_sensor_name,
        FramebufferDimensions dimensions
    );

    /** Open the reMarkable 2 controller. */
    static Controller open_remarkable2();

    /** Close the controller. */
    ~Controller();

    /** Start the controller. */
    void start();

    /** Stop the controller. */
    void stop();

    /** Get the current panel temperature. */
    int get_temperature();

    /** Get the framebuffer dimensions. */
    const FramebufferDimensions& get_dimensions() const;

    /** Get a blank frame. */
    const std::vector<std::uint8_t>& get_blank_frame() const;

    /** Turn the controller on or off. */
    void set_power(bool power_state);

    /** Access the back buffer. */
    std::uint8_t* get_back_buffer();

    /**
     * Wait for next vsync interval and swap the back and front buffers.
     *
     * @throws std::system_error If the page flip failed.
     */
    void page_flip();

private:
    // File descriptor for the scanout framebuffer device
    FileDescriptor framebuffer_fd;

    // Linux structures containing framebuffer information
    fb_var_screeninfo var_info{};
    fb_fix_screeninfo fix_info{};

    // Framebuffer dimensions
    FramebufferDimensions dims;

    // Pointer to the mmap’ed scanout framebuffer
    std::uint8_t* scanout_fb = nullptr;

    // Frame index currently used as the front buffer
    std::size_t front_buffer_index = -1;

    // Frame index currently used as the back buffer
    std::size_t back_buffer_index = 0;

    // Frame that leaves the display contents unchanged
    std::vector<std::uint8_t> blank_frame;

    // True if the display controller is powered on
    bool power_state = false;

    // File descriptor for the temperature sensor device
    FileDescriptor temp_sensor_fd;

    // Interval at which to take readings of the panel temperature
    static constexpr std::chrono::seconds temperature_read_interval{30};

    // Last panel temperature reading value and timestamp
    int temperature = 0;
    std::chrono::steady_clock::time_point temperature_last_read;
}; // class Controller

} // namespace Waved

#endif // WAVED_CONTROLLER_HPP

/**
 * @file
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_DISPLAY_HPP
#define WAVED_DISPLAY_HPP

#include "defs.hpp"
#include "file_descriptor.hpp"
#include "waveform_table.hpp"
#include <atomic>
#include <optional>
#include <array>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include <linux/fb.h>

/** Time after which to switch the controller off if no updates are received. */
constexpr std::chrono::milliseconds power_off_timeout{3000};

/** Interval at which to take readings of the panel temperature. */
constexpr std::chrono::seconds temp_read_interval{30};

/**
 * Interface for the display controller.
 *
 * This class processes update regions and drives the display controller with
 * appropriate waveforms to display the wanted intensities. It assumes it has
 * exclusive access to the controller, concurrent access will lead to
 * unpredictable behavior.
 */
class Display
{
public:
    /**
     * Open a display with given device paths.
     *
     * @param framebuffer_path Path to the framebuffer device.
     * @param temperature_sensor_path Path to the temperature sensor file.
     * @param waveform_table Display-specific waveform data.
     */
    Display(
        const char* framebuffer_path,
        const char* temperature_sensor_path,
        WaveformTable waveform_table
    );

    /** Discover the path to framebuffer device. */
    static std::optional<std::string> discover_framebuffer();

    /** Discover the path to the temperature sensor file. */
    static std::optional<std::string> discover_temperature_sensor();

    ~Display();

    /**
     * Start processing updates.
     *
     * This method will power on the display controller and process updates
     * added to the queue using `push_update()` continuously from a background
     * thread. If no updates are received for `power_off_timeout` ticks, the
     * controller is switched off to save power. Calling `stop()` or destroying
     * this object will stop the background threads.
     */
    void start();

    /** Stop processing updates. */
    void stop();

    /**
     * Add an update to the queue.
     *
     * @param mode Update mode to use.
     * @param region Coordinates of the region affected by the update.
     * @param buffer New values for the pixels in the updated region.
     * @param waveform Waveform to use for updating pixels. The waveform object
     * must live until this update is processed, which can take some time.
     * @return True if the update was pushed, false if it was deemed invalid.
     */
    bool push_update(
        Mode mode,
        Region region,
        const std::vector<Intensity>& buffer
    );

private:
    // Display-specific waveform information
    WaveformTable table;

    // True if the processing threads have been started
    bool started = false;

    // Signals that the generator and vsync threads need to stop
    std::atomic<bool> stopping_generator = false;
    std::atomic<bool> stopping_vsync = false;

    // File descriptor for the framebuffer device
    FileDescriptor framebuffer_fd;

    // Pointer to the mmap’ed framebuffer
    std::uint8_t* framebuffer = nullptr;

    // True if the display is powered on
    bool power_state = false;
    std::mutex power_lock;

    /** Turn the display controller on or off. */
    void set_power(bool power_state);

    // File descriptor for reading the panel temperature
    FileDescriptor temp_sensor_fd;

    // Last panel temperature reading and timestamp
    int temp_value = 0;
    std::chrono::steady_clock::time_point temp_read_time;

    /** Get the panel temperature. */
    int get_temperature();

    // Structures used for communicating with the display controller
    fb_var_screeninfo var_info{};
    fb_fix_screeninfo fix_info{};

    // Fixed sizes for the framebuffer. Ideally, those sizes would be provided
    // by var_info and fix_info above so we don’t have to hardcode them, but
    // unfortunately they don’t match exactly

    // Number of pixels in a row of the buffer
    static constexpr std::uint32_t buf_width = 260;

    // Number of bytes per pixel. The first two bytes of each pixel contain
    // data for 8 actual display pixels (2 bits per pixel). The third byte
    // contains a fixed value whose role is unclear (probably some sync
    // markers). The fourth byte is null
    static constexpr std::uint32_t buf_depth = 4;

    // Number of bytes per row
    static constexpr std::uint32_t buf_stride = buf_width * buf_depth;

    // Number of actual display pixels in each buffer pixel (see above)
    static constexpr std::uint32_t buf_actual_depth = 8;

    // Number of rows in the screen
    static constexpr std::uint32_t buf_height = 1408;

    // Total size of a frame in bytes
    static constexpr std::uint32_t buf_frame = buf_stride * buf_height;

    // Number of frames held in the buffer. The buffer contains more space
    // than is needed for holding a single frame. This extra space is used to
    // prepare the upcoming frames while the current frame is being sent to
    // the display controller
    static constexpr std::uint32_t buf_total_frames = 17;

    // The MXSFB driver automatically flips to the last frame of the buffer
    // after each vsync (unless we ask for another flip within the next
    // vsync interval). By storing a null frame at this default location, we
    // ensure that a charge is never applied for too long on the EPD, even
    // if there is a bug in our program. This feature is called “prevent frying
    // pan” mode in the MXSFB kernel driver
    static constexpr std::uint32_t buf_default_frame = 16;

    // Number of usable frames, excluding the default frame which
    // we shouldn’t change for reasons stated above
    static constexpr std::uint32_t buf_usable_frames = 16;

    // State of each frame in the buffer. True if the frame contains data
    // ready to be sent to the controller, false otherwise
    std::array<bool, buf_total_frames> frame_readiness;

    // Set of condition variables used to notify threads when the readiness
    // state of a frame changes
    std::array<std::condition_variable, buf_total_frames> frame_cvs;

    // Set of mutexes used to lock access to each frame
    std::array<std::mutex, buf_total_frames> frame_locks;

    // Margins of unused pixels in each frame of the buffer
    static constexpr std::uint32_t margin_top = 3;
    static constexpr std::uint32_t margin_bottom = 1;
    static constexpr std::uint32_t margin_left = 26;
    static constexpr std::uint32_t margin_right = 0;

    // Visible screen size. This is expressed in the EPD coordinate system,
    // whose origin is at the bottom right corner of the usual reMarkable
    // coordinate system, with the X and Y axes swapped and flipped
    // (see the diagram below, representing a tablet in portrait orientation)
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
    static constexpr std::uint32_t epd_width
        = (buf_width - margin_left - margin_right) * buf_actual_depth;
    static constexpr std::uint32_t epd_height
        = buf_height - margin_top - margin_bottom;
    static constexpr std::uint32_t epd_size = epd_width * epd_height;

    // A static buffer that contains pixels with bytes 1, 2, and 4 set to zero
    // and byte 3 set to its appropriate fixed value. Used for resetting a
    // frame when preparing it
    std::array<std::uint8_t, buf_frame> null_frame{};

    // Buffer holding the current known intensity state of all display cells
    std::array<Intensity, epd_size> current_intensity{};

    /** Information about a pending display update. */
    struct Update
    {
        // Update mode
        Mode mode;

        // Coordinates of the region affected by the update
        Region region{};

        // Buffer containing the new intensities of the region
        std::vector<Intensity> buffer;
    };

    // Queue of pending updates
    std::queue<Update> pending_updates;
    std::condition_variable updates_cv;
    std::mutex updates_lock;

    /** Thread that processes update requests and generates frames. */
    std::thread generator_thread;
    void run_generator_thread();

    /** Remove the next update from the queue (or wait if queue is empty). */
    std::optional<Update> pop_update();

    /** Align an update on a 8-pixel boundary on the X axis. */
    void align_update(Update& update);

    /** Scan an update to find pixel transitions equal to their predecessor. */
    std::vector<bool> check_consecutive(const Update& update);

    /** Prepare phase frames for the given update. */
    void generate_frames(std::size_t& next_frame, const Update& update);

    /** Store a null frame at the given buffer location. */
    void reset_frame(std::size_t frame_index);

    /** Update current_intensity status with the given finished update. */
    void commit_update(const Update& update);

    /** Thread that sends ready frames to the display controller via vsync. */
    std::thread vsync_thread;
    void run_vsync_thread();
};

#endif // WAVED_DISPLAY_HPP

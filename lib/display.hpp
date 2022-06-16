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
#include <sstream>
#include <cstdint>
#include <vector>
#include <linux/fb.h>

namespace Waved
{

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
     * Open a display with the given device information.
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

    /** Discover the path to the framebuffer device. */
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
     * this object will stop the background threads and updates remaining
     * in the queue will not be processed.
     */
    void start();

    /** Stop processing updates. */
    void stop();

    /**
     * Add an update to the queue.
     *
     * @param mode Update mode to use (ID or kind).
     * @param region Coordinates of the region affected by the update.
     * @param buffer New values for the pixels in the updated region.
     * @return True if the update was pushed, false if it was deemed invalid.
     */
    bool push_update(
        ModeKind mode,
        Region region,
        const std::vector<Intensity>& buffer
    );
    bool push_update(
        ModeID mode,
        Region region,
        const std::vector<Intensity>& buffer
    );

#ifdef ENABLE_PERF_REPORT
    /**
     * Get the performance report for past updates as a CSV string.
     *
     * The CSV document will contain one row per processed update (or batch of
     * updates merged together), with the following information:
     *
     * ids - Unique IDs of updates in this batch
     * mode - Update mode used
     * width -  Width of the update rectangle
     * height - Height of the update rectangle
     * queue_time - Timestamp when the update was queued
     * dequeue_time - Timestamp when the update started being processed
     * generate_times - List of timestamps when each frame generation
     *     was finished
     * vsync_times - List of timestamps when each frame vsync was finished
     *
     * Fields that contain a variable number of values (ids, generate_times,
     * and vsync_times) are colon-separated.
     */
    std::string get_perf_report() const;
#endif

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

    // Time after which to switch the controller off if no updates are received
    static constexpr std::chrono::milliseconds power_off_timeout{3000};

    // True if the display is powered on
    bool power_state = false;

    /** Turn the display controller on or off. */
    void set_power(bool power_state);

    // Interval at which to take readings of the panel temperature
    static constexpr std::chrono::seconds temperature_read_interval{30};

    // File descriptor for reading the panel temperature
    FileDescriptor temp_sensor_fd;

    // Last panel temperature reading and timestamp
    int temperature = 0;
    std::chrono::steady_clock::time_point temperature_last_read;

    /** Update the panel temperature reading, if needed. */
    void update_temperature();

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

    // Buffer type for a single frame
    using Frame = std::array<std::uint8_t, buf_frame>;

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

    // Buffer holding the current known intensity state of all display cells
    std::array<Intensity, epd_size> current_intensity{};

    /** Identifier for an update being processed. */
    using UpdateID = std::uint32_t;

    static UpdateID next_update_id;

    /** Information about a display update being processed. */
    struct Update
    {
        // List of IDs for this update. Usually contains just a single ID,
        // but can contain more if several updates are merged together
        std::vector<UpdateID> id;

        // Update mode
        ModeID mode;

        // Coordinates of the region affected by the update
        Region region{};

        // Buffer containing the new intensities of the region
        std::vector<Intensity> buffer;

#ifdef ENABLE_PERF_REPORT
        // Time of creation and addition to the update queue
        std::chrono::steady_clock::time_point queue_time;

        // Time of removal from the update queue
        std::chrono::steady_clock::time_point dequeue_time;

        // Frame generation start time and individual frame end times
        std::vector<std::chrono::steady_clock::time_point> generate_times;

        // Vsync start time and individual frame end times
        std::vector<std::chrono::steady_clock::time_point> vsync_times;
#endif // ENABLE_PERF_REPORT
    };

    // Queue of pending updates
    std::queue<Update> pending_updates;
    std::condition_variable updates_cv;
    std::mutex updates_lock;

    // Frame that leaves cell intensities unchanged
    Frame null_frame{};

    // Buffer to which to generator thread will write to generate frames
    // for each update
    std::vector<Frame> generate_buffer{};

    // Update for which frames are currently being generated
    Update generate_update;

    // Buffer from which the vsync thread will read to send frames
    // to the display
    std::vector<Frame> vsync_buffer{};

    // Update for which frames are currently being vsynced
    Update vsync_update;

    // Locks to read from or write to the vsync buffer
    std::atomic<bool> vsync_can_read = false;
    std::condition_variable vsync_can_read_cv;
    std::mutex vsync_read_lock;

    std::atomic<bool> vsync_can_write = true;
    std::condition_variable vsync_can_write_cv;
    std::mutex vsync_write_lock;

#ifdef ENABLE_PERF_REPORT
    std::ostringstream perf_report;
#endif

    /** Thread that processes update requests and generates frames. */
    std::thread generator_thread;
    void run_generator_thread();

    /** Wait for the next update to be added to the queue and process it. */
    void process_update();

    /**
     * Remove the next update from the queue (or wait if queue is empty).
     *
     * The new update is placed in `generate_update`.
     *
     * @return True if a new update is available, false if the generator
     * thread should stop.
     */
    bool pop_update();

    /**
     * Try to merge the next update from the queue into the current update.
     *
     * Two updates can be merged if they bear the same update mode.
     * This assumes that a lock on updates_lock is already held by
     * the current thread.
     *
     * @return True if an update was merged into the current update,
     * false otherwise.
     */
    bool merge_update();

    /** Align the current update on a 8-pixel boundary on the X axis. */
    void align_update();

    /** Scan update to find pixel transitions equal to their predecessor. */
    std::vector<bool> check_consecutive();

    /** Prepare phase frames for the current update. */
    void generate_frames();

    /** Store a null frame at the given buffer location. */
    void reset_frame(std::size_t frame_index);

    /** Update current_intensity status with the current update. */
    void commit_update();

    /** Thread that sends ready frames to the display controller via vsync. */
    std::thread vsync_thread;
    void run_vsync_thread();

#ifdef ENABLE_PERF_REPORT
    /** Add perf report record for finished update. */
    void make_perf_record();
#endif // ENABLE_PERF_REPORT
}; // class Display

} // namespace Waved

#endif // WAVED_DISPLAY_HPP

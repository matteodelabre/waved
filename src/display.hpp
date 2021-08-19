#ifndef WAVED_DISPLAY_HPP
#define WAVED_DISPLAY_HPP

#include "defs.hpp"
#include "file_descriptor.hpp"
#include <array>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include <linux/fb.h>

class Display
{
public:
    /** Open the display with auto-detected device paths. */
    Display();

    /**
     * Open a display with given device paths.
     *
     * @param framebuffer_path Path to the framebuffer device.
     * @param temperature_sensor_path Path to the temperature sensor device.
     */
    Display(const char* framebuffer_path, const char* temperature_sensor_path);

    /** Stop operating the display and free up resources. */
    ~Display();

    /** Initialize resources and start the processing threads. */
    void start();

    /**
     * Change the power status of the display.
     *
     * Check `get_power()` to see if the operation succeeded.
     *
     * @param power True to power the display on, false to power it off.
     */
    void set_power(bool power);

    /** Get the current power status of the display. */
    bool get_power() const;

    /**
     * Get the current display temperature in Celsius.
     *
     * This will report 0 if the display is powered off.
     */
    int get_temperature() const;

    /** Information about a display update to be performed. */
    struct Update
    {
        // Coordinates of the region affected by the update
        Region region{};

        // Buffer containing the new intensities of the region
        std::vector<Intensity> buffer;

        // Pointer to the waveform to use for the update
        const Waveform* waveform = nullptr;
    };

    /** Add an update to the queue. */
    void queue_update(Update&& update);

private:
    // True if the processing threads have been started
    bool started = false;

    // True if the processing threads need to stop
    bool stopping = false;

    // File descriptor for the framebuffer
    FileDescriptor framebuffer_fd;

    // Pointer to the mmap’ed framebuffer
    std::uint8_t* framebuffer = nullptr;

    // True if the display is powered on
    bool power_on = false;

    // File descriptor for reading the panel temperature
    FileDescriptor temperature_sensor_fd;

    // Structures used for communicating with the display controller
    fb_var_screeninfo var_info{};
    fb_fix_screeninfo fix_info{};

    // Fixed sizes for the framebuffer. Ideally, those sizes would be provided
    // by var_info and fix_info above so we don’t have to hardcode them, but
    // unfortunately they don’t

    // Number of pixels in a row of the buffer
    static constexpr std::size_t buf_width = 260;

    // Number of bytes per pixel. The first two bytes of each pixel contain
    // data for 8 actual display pixels (2 bits per pixel). The third byte
    // contains a fixed value whose role is unclear. The fourth byte is null
    static constexpr std::size_t buf_depth = 4;

    // Number of bytes per row
    static constexpr std::size_t buf_stride = buf_width * buf_depth;

    // Number of actual display pixels in each buffer pixel (see above)
    static constexpr std::size_t buf_actual_depth = 8;

    // Number of rows in the screen
    static constexpr std::size_t buf_height = 1408;

    // Total size of a frame in bytes
    static constexpr std::size_t buf_frame = buf_stride * buf_height;

    // Number of frames held in the buffer. The buffer contains more space
    // than is needed for holding a single frame. This extra space is used to
    // prepare the upcoming frames while the current frame is being sent to
    // the display controller
    static constexpr std::size_t buf_total_frames = 17;

    // State of each frame in the buffer. True if the frame contains data
    // ready to be sent to the controller, false otherwise
    std::array<bool, buf_total_frames> frame_readiness;

    // Set of condition variables used to notify threads when the readiness
    // state of a frame changes
    std::array<std::condition_variable, buf_total_frames> frame_cvs;

    // Set of mutexes used to lock access to each frame
    std::array<std::mutex, buf_total_frames> frame_locks;

    // Margins of unused pixels in each frame of the buffer
    static constexpr std::size_t margin_top = 2;
    static constexpr std::size_t margin_bottom = 2;
    static constexpr std::size_t margin_left = 26;
    static constexpr std::size_t margin_right = 0;

public:
    // Visible screen size
    static constexpr std::size_t screen_width
        = buf_height - margin_top - margin_bottom;
    static constexpr std::size_t screen_height
        = (buf_width - margin_left - margin_right) * buf_actual_depth;
    static constexpr std::size_t screen_size = screen_width * screen_height;

private:
    // A static buffer that contains pixels with bytes 1, 2, and 4 set to zero
    // and byte 3 set to its appropriate fixed value. Used for resetting a
    // frame when preparing the next frame
    std::array<std::uint8_t, buf_frame> null_phases{};

    // Buffer holding the current known intensity state of all cells
    std::array<Intensity, screen_size> current_intensity;

    // Queue of pending updates
    std::queue<Update> pending_updates;
    std::condition_variable updates_cv;
    std::mutex updates_lock;

    /** Thread that processes update requests and generates frames. */
    std::thread generator_thread;
    void run_generator_thread();

    /** Thread that sends ready frames to the display controller via vsync. */
    std::thread vsync_thread;
    void run_vsync_thread();
};

#endif // WAVED_DISPLAY_HPP

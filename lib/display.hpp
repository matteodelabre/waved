/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_GENERATOR_HPP
#define WAVED_GENERATOR_HPP

#include "controller.hpp"
#include "defs.hpp"
#include "update.hpp"
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
#include <vector>
#include <set>

namespace Waved
{

/**
 * Generates frames from update requests and sends them to the controller.
 *
 * This class processes update regions and drives the display controller with
 * appropriate waveforms to display the wanted intensities.
 */
class Generator
{
public:
    /**
     * Initialize a generator for a given controller.
     *
     * @param controller The controller to drive.
     * @param waveform_table Controller-specific waveform data.
     */
    Generator(
        Controller& controller,
        WaveformTable& waveform_table
    );

    // No copies, only allow moves
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    Generator(Generator&&) = default;
    Generator& operator=(Generator&&) = default;

    /** Destroy the generator. */
    ~Generator();

    /**
     * Start processing updates.
     *
     * This method will power on the display controller and process updates
     * added to the queue using `push_update()` continuously from a background
     * thread. If no updates are received for `power_off_timeout` ticks, the
     * controller is switched off to save power. Calling `stop()` or destroying
     * this object will stop the background threads, discarding any remaining
     * updates in the queue.
     */
    void start();

    /** Stop processing updates. */
    void stop();

    /**
     * Add an update to the queue.
     *
     * @param mode Update mode to use (by kind or by ID).
     * @param immediate Whether to process this update in immediate mode.
     * @param region Coordinates of the region affected by the update.
     * @param buffer New values for the pixels in the updated region.
     * @return If the update was accepted, unique update ID that can be used
     * to track its completion.
     */
    std::optional<UpdateID> push_update(
        ModeKind mode,
        bool immediate,
        UpdateRegion region,
        const std::vector<Intensity>& buffer
    );
    std::optional<UpdateID> push_update(
        ModeID mode,
        bool immediate,
        UpdateRegion region,
        const std::vector<Intensity>& buffer
    );

    /** Block until an update is complete. */
    void wait_for(UpdateID id);

    /** Block until all updates are complete. */
    void wait_for_all();

    /**
     * Enable performance reporting to the given stream.
     *
     * This method is a no-op unless ENABLE_PERF_REPORT is set at compile time.
     * When enabled, a CSV document is written to the given stream with
     * information on when each update enters or leaves the queue and when
     * frames are generated and sent to the controller.
     *
     * The CSV document will contain one row per processed update (or batch of
     * updates), with the following information:
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
     *
     * @param out Stream to write performance data to.
     */
    void enable_perf_report(std::ostream& out);

    /** Disable performance reporting. */
    void disable_perf_report();

private:
    // Driven controller
    Controller* controller;

    // Controller-specific waveform information
    WaveformTable* table;

    // True if the processing threads have been started
    bool started = false;

    // Signals that the generator and vsync threads need to stop
    std::atomic<bool> stopping_generator = false;
    std::atomic<bool> stopping_vsync = false;

    // Time after which to switch the controller off if no updates are received
    static constexpr std::chrono::milliseconds power_off_timeout{3000};

    using IntensityArray = std::vector<Intensity>;
    using StepArray = std::vector<std::size_t>;

    // Buffer holding the current known values of all pixels
    IntensityArray current_intensity;

    // Buffer holding the target pixel values during an update
    IntensityArray next_intensity;

    // Tells the current waveform “step” of each pixel during immediate updates
    StepArray waveform_steps;

    // Queue of updates waiting to be processed
    std::queue<Update> pending_updates;
    std::condition_variable updates_cv;
    std::mutex updates_lock;

    // Set of updates currently being processed or in queue
    std::set<UpdateID> processing_updates;
    std::condition_variable processed_cv;
    std::mutex processing_lock;

    // Buffer to which to generator thread will write to generate frames
    // for each update
    std::vector<std::vector<std::uint8_t>> generate_buffer{};

    // Update currently being processed by the generator thread
    Update generator_update;

    // Buffer from which the vsync thread will read frames to send
    // to the controller
    std::vector<std::vector<std::uint8_t>> vsync_buffer{};

    // Update currently being processed by the vsync thread
    Update vsync_update;

    // Set to true before calling vsync for the last time for a given update
    bool vsync_finalize = false;

    // Locks to read from or write to the vsync buffer
    std::atomic<bool> vsync_can_read = false;
    std::condition_variable vsync_can_read_cv;
    std::mutex vsync_read_lock;

    std::atomic<bool> vsync_can_write = true;
    std::condition_variable vsync_can_write_cv;
    std::mutex vsync_write_lock;

#ifdef ENABLE_PERF_REPORT
    std::ostream* perf_report_stream = nullptr;
#endif

    /** Thread that processes update requests and generates frames. */
    std::thread generator_thread;
    void run_generator_thread();

    /**
     * Remove the next update from the queue (or wait if queue is empty).
     *
     * @return Next update from the queue, or an empty optional if
     * there will be no more updates.
     */
    std::optional<Update> pop_update();

    /** Try to merge upcoming updates from the queue into a given update. */
    void merge_updates();

    /** Align a display region to lie on the byte boundary. */
    UpdateRegion align_region(UpdateRegion region) const;

    /** Prepare phase frames in batch for the current update and send them. */
    void generate_batch();

    /** Prepare and immediately send phase frames for the current update. */
    void generate_immediate();

    /** Store a null frame at the given buffer location. */
    void reset_frame(std::size_t frame_index);

    /**
     * Send frames generated by the generator thread to the vsync thread.
     *
     * @param finalize True iff this set of frames is the last one for
     * its associated update.
     */
    void send_frames(bool finalize);

    /** Thread that sends ready frames to the display controller via vsync. */
    std::thread vsync_thread;
    void run_vsync_thread();
}; // class Generator

} // namespace Waved

#endif // WAVED_GENERATOR_HPP

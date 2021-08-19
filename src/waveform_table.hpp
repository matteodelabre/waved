/**
 * @file
 * Load electrophoretic display (EPD) waveform definition tables.
 */

#ifndef WAVED_WAVEFORM_TABLE_HPP
#define WAVED_WAVEFORM_TABLE_HPP

#include "defs.hpp"
#include <utility>
#include <cstdint>
#include <array>
#include <vector>

/**
 * Waveform mode.
 *
 * Users can usually choose from several kinds of waveforms that provide
 * different trade-offs between image fidelity and rendering speed.
 */
using Mode = std::uint8_t;

/**
 * Temperature (in Celsius).
 *
 * Knowing the EPD’s current temperature is needed for selecting the right
 * waveform to use to transition a given cell.
 */
using Temperature = std::int8_t;

/** Read and use waveform definitions. */
class WaveformTable
{
public:
    /** Create an empty waveform table. */
    WaveformTable();

    /**
     * Read waveform table definitions from a WBF file.
     *
     * @param path Path to the file.
     * @return Parsed waveform table.
     * @throws std::runtime_error If a parsing error occurs.
     * @throws std::system_error If the file cannot be read.
     */
    static WaveformTable from_wbf(const char* path);

    /**
     * Set the operating mode.
     *
     * @param mode New mode.
     * @throws std::out_of_range If the given mode is not supported.
     */
    void set_mode(int mode);

    /**
     * Set the operating temperature.
     *
     * @param temperature New temperature in Celsius.
     * @throws std::out_of_range If the given temperature is not supported.
     */
    void set_temperature(int temperature);

    /**
     * Lookup the waveform for the given mode and temperature.
     *
     * @return Corresponding waveform.
     */
    const Waveform& lookup() const;
    using Lookup = std::vector<std::vector<std::size_t>>;

private:
    // Number of available modes
    Mode mode_count;

    // Current operating mode. This can be used as the first index in
    // `waveform_lookup` to find the right waveform for a given mode
    Mode current_mode = 0;

    // Set of temperature thresholds
    // The last value is the maximal operating temperature
    std::vector<Temperature> temperatures;

    // Current operating temperature range. This can be used as the second
    // index in `waveform_lookup` to find the right waveform for a given
    // temperature range
    std::size_t current_temperature = 0;

    // All available waveforms. This table may be smaller than
    // `(temperatures.size() - 1) * mode_count` since some modes/temperatures
    // combinations reuse the same waveform
    std::vector<Waveform> waveforms;

    // Vector for looking the waveform corresponding to a mode and temperature
    Lookup waveform_lookup;
};

#endif // WAVED_WAVEFORM_TABLE_HPP

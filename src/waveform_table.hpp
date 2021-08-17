/**
 * @file
 * Types and classes for handling electrophoretic display (EPD) waveforms.
 */

#ifndef WAVED_WAVEFORM_TABLE_HPP
#define WAVED_WAVEFORM_TABLE_HPP

#include <utility>
#include <cstdint>
#include <array>
#include <vector>

/**
 * Phase types.
 *
 * A phase is a command sent to an individual EPD cell for 1/85 s.
 */
enum class Phase : std::uint8_t
{
    // Leave the cell in its present cell
    Noop = 0b00,

    // Apply a current to bring black particles to the top
    Black = 0b01,

    // Apply a current to bring white particles to the top
    White = 0b10,
};

/**
 * Waveform.
 *
 * A waveform is a sequence of phases used to transition an EPD cell from a
 * given grayscale intensity to another.
 */
using PhaseMatrix = std::array<std::array<Phase, 32>, 32>;
using Waveform = std::vector<PhaseMatrix>;
using WaveformLookup = std::vector<std::vector<std::size_t>>;

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

/** Cell grayscale intensity (5 bits).  */
using Intensity = std::uint8_t;

/** Read and use waveform definitions. */
class WaveformTable
{
public:
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
    WaveformLookup waveform_lookup;
};

#endif // WAVED_WAVEFORM_TABLE_HPP

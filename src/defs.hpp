#ifndef WAVED_DEFS_HPP
#define WAVED_DEFS_HPP

#include <array>
#include <cstdint>
#include <vector>

/**
 * Phase types.
 *
 * A phase is a command sent to an individual EPD cell for 1/85 s.
 */
enum class Phase : std::uint8_t
{
    // Leave the cell in its present state
    Noop = 0b00,

    // Apply a current to bring black particles to the top
    Black = 0b01,

    // Apply a current to bring white particles to the top
    White = 0b10,
};

/** Cell grayscale intensity (5 bits).  */
using Intensity = std::uint8_t;
constexpr std::uint8_t intensity_values = 1 << 5;

/**
 * Waveform.
 *
 * A waveform is a sequence of phases used to transition an EPD cell from a
 * given grayscale intensity to another.
 */
using PhaseMatrix
    = std::array<std::array<Phase, intensity_values>, intensity_values>;
using Waveform = std::vector<PhaseMatrix>;

/** Screen region. */
struct Region
{
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

#endif // WAVED_DEFS_HPP

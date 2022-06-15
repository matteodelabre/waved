/**
 * @file Shared definitions for waved.
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WAVED_DEFS_HPP
#define WAVED_DEFS_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Waved
{

/**
 * Phase types.
 *
 * A phase is a command sent to an individual EPD cell in one frame.
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

/**
 * Cell grayscale intensity (5 bits).
 *
 * Only even values are used. 0 denotes full black, 30 full white.
 */
using Intensity = std::uint8_t;
constexpr std::uint8_t intensity_values = 1 << 5;

/**
 * Phase matrix.
 *
 * Lookup table that gives the appropriate phase to apply to transition
 * between two intensities.
 */
using PhaseMatrix
    = std::array<std::array<Phase, intensity_values>, intensity_values>;

/**
 * Waveform.
 *
 * A waveform is a sequence of phase matrices used to transition an EPD cell
 * from a given grayscale intensity to another.
 */
using Waveform = std::vector<PhaseMatrix>;

/** Screen region. */
struct Region
{
    std::uint32_t top;
    std::uint32_t left;
    std::uint32_t width;
    std::uint32_t height;
};

/**
 * Waveform types.
 *
 * Users can usually choose from several kinds of waveforms that provide
 * different trade-offs between image fidelity and rendering speed.
 *
 * See <https://www.waveshare.com/w/upload/c/c4/E-paper-mode-declaration.pdf>
 */
enum class ModeKind
{
    UNKNOWN,

    // Initialization mode used to force all pixels to go back to a
    // known white state
    INIT,

    // Fast, non-flashy update that only supports transitions to black or white
    DU,

    // Same as DU but supports 4 gray tones
    DU4,

    // Faster than DU and only supports transitions *between* black and white
    A2,

    // Full resolution mode (16 gray tones)
    GC16,

    // Full resolution mode with support for Regal
    GLR16,
};

/** Get a human-readable name for a mode kind. */
std::string mode_kind_to_string(ModeKind);

} // namespace Waved

#endif // WAVED_DEFS_HPP

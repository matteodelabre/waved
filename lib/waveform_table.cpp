/**
 * @file
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "waveform_table.hpp"
#include "checksum.tpp"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <set>
#include <endian.h>

namespace fs = std::filesystem;

namespace Waved
{

WaveformTable::WaveformTable()
{}

auto WaveformTable::lookup(ModeID mode, int temperature) const
-> const Waveform&
{
    if (mode < 0 || mode >= this->mode_count) {
        std::ostringstream message;
        message << "Mode " << mode << " not supported, available modes "
            "are 0-" << (this->mode_count - 1);
        throw std::out_of_range(message.str());
    }

    std::vector<Temperature>::const_iterator it;

    if (temperature < -128) {
        it = this->temperatures.cbegin();
    } else if (temperature > 127) {
        it = this->temperatures.cend();
    } else {
        it = std::upper_bound(
            this->temperatures.cbegin(),
            this->temperatures.cend(),
            temperature
        );
    }

    if (it == this->temperatures.cbegin()) {
        if (this->temperatures.empty()) {
            throw std::out_of_range("No temperature available");
        } else {
            std::ostringstream message;
            message << "Temperature " << (int) temperature << " °C too low, "
                "minimum operating temperature is "
                << (int) this->temperatures.front() << " °C";
            throw std::out_of_range(message.str());
        }
    }

    if (it == this->temperatures.cend()) {
        std::ostringstream message;
        message << "Temperature " << (int) temperature << " °C too high, "
            "maximum operating temperature is "
            << (int) (this->temperatures.back() - 1) << " °C";
        throw std::out_of_range(message.str());
    }

    std::size_t range = it - this->temperatures.cbegin() - 1;
    return this->waveforms[waveform_lookup[mode][range]];
}

auto WaveformTable::get_frame_rate() const -> std::uint8_t
{
    return this->frame_rate;
}

auto WaveformTable::get_temperatures() const -> const std::vector<Temperature>&
{
    return this->temperatures;
}

auto WaveformTable::get_mode_count() const -> ModeID
{
    return this->mode_count;
}

auto WaveformTable::get_mode_kind(ModeID mode) const -> ModeKind
{
    return this->mode_kind_by_id[mode];
}

auto WaveformTable::get_mode_id(ModeKind mode) const -> ModeID
{
    auto id_iter = this->mode_id_by_kind.find(mode);

    if (id_iter == this->mode_id_by_kind.end()) {
        std::ostringstream message;
        message << "Mode kind " << mode_kind_to_string(mode) << " is not "
            "supported";
        throw std::out_of_range(message.str());
    }

    return id_iter->second;
}

namespace
{

/**
 * Use heuristics to classify a mode into a mode kind given
 * a sample waveform from that mode.
 */
auto classify_mode_kind(const Waveform& waveform) -> ModeKind
{
    // Detect INIT mode: transitions should be all the same regardless
    // of the initial or target intensity values
    bool is_init = true;

    for (const auto& matrix : waveform) {
        for (Intensity from = 0; from < intensity_values; ++from) {
            for (Intensity to = 0; to < intensity_values; ++to) {
                if (matrix[0][0] != matrix[from][to]) {
                    is_init = false;
                    break;
                }
            }

            if (!is_init) {
                break;
            }
        }

        if (!is_init) {
            break;
        }
    }

    if (is_init) {
        return ModeKind::INIT;
    }

    // Detect which intensity transitions are no-ops
    std::array<std::array<bool, intensity_values>, intensity_values> no_ops;

    for (Intensity from = 0; from < intensity_values; ++from) {
        for (Intensity to = 0; to < intensity_values; ++to) {
            no_ops[from][to] = std::all_of(
                std::cbegin(waveform),
                std::cend(waveform),
                [from, to](const auto& matrix) {
                    return matrix[from][to] == Phase::Noop;
                }
            );
        }
    }

    // “Regal” waveforms support special transitions
    bool regalable = (
        !no_ops[28][29] && !no_ops[28][31]
        && !no_ops[29][29] && !no_ops[29][31]
        && !no_ops[30][29] && !no_ops[30][31]
    );

    // Quantify the amount of supported intensities in sources and targets
    double defined_sources = 0;
    double defined_targets = 0;

    for (Intensity from = 0; from < intensity_values; ++from) {
        bool is_defined = false;

        for (Intensity to = 0; to < intensity_values; ++to) {
            if (!no_ops[from][to]) {
                ++defined_targets;
                is_defined = true;
            }
        }

        if (is_defined) {
            defined_sources += 1;
        }
    }

    defined_targets /= defined_sources;

    if (defined_sources >= 16) {
        if (defined_targets < 2) {
            return ModeKind::DU;
        }

        if (defined_targets < 4) {
            return ModeKind::DU4;
        }

        if (defined_targets >= 16) {
            if (regalable) {
                return ModeKind::GLR16;
            }

            return ModeKind::GC16;
        }
    }

    if (defined_sources <= 8 && defined_targets <= 1) {
        return ModeKind::A2;
    }

    return ModeKind::UNKNOWN;
}

} // anonymous namespace

void WaveformTable::populate_mode_kind_mappings()
{
    mode_kind_by_id.resize(this->mode_count);
    mode_id_by_kind.clear();

    constexpr auto sample_temperature = 21;

    for (ModeID mode = 0; mode < this->mode_count; ++mode) {
        auto kind = classify_mode_kind(this->lookup(mode, sample_temperature));

        if (kind == ModeKind::UNKNOWN) {
            std::cerr << "[warn] Could not detect mode kind for mode #"
                << static_cast<int>(mode) << '\n';
        } else {
            this->mode_id_by_kind.insert({kind, mode});
        }

        this->mode_kind_by_id[mode] = kind;
    }
}

namespace
{

/**
 * WBF file decoding.
 *
 * The WBF file is widely used for storing waveform data, but no official
 * description of the format has been published by E-Ink. These decoding
 * functions are based on the following unofficial sources.
 *
 * Information sourced from:
 *
 * - <https://www.waveshare.net/w/upload/c/c4/E-paper-mode-declaration.pdf>
 * - <https://github.com/fread-ink/inkwave>
 * - <https://github.com/torvalds/linux/blob/master/drivers/video/fbdev/metronomefb.c>
 * - <https://github.com/julbouln/ice40_eink_controller/blob/master/utils/wbf_dump/wbf_dump.c>
 *
 * All values in WBF files are little-endian.
 */
using Buffer = std::vector<char>;

struct __attribute__((packed)) wbf_header {
    std::uint32_t checksum; // CRC32 checksum
    std::uint32_t filesize; // Total file length
    std::uint32_t serial; // Unique serial number for the waveform file
    std::uint8_t run_type;
    std::uint8_t fpl_platform;
    std::uint16_t fpl_lot;
    std::uint8_t adhesive_run;
    std::uint8_t waveform_version;
    std::uint8_t waveform_subversion;
    std::uint8_t waveform_type;
    std::uint8_t fpl_size;
    std::uint8_t mfg_code;
    std::uint8_t waveform_revision;
    std::uint8_t old_frame_rate; // Old field used for frame rate specification,
                                 // (only supported value: 0x85 for 85 Hz)
    std::uint8_t frame_rate; // New frame rate field (in Hz)
    std::uint8_t vcom_offset;
    std::uint16_t _reserved1;
    std::uint32_t extra_info_addr : 24;
    std::uint8_t checksum1; // Checksum for bytes 0-30 with 8 first bytes to 0
    std::uint32_t wmta : 24;
    std::uint8_t fvsn;
    std::uint8_t luts;
    std::uint8_t mode_count; // Index of the last mode
    std::uint8_t temp_range_count; // Index of the last temperature range
    std::uint8_t advanced_wfm_flags;
    std::uint8_t eb;
    std::uint8_t sb;
    std::uint8_t _reserved2;
    std::uint8_t _reserved3;
    std::uint8_t _reserved4;
    std::uint8_t _reserved5;
    std::uint8_t _reserved6;
    std::uint8_t checksum2;
};

// Set of values that we do not expect to change. To be on the safe side (since
// we’re not sure what those values mean precisely), operation will not proceed
// if those differ from the values in the WBF file
constexpr auto expected_run_type = 17;
constexpr auto expected_fpl_platform = 0;
constexpr auto expected_adhesive_run = 25;
constexpr auto expected_waveform_type = 81;
constexpr auto expected_waveform_revision = 0;
constexpr auto expected_vcom_offset = 0;
constexpr auto expected_fvsn = 1;
constexpr auto expected_luts = 4;
constexpr auto expected_advanced_wfm_flags = 3;

/** Parse the header of a WBF file and check its integrity. */
auto parse_header(const Buffer& buffer) -> wbf_header
{
    if (buffer.size() < sizeof(wbf_header)) {
        std::ostringstream message;
        message << "Too short to be a WBF file: file is "
            << buffer.size() << " bytes long while the minimum header size is "
            << sizeof(wbf_header) << " bytes";
        throw std::runtime_error(message.str());
    }

    auto header = reinterpret_cast<const wbf_header&>(*buffer.data());
    const auto begin = buffer.cbegin();

    // Fix endianness if needed
    header.checksum = le32toh(header.checksum);
    header.filesize = le32toh(header.filesize);
    header.serial = le32toh(header.serial);
    header.fpl_lot = le16toh(header.fpl_lot);
    header._reserved1 = le16toh(header._reserved1);
    header.extra_info_addr = le32toh(header.extra_info_addr);
    header.wmta = le32toh(header.wmta);

    // Verify checksums
    std::uint8_t checksum1_verif = basic_checksum(begin + 8, begin + 31);

    if (header.checksum1 != checksum1_verif) {
        std::ostringstream message;
        message << "Corrupted WBF header: expected checksum1 0x"
            << std::hex << (int) header.checksum1 << ", actual 0x"
            << std::hex << (int) checksum1_verif;
        throw std::runtime_error(message.str());
    }

    std::uint8_t checksum2_verif = basic_checksum(begin + 32, begin + 47);

    if (header.checksum2 != checksum2_verif) {
        std::ostringstream message;
        message << "Corrupted WBF header: expected checksum2 0x"
            << std::hex << (int) header.checksum2 << ", actual 0x"
            << std::hex << (int) checksum2_verif;
        throw std::runtime_error(message.str());
    }

    // Check for expected values
    if (header.run_type != expected_run_type) {
        std::ostringstream message;
        message << "Invalid run type in WBF header: expected "
            << expected_run_type << ", actual " << (int) header.run_type;
        throw std::runtime_error(message.str());
    }

    if (header.fpl_platform != expected_fpl_platform) {
        std::ostringstream message;
        message << "Invalid FPL platform in WBF header: expected "
            << expected_fpl_platform << ", actual "
            << (int) header.fpl_platform;
        throw std::runtime_error(message.str());
    }

    if (header.adhesive_run != expected_adhesive_run) {
        std::ostringstream message;
        message << "Invalid adhesive run in WBF header: expected "
            << expected_adhesive_run << ", actual "
            << (int) header.adhesive_run;
        throw std::runtime_error(message.str());
    }

    if (header.waveform_type != expected_waveform_type) {
        std::ostringstream message;
        message << "Invalid waveform type in WBF header: expected "
            << expected_waveform_type << ", actual "
            << (int) header.waveform_type;
        throw std::runtime_error(message.str());
    }

    if (header.waveform_revision != expected_waveform_revision) {
        std::ostringstream message;
        message << "Invalid waveform revision in WBF header: expected "
            << expected_waveform_revision << ", actual "
            << (int) header.waveform_revision;
        throw std::runtime_error(message.str());
    }

    if (header.vcom_offset != expected_vcom_offset) {
        std::ostringstream message;
        message << "Invalid VCOM offset in WBF header: expected "
            << expected_vcom_offset << ", actual " << (int) header.vcom_offset;
        throw std::runtime_error(message.str());
    }

    if (header.fvsn != expected_fvsn) {
        std::ostringstream message;
        message << "Invalid FVSN in WBF header: expected "
            << expected_fvsn << ", actual " << (int) header.fvsn;
        throw std::runtime_error(message.str());
    }

    if (header.luts != expected_luts) {
        std::ostringstream message;
        message << "Invalid LUTS in WBF header: expected "
            << expected_luts << ", actual " << (int) header.luts;
        throw std::runtime_error(message.str());
    }

    if (header.advanced_wfm_flags != expected_advanced_wfm_flags) {
        std::ostringstream message;
        message << "Invalid advanced WFM flags revision in WBF header: expected "
            << expected_advanced_wfm_flags << ", actual "
            << (int) header.advanced_wfm_flags;
        throw std::runtime_error(message.str());
    }

    return header;
}

/** Parse the set of temperature ranges from a WBF file. */
auto parse_temperatures(
    const wbf_header& header, Buffer::const_iterator& begin
) -> std::vector<Temperature>
{
    std::vector<Temperature> result;
    std::size_t count = header.temp_range_count + 2;
    result.reserve(count);

    auto it = begin;

    for (; it != begin + count; ++it) {
        result.emplace_back(*it);
    }

    std::uint8_t checksum_verif = basic_checksum(begin, it);
    std::uint8_t checksum_expect = *it;

    if (checksum_expect != checksum_verif) {
        std::ostringstream message;
        message << "Corrupted WBF temperatures: expected checksum 0x"
            << std::hex << (int) checksum_expect << " actual 0x"
            << std::hex << (int) checksum_verif;
        throw std::runtime_error(message.str());
    }

    begin = it + 1;
    return result;
}

/** Read a pointer field to a WBF file section. */
auto parse_pointer(Buffer::const_iterator& begin) -> std::uint32_t
{
    std::uint8_t byte1 = *begin;
    ++begin;
    std::uint8_t byte2 = *begin;
    ++begin;
    std::uint8_t byte3 = *begin;
    ++begin;

    std::uint32_t pointer = le32toh(byte1 + (byte2 << 8) + (byte3 << 16));
    std::uint8_t checksum_verif = byte1 + byte2 + byte3;
    std::uint8_t checksum_expect = *begin;
    ++begin;

    if (checksum_expect != checksum_verif) {
        std::ostringstream message;
        message << "Corrupted WBF pointer: expected checksum 0x"
            << std::hex << (int) checksum_expect << " actual 0x"
            << std::hex << (int) checksum_verif;
        throw std::runtime_error(message.str());
    }

    return pointer;
}

/** Computes the ordered list of waveform block addresses in a WBF file. */
auto find_waveform_blocks(
    const wbf_header& header,
    Buffer::const_iterator file_begin,
    Buffer::const_iterator table_begin
) -> std::vector<std::uint32_t>
{
    std::size_t mode_count = header.mode_count + 1;
    std::size_t temp_count = header.temp_range_count + 1;

    std::set<std::uint32_t> result;

    for (std::size_t mode = 0; mode < mode_count; ++mode) {
        auto mode_begin = file_begin + parse_pointer(table_begin);

        for (std::size_t temp = 0; temp < temp_count; ++temp) {
            result.insert(parse_pointer(mode_begin));
        }
    }

    return {result.begin(), result.end()};
}

/** Parse a waveform block in a WBF file. */
auto parse_waveform(Buffer::const_iterator begin, Buffer::const_iterator end)
-> Waveform
{
    end -= 2;

    PhaseMatrix matrix;
    Waveform result;

    std::uint8_t i = 0;
    std::uint8_t j = 0;
    bool repeat_mode = true;

    while (begin != end) {
        std::uint8_t byte = *begin;
        ++begin;

        if (byte == 0xFC) {
            repeat_mode = !repeat_mode;
            continue;
        }

        auto p4 = static_cast<Phase>(byte & 3);
        auto p3 = static_cast<Phase>((byte >> 2) & 3);
        auto p2 = static_cast<Phase>((byte >> 4) & 3);
        auto p1 = static_cast<Phase>(byte >> 6);

        int repeat = 1;

        if (repeat_mode && begin != end) {
            // In repeat_mode, each byte is followed by a repetition number;
            // otherwise, this number is assumed to be 1
            repeat = static_cast<std::uint8_t>(*begin) + 1;
            ++begin;

            if (byte == 0xFF) {
                break;
            }
        }

        for (int n = 0; n < repeat; ++n) {
            matrix[j++][i] = p1;
            matrix[j++][i] = p2;
            matrix[j++][i] = p3;
            matrix[j++][i] = p4;

            if (j == intensity_values) {
                j = 0;
                ++i;
            }

            if (i == intensity_values) {
                i = 0;
                result.push_back(matrix);
            }
        }
    }

    return result;
}

/**
 * Parse all waveform blocks of a WBF file and associate them to their
 * mode and temperature indices.
 */
auto parse_waveforms(
    const wbf_header& header,
    const std::vector<std::uint32_t>& blocks,
    Buffer::const_iterator file_begin,
    Buffer::const_iterator table_begin
) -> std::pair<std::vector<Waveform>, WaveformTable::Lookup>
{
    std::vector<Waveform> waveforms;

    for (auto it = blocks.cbegin(); std::next(it) != blocks.cend(); ++it) {
        waveforms.emplace_back(parse_waveform(
            file_begin + *it,
            file_begin + *(it + 1)
        ));
    }

    std::size_t mode_count = header.mode_count + 1;
    std::size_t temp_count = header.temp_range_count + 1;
    WaveformTable::Lookup waveform_lookup;
    waveform_lookup.reserve(mode_count);

    for (std::size_t mode = 0; mode < mode_count; ++mode) {
        auto mode_begin = file_begin + parse_pointer(table_begin);
        std::vector<std::size_t> temp_lookup;
        temp_lookup.reserve(temp_count);

        for (std::size_t temp = 0; temp < temp_count; ++temp) {
            auto waveform_begin = parse_pointer(mode_begin);
            temp_lookup.push_back(
                std::lower_bound(blocks.cbegin(), blocks.cend(), waveform_begin)
                - blocks.cbegin()
            );
        }

        waveform_lookup.emplace_back(std::move(temp_lookup));
    }

    return {waveforms, waveform_lookup};
}

} // anonymous namespace

auto WaveformTable::from_wbf(std::istream& file) -> WaveformTable
{
    WaveformTable result;

    // Read the entire file in memory
    std::ostringstream buffer_read;
    buffer_read << file.rdbuf();
    const std::string& buffer_str = buffer_read.str();
    Buffer buffer(buffer_str.cbegin(), buffer_str.cend());

    // Parse WBF header
    wbf_header header = parse_header(buffer);
    auto it = buffer.cbegin() + sizeof(header);

    result.frame_rate = header.frame_rate == 0 ? 85 : header.frame_rate;
    result.mode_count = header.mode_count + 1;

    // Check expected size
    if (header.filesize != buffer.size()) {
        std::ostringstream message;
        message << "Invalid filesize in WBF header: specified "
            << header.filesize << " bytes, actual " << buffer.size()
            << " bytes";
        throw std::runtime_error(message.str());
    }

    // Verify CRC32 checksum
    std::uint8_t zeroes[] = {0, 0, 0, 0};
    std::uint32_t crc_verif = 0;
    crc_verif = crc32_checksum(crc_verif, zeroes, zeroes + 4);
    crc_verif = crc32_checksum(crc_verif, buffer.cbegin() + 4, buffer.cend());

    if (header.checksum != crc_verif) {
        std::ostringstream message;
        message << "Corrupted WBF file: expected CRC32 0x"
            << std::hex << (int) header.checksum << ", actual 0x"
            << std::hex << (int) crc_verif;
        throw std::runtime_error(message.str());
    }

    // Parse temperature table
    result.temperatures = parse_temperatures(header, it);

    // Skip extra information (contains a string equal to the file name)
    std::uint8_t len = *it;
    it += len + 2;

    // Parse waveforms
    auto blocks = find_waveform_blocks(header, buffer.cbegin(), it);
    blocks.push_back(buffer.size());
    auto waveforms = parse_waveforms(header, blocks, buffer.cbegin(), it);

    result.waveforms = std::move(waveforms.first);
    result.waveform_lookup = std::move(waveforms.second);
    result.populate_mode_kind_mappings();
    return result;
}

auto WaveformTable::from_wbf(const char* path) -> WaveformTable
{
    std::ifstream file{path, std::ios::binary};

    if (!file) {
        throw std::system_error(
            errno, std::generic_category(), "Open file for reading"
        );
    }

    return WaveformTable::from_wbf(file);
}

namespace
{

/**
 * Barcode decoding.
 *
 * /dev/mmcblk2boot1 is a 2 MiB-long device that contains a set of
 * length-prefixed metadata fields. The first field is the device serial
 * number. The fourth field contains a “barcode” that identifies the EPD, in
 * particular the front panel laminate (FPL) number. Using this FPL number,
 * we can find the appropriate WBF file in /usr/share/remarkable.
 *
 * Information sourced from:
 *
 * - <https://github.com/rmkit-dev/rmkit/issues/35>
 * - Reverse-engineering Xochitl
 */

/** Read the set of metadata fields from the metadata device. */
auto read_metadata() -> std::vector<std::string>
{
    std::ifstream device{"/dev/mmcblk2boot1", std::ios::binary};
    std::vector<std::string> result;

    while (device) {
        std::uint32_t length;
        device.read(reinterpret_cast<char*>(&length), sizeof(length));
        length = be32toh(length);

        if (length == 0) {
            break;
        }

        std::string data(length, ' ');
        device.read(data.data(), length);
        result.emplace_back(std::move(data));
    }

    return result;
}

auto barcode_symbol_to_int(char symbol) -> std::int16_t
{
    if ('0' <= symbol && symbol <= '9') {
        // 0 - 9 get mapped to 0 - 9
        return symbol - '0';
    }

    if ('A' <= symbol && symbol <= 'H') {
        // A - H get mapped to 10 - 17
        return symbol - 'A' + 10;
    } else if ('J' <= symbol && symbol <= 'N') {
        // J - N get mapped to 18 - 22
        return symbol - 'J' + 18;
    } else if ('Q' <= symbol && symbol <= 'Z') {
        // Q - Z get mapped to 23 - 32
        return symbol - 'Q' + 23;
    } else {
        return -1;
    }
}

auto decode_fpl_number(const std::string& barcode) -> std::int16_t
{
    if (barcode.size() < 8) {
        return -1;
    }

    auto d6 = barcode_symbol_to_int(barcode[6]);
    auto d7 = barcode_symbol_to_int(barcode[7]);

    if (d6 == -1 || d7 == -1) {
        return -1;
    }

    if (d7 < 10) {
        // Values from 0 to 329
        return d7 + d6 * 10;
    }

    // Values from 330 to 858
    return d7 + 320 + (d6 - 10) * 23;
}

} // anonymous namespace

auto WaveformTable::discover_wbf_file() -> std::optional<std::string>
{
    auto metadata = read_metadata();

    if (metadata.size() < 4) {
        return {};
    }

    auto fpl_lot = decode_fpl_number(metadata[3]);

    for (const auto& entry : fs::directory_iterator{"/usr/share/remarkable"}) {
        if (entry.path().extension().native() != ".wbf") {
            continue;
        }

        std::ifstream file{entry.path(), std::ios::binary};
        Buffer buffer(sizeof(wbf_header));
        file.read(buffer.data(), buffer.size());

        if (!file) {
            continue;
        }

        try {
            auto header = parse_header(buffer);

            if (header.fpl_lot == fpl_lot) {
                return entry.path();
            }
        } catch (const std::runtime_error& err) {
            // Ignore malformed files
            continue;
        }
    }

    return {};
}

} // namespace Waved

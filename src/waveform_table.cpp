#include "waveform_table.hpp"
#include "checksum.tpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <endian.h>

WaveformTable::WaveformTable()
{}

void WaveformTable::set_mode(int mode)
{
    if (mode < 0 || mode >= this->mode_count) {
        std::ostringstream message;
        message << "Mode " << mode << " not supported, available modes "
            "are 0-" << (this->mode_count - 1);
        throw std::out_of_range(message.str());
    }

    this->current_mode = mode;
}

void WaveformTable::set_temperature(int temperature)
{
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

    this->current_temperature = it - this->temperatures.cbegin() - 1;
}

auto WaveformTable::lookup() const -> const Waveform&
{
    return this->waveforms[
        waveform_lookup[this->current_mode][this->current_temperature]
    ];
}

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
constexpr auto expected_fpl_size = 0;
constexpr auto expected_waveform_revision = 0;
constexpr auto expected_frame_rate = 85;
constexpr auto expected_vcom_offset = 0;
constexpr auto expected_fvsn = 1;
constexpr auto expected_luts = 4;
constexpr auto expected_advanced_wfm_flags = 3;

wbf_header parse_header(const Buffer& buffer)
{
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
    std::uint8_t zeroes[] = {0, 0, 0, 0};
    std::uint32_t crc_verif = 0;
    crc_verif = crc32_checksum(crc_verif, zeroes, zeroes + 4);
    crc_verif = crc32_checksum(crc_verif, begin + 4, buffer.cend());

    if (header.checksum != crc_verif) {
        std::ostringstream message;
        message << "Corrupted WBF file: expected CRC32 0x"
            << std::hex << (int) header.checksum << ", actual 0x"
            << std::hex << (int) crc_verif;
        throw std::runtime_error(message.str());
    }

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
    if (header.filesize != buffer.size()) {
        std::ostringstream message;
        message << "Invalid filesize in WBF header: specified "
            << header.filesize << " bytes, actual " << buffer.size()
            << " bytes";
        throw std::runtime_error(message.str());
    }

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

    if (header.fpl_size != expected_fpl_size) {
        std::ostringstream message;
        message << "Invalid FPL size in WBF header: expected "
            << expected_fpl_size << ", actual " << (int) header.fpl_size;
        throw std::runtime_error(message.str());
    }

    if (header.waveform_revision != expected_waveform_revision) {
        std::ostringstream message;
        message << "Invalid waveform revision in WBF header: expected "
            << expected_waveform_revision << ", actual "
            << (int) header.waveform_revision;
        throw std::runtime_error(message.str());
    }

    if (header.frame_rate != expected_frame_rate) {
        std::ostringstream message;
        message << "Invalid frame rate in WBF header: expected "
            << expected_frame_rate << ", actual " << (int) header.frame_rate;
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

std::vector<Temperature> parse_temperatures(
    const wbf_header& header, Buffer::const_iterator& begin
)
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

std::uint32_t parse_pointer(Buffer::const_iterator& begin)
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

std::vector<std::uint32_t> find_waveform_blocks(
    const wbf_header& header,
    Buffer::const_iterator file_begin,
    Buffer::const_iterator table_begin
)
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

Waveform parse_waveform(
    Buffer::const_iterator begin,
    Buffer::const_iterator end
)
{
    end -= 2;

    PhaseMatrix matrix;
    Waveform result;

    uint8_t i = 0;
    uint8_t j = 0;
    bool repeat_mode = true;

    while (begin != end) {
        std::uint8_t byte = *begin;
        ++begin;

        if (byte == 0xFC) {
            repeat_mode = !repeat_mode;
            continue;
        }

        Phase p1 = static_cast<Phase>(byte & 3);
        Phase p2 = static_cast<Phase>((byte >> 2) & 3);
        Phase p3 = static_cast<Phase>((byte >> 4) & 3);
        Phase p4 = static_cast<Phase>(byte >> 6);

        int repeat = 1;

        if (repeat_mode && begin != end) {
            // In repeat_mode, each byte is followed by a repetition number;
            // otherwise, this number is assumed to be 1
            repeat = static_cast<uint8_t>(*begin) + 1;
            ++begin;

            if (byte == 0xFF) {
                break;
            }
        }

        for (int n = 0; n < repeat; ++n) {
            matrix[i][j++] = p1;
            matrix[i][j++] = p2;
            matrix[i][j++] = p3;
            matrix[i][j++] = p4;

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

std::pair<std::vector<Waveform>, WaveformTable::Lookup> parse_waveforms(
    const wbf_header& header,
    const std::vector<std::uint32_t>& blocks,
    Buffer::const_iterator file_begin,
    Buffer::const_iterator table_begin
)
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

WaveformTable WaveformTable::from_wbf(const char* path)
{
    WaveformTable result;

    // Read the entire file in memory
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "(WaveformTable::from_wbf) Open file for reading"
        );
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    Buffer buffer(size);
    file.read(buffer.data(), size);

    // Parse WBF header
    wbf_header header = parse_header(buffer);
    auto it = buffer.cbegin() + sizeof(header);
    result.mode_count = header.mode_count + 1;

    // Parse temperature table
    result.temperatures = parse_temperatures(header, it);

    // Skip extra information (contains a string equal to the file name)
    std::uint8_t len = *it;
    it += len + 2;

    // Parse waveforms
    auto blocks = find_waveform_blocks(header, buffer.cbegin(), it);
    blocks.push_back(size);
    auto waveforms = parse_waveforms(header, blocks, buffer.cbegin(), it);

    result.waveforms = std::move(waveforms.first);
    result.waveform_lookup = std::move(waveforms.second);
    return result;
}

/**
 * @file Dump waveform information from a WBF file.
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "waveform_table.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>

void print_help(std::ostream& out, const char* name)
{
    out << "Usage: " << name << " [-h|--help] [FILE] [MODE TEMP]\n";
    out << "Dump waveform information from a WBF file.\n";
}

bool is_index(const char* arg)
{
    return std::all_of(
        arg,
        arg + std::strlen(arg),
        [](unsigned char c){ return std::isdigit(c); }
    );
}

inline void next_arg(int& argc, const char**& argv)
{
    --argc;
    ++argv;
}

int main(int argc, const char** argv)
{
    const char* name = argv[0];
    bool from_file = false;
    next_arg(argc, argv);

    if (argc && (argv[0] == std::string("-h") || argv[0] == std::string("--help"))) {
        print_help(std::cout, name);
        return 0;
    }

    Waved::WaveformTable table;

    try {
        if (argc && !is_index(argv[0])) {
            from_file = true;
            table = Waved::WaveformTable::from_wbf(argv[0]);
            next_arg(argc, argv);
        } else {
            table = Waved::WaveformTable::from_wbf(std::cin);
        }
    } catch (const std::system_error& err) {
        std::cerr << "I/O error: " << err.what() << '\n';
        return 1;
    } catch (const std::runtime_error& err) {
        std::cerr << "Parse error: " << err.what() << '\n';
        return 1;
    }

    auto frame_rate = table.get_frame_rate();
    auto mode_count = table.get_mode_count();
    auto temps = table.get_temperatures();

    if (argc < 2) {
        std::cout << "Frame rate: " << static_cast<int>(frame_rate) << " Hz\n";
        std::cout << "Available modes: " << static_cast<int>(mode_count) << '\n';
        std::cout << "Temperature ranges:\n";

        for (std::size_t i = 0; i < temps.size() - 1; ++i) {
            std::cout << "  " << std::setw(2) << static_cast<int>(temps[i])
                << " - " << std::setw(2) << static_cast<int>(temps[i + 1] - 1)
                << " °C\n";
        }

        std::cerr << "\nCall ";

        if (from_file) {
            std::cerr << name << " [FILE]";
        } else {
            std::cerr << name;
        }

        std::cerr << " [MODE TEMP] to list waveforms for\na given mode "
            "and temperature range.\n";
        return 0;
    }

    int mode = std::stoi(argv[0]);
    int temperature = std::stoi(argv[1]);

    try {
        const auto& waveform = table.lookup(mode, temperature);
        std::cerr << "Listing waveforms for mode " << mode << " and "
            "temperature " << temperature << " °C\n"
            "(No-op waveforms are not shown)\n\n";

        for (Waved::Intensity from = 0; from < Waved::intensity_values; ++from) {
            for (Waved::Intensity to = 0; to < Waved::intensity_values; ++to) {
                if (std::all_of(
                    std::cbegin(waveform),
                    std::cend(waveform),
                    [from, to](const auto& matrix) {
                        return matrix[from][to] == Waved::Phase::Noop;
                    }
                )) {
                    // Skip no-op waveforms
                    continue;
                }

                std::cerr << "(" << std::setw(2) << static_cast<int>(from)
                    << " -> " << std::setw(2) << static_cast<int>(to) << "): ";

                for (const auto& matrix : waveform) {
                    std::cout << static_cast<int>(matrix[from][to]);
                }

                std::cerr << '\n';
            }
        }
    } catch (const std::out_of_range& err) {
        std::cerr << "Error: " << err.what() << '\n';
        return 1;
    }

    return 0;
}

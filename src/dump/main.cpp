/**
 * @file Dump waveform information from a WBF file.
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
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
    out << "Usage: " << name << " [-h|--help] FILE [MODE TEMP]\n";
    out << "Dump waveform information from a WBF file.\n";
}

int print_summary(const char* name, const Waved::WaveformTable& table)
{
    auto frame_rate = table.get_frame_rate();
    auto mode_count = table.get_mode_count();
    auto temps = table.get_temperatures();

    std::cout << "Frame rate: " << static_cast<int>(frame_rate) << " Hz\n";
    std::cout << "\nAvailable modes:\n";

    for (std::size_t mode = 0; mode < mode_count; ++mode) {
        std::cout << "  " << mode << ": "
            << Waved::mode_kind_to_string(table.get_mode_kind(mode))
            << '\n';
    }

    std::cout << "\nTemperature ranges:\n";

    for (std::size_t i = 0; i < temps.size() - 1; ++i) {
        std::cout << "  " << std::setw(2) << static_cast<int>(temps[i])
            << " - " << std::setw(2) << static_cast<int>(temps[i + 1] - 1)
            << " °C\n";
    }

    std::cerr << "\nCall '" << name << " FILE MODE TEMP' for a list of "
        "waveforms for\na given mode and temperature range.\n";

    return EXIT_SUCCESS;
}

int print_mode(
    const char* name,
    const Waved::WaveformTable& table,
    int mode,
    int temp,
    bool by_frame
)
{
    try {
        const auto& waveform = table.lookup(mode, temp);
        std::cerr << "Listing waveforms for mode " << mode << " ("
            << Waved::mode_kind_to_string(table.get_mode_kind(mode))
            << ") and temperature " << temp << " °C\n";

        if (!by_frame) {
            std::cerr << "Waveforms are listed by transition (no-op "
                "transitions are omitted)\n";
            std::cerr << "Call '" << name << " FILE MODE TEMP --frames' to "
                "list by frame instead\n\n";

            for (
                Waved::Intensity from = 0;
                from < Waved::intensity_values;
                ++from
            ) {
                for (
                    Waved::Intensity to = 0;
                    to < Waved::intensity_values;
                    ++to
                ) {
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
                        << " -> " << std::setw(2) << static_cast<int>(to)
                        << "): ";

                    for (const auto& matrix : waveform) {
                        std::cout << static_cast<int>(matrix[from][to]);
                    }

                    std::cerr << '\n';
                }
            }
        } else {
            std::cerr << "Waveforms are listed frame by frame (with repeated "
                "frames indicated as such)\n";
            std::cerr << "Call '" << name << " FILE MODE TEMP' to "
                "list by transition instead\n\n";

            for (std::size_t i = 0; i < waveform.size(); ++i) {
                std::cerr << "Frame #" << i << ":";

                const auto& prev = i > 0 ? waveform[i - 1] : waveform[i];
                const auto& matrix = waveform[i];
                bool repeat = false;

                for (std::size_t j = 0; j < i; ++j) {
                    if (waveform[i] == waveform[j]) {
                        std::cerr << " (repeat frame #" << j << ")";
                        repeat = true;
                        break;
                    }
                }

                if (!repeat) {
                    std::cerr << '\n';
                    std::cerr << "             1111111111222222222233\n";
                    std::cerr << "   01234567890123456789012345678901\n\n";

                    for (
                        Waved::Intensity from = 0;
                        from < Waved::intensity_values;
                        ++from
                    ) {
                        std::cerr << std::setw(2) << static_cast<int>(from)
                            << ' ';

                        for (
                            Waved::Intensity to = 0;
                            to < Waved::intensity_values;
                            ++to
                        ) {
                            if (matrix[from][to] != prev[from][to]) {
                                std::cerr << "\033[31m";
                            }

                            std::cerr << static_cast<int>(matrix[from][to]);

                            if (matrix[from][to] != prev[from][to]) {
                                std::cerr << "\033[0m";
                            }
                        }

                        std::cerr << '\n';
                    }
                }

                std::cerr << '\n';
            }
        }

        return EXIT_SUCCESS;
    } catch (const std::out_of_range& err) {
        std::cerr << "Error: " << err.what() << '\n';
        return EXIT_FAILURE;
    }
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
    next_arg(argc, argv);

    if (argc == 0) {
        print_help(std::cerr, name);
        return EXIT_FAILURE;
    }

    if (argv[0] == std::string("-h") || argv[0] == std::string("--help")) {
        print_help(std::cout, name);
        return EXIT_SUCCESS;
    }

    Waved::WaveformTable table;

    try {
        if (argv[0] == "-") {
            table = Waved::WaveformTable::from_wbf(std::cin);
        } else {
            table = Waved::WaveformTable::from_wbf(argv[0]);
        }

        next_arg(argc, argv);
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
        return print_summary(name, table);
    }

    Waved::ModeID mode;

    if (is_index(argv[0])) {
        mode = std::stoi(argv[0]);
    } else {
        auto kind = Waved::mode_kind_from_string(argv[0]);

        if (kind == Waved::ModeKind::UNKNOWN) {
            std::cerr << "Error: Unknown mode '" << argv[0] << "'\n";
            return EXIT_FAILURE;
        }

        try {
            mode = table.get_mode_id(kind);
        } catch (const std::out_of_range& err) {
            std::cerr << "Error: Unsupported mode '" << argv[0] << "'\n";
            return EXIT_FAILURE;
        }
    }

    next_arg(argc, argv);
    int temp = 0;

    try {
        temp = std::stoi(argv[0]);
    } catch (const std::invalid_argument&) {
        std::cerr << "Error: Invalid temperature '" << argv[0] << "'\n";
        return EXIT_FAILURE;
    }

    next_arg(argc, argv);
    bool by_frame = argc && argv[0] == std::string("--frames");

    return print_mode(name, table, mode, temp, by_frame);
}

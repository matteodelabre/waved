/**
 * @file
 * SPDX-FileCopyrightText: 2021-2022 Mattéo Delabre <git.matteo@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "controller.hpp"
#include "generator.hpp"
#include "waveform_table.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <random>

void do_init(Waved::Generator& generator)
{
    auto update = generator.push_update(
        Waved::ModeKind::INIT,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Waved::Intensity>(1404 * 1872, 30)
    ).value();
    generator.wait_for(update);
}

void do_gradients(Waved::Generator& generator)
{
    constexpr std::size_t width = 50;
    constexpr std::size_t block_height = 100;
    constexpr std::size_t block_count = 16;
    constexpr std::size_t resol = 5;

    std::vector<Waved::Intensity> block_buffer(
        width * block_height * block_count
    );
    std::vector<Waved::Intensity> continuous_buffer(
        width * block_height * block_count
    );

    for (std::size_t i = 0; i < block_count; ++i) {
        std::fill(
            block_buffer.begin() + width * block_height * i,
            block_buffer.begin() + width * block_height * (i + 1),
            static_cast<Waved::Intensity>(i * 2)
        );
    }

    for (std::size_t i = 0; i < block_height * block_count; ++i) {
        std::fill(
            continuous_buffer.begin() + width * i,
            continuous_buffer.begin() + width * (i + 1),
            (i / 16 / resol) % 2 == 0
                ? static_cast<Waved::Intensity>(((i / resol) % 16) * 2)
                : static_cast<Waved::Intensity>(30 - ((i / resol) % 16) * 2)
        );
    }

    Waved::UpdateID last_update = 0;

    for (Waved::ModeID mode = 1; mode < 8; ++mode) {
        generator.push_update(
            mode,
            /* immediate = */ false,
            Waved::UpdateRegion{
                /* top = */ 136U,
                /* left = */ 127U + (mode - 1) * 175U,
                /* width = */ width,
                /* height = */ block_height * block_count
            },
            block_buffer
        );
        last_update = generator.push_update(
            mode,
            /* immediate = */ false,
            Waved::UpdateRegion{
                /* top = */ 136U,
                /* left = */ 127U + 50U + (mode - 1) * 175U,
                /* width = */ width,
                /* height = */ block_height * block_count
            },
            continuous_buffer
        ).value();
    }

    generator.wait_for(last_update);
}

void do_all_diff(Waved::Generator& generator)
{
    std::vector<Waved::Intensity> buffer(1404 * 1872);

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = (i % 16) * 2;
    }

    auto update = generator.push_update(
        Waved::ModeKind::GC16,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        buffer
    ).value();
    generator.wait_for(update);
}

void do_random(Waved::Generator& generator)
{
    std::vector<Waved::Intensity> buffer(1404 * 1872);
    std::mt19937 prng(424242);
    std::uniform_int_distribution<std::mt19937::result_type> distrib(0, 15);

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = distrib(prng) * 2;
    }

    auto update = generator.push_update(
        Waved::ModeKind::GC16,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        buffer
    ).value();
    generator.wait_for(update);
}

void do_spiral(Waved::Generator& generator)
{
    using namespace std::literals::chrono_literals;
    int count = 700;
    double resol = 4.;
    double resol_scaling = 0.044;
    int scale = 2;

    std::uint32_t stencil = 6;
    std::uint32_t width = 1404;
    std::uint32_t height = 1872;

    std::vector<Waved::Intensity> buffer(stencil * stencil, 0);
    Waved::UpdateID last_update = 0;

    for (int i = 0; i < count; ++i) {
        auto t = i / (resol + i * resol_scaling);
        auto ampl = std::exp(0.30635 * t);

        std::uint32_t x = width / 2 + std::round(std::cos(t) * ampl * scale);
        std::uint32_t y = height / 2 - std::round(std::sin(t) * ampl * scale);

        last_update = generator.push_update(
            Waved::ModeKind::A2,
            /* immediate = */ true,
            Waved::UpdateRegion{
                /* top = */ y, /* left = */ x,
                /* width = */ stencil, /* height = */ stencil
            },
            buffer
        ).value();
        std::this_thread::sleep_for(5ms);
    }

    generator.wait_for(last_update);
}

void do_image(Waved::Generator& generator)
{
    std::ifstream image{"./image.pgm"};

    if (!image) {
        std::cerr << "Open ./image.pgm: " << std::strerror(errno) << '\n';
        return;
    }

    // Read image header
    std::string line;
    int next_field = 0;
    int width = 0;
    int height = 0;
    int maxval = 0;

    while (next_field < 4 && std::getline(image, line)) {
        if (line[0] == '#') {
            continue;
        }

        std::istringstream stream{line};

        while (next_field < 4 && stream) {
            switch (next_field) {
            case 0:
            {
                std::string type;
                stream >> type;

                if (type != "P2") {
                    std::cerr << "Read ./image.pgm: Expected ASCII PGM "
                        "format (P2), got " << type << '\n';
                    return;
                }

                next_field += 1;
                break;
            }

            case 1:
                stream >> width;

                if (!stream.fail()) {
                    ++next_field;
                }
                break;

            case 2:
                stream >> height;

                if (!stream.fail()) {
                    ++next_field;
                }
                break;

            case 3:
                stream >> maxval;

                if (!stream.fail()) {
                    ++next_field;
                }
                break;
            }
        }
    }

    if (maxval == 0 || width == 0 || height == 0) {
        return;
    }

    // Create buffer from image data
    std::vector<Waved::Intensity> buffer(1404 * 1872, 0);

    for (std::size_t y = 0; y < 1872; ++y) {
        for (std::size_t x = 0; x < 1404; ++x) {
            std::getline(image, line);
            int val = std::stoi(line, nullptr, 10);
            buffer[y * 1404 + x] = (val * 16) / maxval * 2;
        }

        // Ignore overflowing pixels
        auto rem = 1404u - width;

        if (rem > 1404u) {
            rem = 0;
        }

        for (std::size_t x = 0; x < rem; ++x) {
            std::getline(image, line);
        }
    }

    auto update = generator.push_update(
        Waved::ModeKind::GC16,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        buffer
    ).value();
    generator.wait_for(update);
}

void print_help(std::ostream& out, const char* name)
{
#ifdef ENABLE_PERF_REPORT
    out << "Usage: " << name << " [-h|--help] [PERF_OUT]\n";
#else
    out << "Usage: " << name << " [-h|--help]\n";
#endif
    out << "Run waved tests.\n";
#ifdef ENABLE_PERF_REPORT
    out << "Dump a performance report to PERF_OUT (in CSV format).\n";
#endif
}

inline void next_arg(int& argc, const char**& argv)
{
    --argc;
    ++argv;
}

int main(int argc, const char** argv)
{
    using namespace std::literals::chrono_literals;
    const char* name = argv[0];
    next_arg(argc, argv);

    if (argc && (argv[0] == std::string("-h") || argv[0] == std::string("--help"))) {
        print_help(std::cout, name);
        return EXIT_SUCCESS;
    }

    auto wbf_path = Waved::WaveformTable::discover_wbf_file();

    if (!wbf_path) {
        std::cerr << "[init] Cannot find waveform file\n";
        return 1;
    } else {
        std::cerr << "[init] Using waveform file: " << *wbf_path << '\n';
    }

    auto table = Waved::WaveformTable::from_wbf(wbf_path->data());
    auto controller = Waved::Controller::open_remarkable2();
    Waved::Generator generator(controller, table);

#ifdef ENABLE_PERF_REPORT
    std::ofstream perf_report_out;

    if (argc) {
        perf_report_out.open(argv[0]);
        next_arg(argc, argv);
        generator.enable_perf_report(perf_report_out);
    }
#endif

    generator.start();

    std::cerr << "[test] Gradients\n";
    do_init(generator);
    do_gradients(generator);

    std::cerr << "[test] Image\n";
    do_init(generator);
    do_image(generator);

    std::cerr << "[test] All different values\n";
    do_init(generator);
    do_all_diff(generator);

    std::cerr << "[test] Random values\n";
    do_init(generator);
    do_random(generator);

    std::cerr << "[test] Spiral\n";
    do_init(generator);
    do_spiral(generator);

    generator.wait_for_all();
    return 0;
}

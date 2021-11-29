/**
 * @file
 * SPDX-FileCopyrightText: 2021 Mattéo Delabre <spam@delab.re>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.hpp"
#include "waveform_table.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <random>

void do_init(Display& display, const WaveformTable& table, int temp)
{
    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Intensity>(1404 * 1872, 30),
        &table.lookup(/* mode = */ 0, temp)
    );
}

void do_block_gradients(Display& display, const WaveformTable& table, int temp)
{
    constexpr std::size_t block_size = 100;
    constexpr std::size_t block_count = 16;
    std::vector<Intensity> buffer(block_size * block_size * block_count);

    for (std::size_t i = 0; i < block_count; ++i) {
        std::fill(
            buffer.begin() + block_size * block_size * i,
            buffer.begin() + block_size * block_size * (i + 1),
            static_cast<Intensity>(i * 2)
        );
    }

    for (Mode mode = 1; mode < 8; ++mode) {
        display.push_update(
            Region{
                /* top = */ 136,
                /* left = */ 200 + (mode - 1) * 150U,
                /* width = */ block_size,
                /* height = */ block_size * block_count
            },
            buffer,
            &table.lookup(mode, temp)
        );
    }
}

void do_continuous_gradients(Display& display, const WaveformTable& table, int temp)
{
    constexpr std::size_t block_size = 100;
    constexpr std::size_t block_count = 16;
    constexpr std::size_t resol = 5;
    std::vector<Intensity> buffer(block_size * block_size * block_count);

    for (std::size_t i = 0; i < block_size * block_count; ++i) {
        std::fill(
            buffer.begin() + block_size * i,
            buffer.begin() + block_size * (i + 1),
            (i / 16 / resol) % 2 == 0
                ? static_cast<Intensity>(((i / resol) % 16) * 2)
                : static_cast<Intensity>(30 - ((i / resol) % 16) * 2)
        );
    }

    for (Mode mode = 1; mode < 8; ++mode) {
        display.push_update(
            Region{
                /* top = */ 136,
                /* left = */ 200 + (mode - 1) * 150U,
                /* width = */ block_size,
                /* height = */ block_size * block_count
            },
            buffer,
            &table.lookup(mode, temp)
        );
    }
}

void do_all_diff(Display& display, const WaveformTable& table, int temp)
{
    std::vector<Intensity> buffer(1404 * 1872);

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = (i % 16) * 2;
    }

    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        buffer,
        &table.lookup(/* mode = */ 2, temp)
    );
}

void do_random(Display& display, const WaveformTable& table, int temp)
{
    std::vector<Intensity> buffer(1404 * 1872);
    std::mt19937 generator(424242);
    std::uniform_int_distribution<std::mt19937::result_type> distrib(0, 15);

    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = distrib(generator) * 2;
    }

    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        buffer,
        &table.lookup(/* mode = */ 2, temp)
    );
}

int main(int, const char**)
{
    using namespace std::literals::chrono_literals;

    auto wbf_path = WaveformTable::discover_wbf_file();

    if (!wbf_path) {
        std::cerr << "[init] Cannot find waveform file\n";
        return 1;
    } else {
        std::cerr << "[init] Using waveform file: " << *wbf_path << '\n';
    }

    auto table = WaveformTable::from_wbf(wbf_path->data());
    auto framebuffer_path = Display::discover_framebuffer();

    if (!framebuffer_path) {
        std::cerr << "[init] Cannot find framebuffer device\n";
        return 2;
    } else {
        std::cerr << "[init] Using framebuffer device: "
            << *framebuffer_path << '\n';
    }

    auto sensor_path = Display::discover_temperature_sensor();

    if (!sensor_path) {
        std::cerr << "[init] Cannot find temperature sensor\n";
        return 3;
    } else {
        std::cerr << "[init] Using temperature sensor: "
            << *sensor_path << '\n';
    }

    Display display{framebuffer_path->data(), sensor_path->data()};
    auto temp = display.get_temperature();
    std::cerr << "[init] Panel temperature: " << temp << " °C\n";

    display.start();

    std::cerr << "\n[test] Block gradients\n";
    do_init(display, table, temp);
    do_init(display, table, temp);
    do_block_gradients(display, table, temp);
    std::this_thread::sleep_for(15s);

    std::cerr << "\n[test] Continuous gradients\n";
    do_init(display, table, temp);
    do_init(display, table, temp);
    do_continuous_gradients(display, table, temp);
    std::this_thread::sleep_for(15s);

    std::cerr << "\n[test] All different values\n";
    do_init(display, table, temp);
    do_init(display, table, temp);
    do_all_diff(display, table, temp);
    std::this_thread::sleep_for(15s);

    std::cerr << "\n[test] Random values\n";
    do_init(display, table, temp);
    do_init(display, table, temp);
    do_random(display, table, temp);
    std::this_thread::sleep_for(15s);

    std::cerr << "\n[test] End\n";
    do_init(display, table, temp);
    do_init(display, table, temp);
    std::this_thread::sleep_for(6s);
    return 0;
}

#include "display.hpp"
#include "waveform_table.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main(int, const char**)
{
    using namespace std::literals::chrono_literals;

    auto wbf_path = WaveformTable::discover_wbf_file();

    if (!wbf_path) {
        std::cerr << "Cannot find waveform file\n";
        return 1;
    } else {
        std::cerr << "Using waveform file: " << *wbf_path << '\n';
    }

    auto table = WaveformTable::from_wbf(wbf_path->data());
    auto framebuffer_path = Display::discover_framebuffer();

    if (!framebuffer_path) {
        std::cerr << "Cannot find framebuffer device\n";
        return 2;
    } else {
        std::cerr << "Using framebuffer device: " << *framebuffer_path << '\n';
    }

    auto sensor_path = Display::discover_temperature_sensor();

    if (!sensor_path) {
        std::cerr << "Cannot find temperature sensor\n";
        return 3;
    } else {
        std::cerr << "Using temperature sensor: " << *sensor_path << '\n';
    }

    Display display{framebuffer_path->data(), sensor_path->data()};
    auto temp = display.get_temperature();
    std::cerr << "Panel temperature: " << temp << " °C\n";

    // Send INIT for whole screen
    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Intensity>(1404 * 1872, 30),
        &table.lookup(/* mode = */ 0, temp)
    );

    // Send block intensity gradients for each mode
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

    display.start();
    std::this_thread::sleep_for(8s);

    // Send INIT again
    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Intensity>(1404 * 1872, 30),
        &table.lookup(/* mode = */ 0, temp)
    );

    // Send continuous gradients for each mode
    constexpr std::size_t resol = 5;

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

    std::this_thread::sleep_for(8s);
    return 0;
}

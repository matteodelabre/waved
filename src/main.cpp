#include "display.hpp"
#include "waveform_table.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main(int, const char**)
{
    using namespace std::literals::chrono_literals;

    // TODO: Auto-detect appropriate WBF file from barcode
    auto table = WaveformTable::from_wbf("/usr/share/remarkable/320_R349_AF0411_ED103TC2U2_VB3300-KCD_TC.wbf");

    Display display;
    auto temp = display.get_temperature();
    std::cerr << "Panel temperature: " << temp << " °C\n";

    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Intensity>(1404 * 1872, 30),
        &table.lookup(/* mode = */ 0, temp)
    );

    display.push_update(
        Region{
            /* top = */ 100, /* left = */ 100,
            /* width = */ 1204, /* height = */ 1672
        },
        std::vector<Intensity>(1204 * 1672, 30),
        &table.lookup(/* mode = */ 0, temp)
    );

    constexpr std::size_t cell_size = 100;
    constexpr std::size_t cell_count = 16;
    std::vector<Intensity> buffer(cell_size * cell_size * cell_count);

    for (std::size_t i = 0; i < cell_count; ++i) {
        std::fill(
            buffer.begin() + cell_size * cell_size * i,
            buffer.begin() + cell_size * cell_size * (i + 1),
            static_cast<Intensity>(i * 2)
        );
    }

    for (Mode mode = 1; mode < 8; ++mode) {
        display.push_update(
            Region{
                /* top = */ 136,
                /* left = */ 200 + (mode - 1) * 150U,
                /* width = */ cell_size,
                /* height = */ cell_size * cell_count
            },
            buffer,
            &table.lookup(mode, temp)
        );
    }

    display.start();

    // TODO: Synchronize with update processing
    std::this_thread::sleep_for(6s);
    return 0;
}

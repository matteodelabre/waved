#include "display.hpp"
#include "waveform_table.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

int main(int, const char**)
{
    using namespace std::literals::chrono_literals;

    auto table = WaveformTable::from_wbf("/usr/share/remarkable/320_R349_AF0411_ED103TC2U2_VB3300-KCD_TC.wbf");

    Display display;
    auto temp = display.get_temperature();
    std::cerr << "Panel temperature: " << temp << " °C\n";

    display.push_update(
        Region{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Intensity>(1404 * 1872, 0),
        &table.lookup(/* mode = */ 0, temp)
    );

    display.push_update(
        Region{
            /* top = */ 100, /* left = */ 104,
            /* width = */ 1204, /* height = */ 1664
        },
        std::vector<Intensity>(1204 * 1664, 0),
        &table.lookup(/* mode = */ 0, temp)
    );

    for (Mode mode = 1; mode < 8; ++mode) {
        for (Intensity val = 0; val < 32; ++val) {
            display.push_update(
                Region{
                    /* top = */ val * 48U,
                    /* left = */ 200 + (mode - 1) * 150U,
                    /* width = */ 100,
                    /* height = */ 48
                },
                std::vector<Intensity>(100 * 48, val),
                &table.lookup(mode, temp)
            );
        }
    }

    display.start();
    std::this_thread::sleep_for(100s);
    return 0;
}

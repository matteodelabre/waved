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

    Display::Update update;

    update.region.top = 0;
    update.region.left = 0;
    update.region.width = Display::screen_width;
    update.region.height = Display::screen_height;
    update.waveform = &table.lookup(/* mode = */ 0, temp);
    update.buffer = std::vector<Intensity>(
        update.region.width * update.region.height, 0);
    display.push_update(std::move(update));

    update.region.top = 100;
    update.region.left = 104;
    update.region.width = Display::screen_width - 208;
    update.region.height = Display::screen_height - 200;
    update.waveform = &table.lookup(/* mode = */ 0, temp);
    update.buffer = std::vector<Intensity>(
        update.region.width * update.region.height, 0);
    display.push_update(std::move(update));

    update.region.top = 200;
    update.region.left = 208;
    update.region.width = Display::screen_width - 416;
    update.region.height = Display::screen_height - 400;
    update.waveform = &table.lookup(/* mode = */ 0, temp);
    update.buffer = std::vector<Intensity>(
        update.region.width * update.region.height, 0);
    display.push_update(std::move(update));

    for (Mode mode = 1; mode < 8; ++mode) {
        for (Intensity val = 0; val < 32; val += 4) {
            update.region.top = 200 + (val / 4 * 104);
            update.region.left = 104 + (mode * 104);
            update.region.width = 104;
            update.region.height = 104;
            update.waveform = &table.lookup(mode, temp);
            update.buffer = std::vector<Intensity>(
                update.region.width * update.region.height, val);
            display.push_update(std::move(update));
        }
    }

    display.start();
    std::this_thread::sleep_for(100s);
    return 0;
}

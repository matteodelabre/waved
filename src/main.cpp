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
    display.start();

    Display::Update update;
    update.region.top = 0;
    update.region.left = 0;
    update.region.width = Display::screen_width;
    update.region.height = Display::screen_height;
    update.waveform = &table.lookup(/* mode = */ 0, /* temp = */ 26);
    update.buffer = std::vector<Intensity>(Display::screen_size, 0);
    display.queue_update(std::move(update));

    std::this_thread::sleep_for(100s);
    return 0;
}

#include "display.hpp"
#include "ipc.cpp"
#include <semaphore.h> // sem_open

#define DEBUG
#define DEBUG_DIRTY
int msg_q_id = 0x2257c;
swtfb::ipc::Queue MSGQ(msg_q_id);


uint16_t *SHARED_MEM;
#define WIDTH 1404
#define HEIGHT 1872

// convert an RGB565_LE to floating point number
// TODO: verify this is correct
constexpr float to_float(uint16_t c)
{
    // 0.21 R + 0.72 G + 0.07 B
    return ((c >> 11) & 31) * (0.21 / 31)  // red
         + ((c >> 5)  & 63) * (0.72 / 63)  // green
         + (c         & 31) * (0.07 / 31); // blue
}

void do_update(Waved::Display &display, const swtfb::swtfb_update &s)
{
    auto mxcfb_update = s.mdata.update;
    auto rect = mxcfb_update.update_region;
    std::vector<Waved::Intensity> buffer(rect.width * rect.height);

#ifdef DEBUG_DIRTY
    std::cerr << "HANDLING UPDATE\n";
    std::cerr << "Dirty Region: " << rect.left << " " << rect.top << " "
              << rect.width << " " << rect.height << '\n';
#endif

    // TODO: verify that the rectangle is within SHARED_MEM's bounds, otherwise the server will crash
    for (unsigned int i = 0; i < rect.height; i++) {
        for (unsigned int j = 0; j < rect.width; j++) {
            // intensity is from 0 - 30, evens only
            buffer[j + i*rect.width] = uint8_t(to_float(SHARED_MEM[j+rect.left + (i+rect.top)*WIDTH]) * 15) * 2;
        }
    }

    Waved::ModeID mode = mxcfb_update.waveform_mode;
    bool full_update = mxcfb_update.update_mode;
    bool immediate = false;

    if (mode == /* fast */ 1 && !full_update) {
        immediate = true;
    }

    Waved::UpdateRegion region;
    region.top = rect.top;
    region.left = rect.left;
    region.width = rect.width;
    region.height = rect.height;

    display.push_update(mode, immediate, region, buffer);
}

int main(int, const char**)
{
    auto wbf_path = Waved::WaveformTable::discover_wbf_file();

    if (!wbf_path) {
        std::cerr << "[init] Cannot find waveform file\n";
        return 1;
    } else {
        std::cerr << "[init] Using waveform file: " << *wbf_path << '\n';
    }

    auto table = Waved::WaveformTable::from_wbf(wbf_path->data());
    auto framebuffer_path = Waved::Display::discover_framebuffer();

    if (!framebuffer_path) {
        std::cerr << "[init] Cannot find framebuffer device\n";
        return 2;
    } else {
        std::cerr << "[init] Using framebuffer device: "
            << *framebuffer_path << '\n';
    }

    auto sensor_path = Waved::Display::discover_temperature_sensor();

    if (!sensor_path) {
        std::cerr << "[init] Cannot find temperature sensor\n";
        return 3;
    } else {
        std::cerr << "[init] Using temperature sensor: "
            << *sensor_path << '\n';
    }

    Waved::Display display{
        framebuffer_path->data(),
        sensor_path->data(),
        std::move(table),
    };

    display.start();

    auto init_update = display.push_update(
        Waved::ModeKind::INIT,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Waved::Intensity>(1404 * 1872, 30)
    ).value();
    display.wait_for(init_update);

    SHARED_MEM = swtfb::ipc::get_shared_buffer();

    while (true) {
        auto buf = MSGQ.recv();
        switch (buf.mtype) {
        case swtfb::ipc::UPDATE_t:
            do_update(display, buf);
            break;

        case swtfb::ipc::XO_t:
            // XO_t means that buf.xochitl_update is filled in and needs to
            // be forwarded to xochitl or translated to a compatible format
            // with waved server
            std::cerr << "(Unhandled XO_t message)\n";
            break;

        case swtfb::ipc::WAIT_t:
            std::cerr << "(Unhandled wait message)\n";
            // TODO
            break;

        default:
            std::cerr << "Error, unknown message type\n";
            break;
        }
    }
}

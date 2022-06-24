#include "controller.hpp"
#include "generator.hpp"
#include "ipc.cpp"
#include <fstream>
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

void do_update(Waved::Generator& generator, const swtfb::swtfb_update& s)
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

    generator.push_update(mode, immediate, region, buffer);
}

void print_help(std::ostream& out, const char* name)
{
#ifdef ENABLE_PERF_REPORT
    out << "Usage: " << name << " [-h|--help] [PERF_OUT]\n";
#else
    out << "Usage: " << name << " [-h|--help]\n";
#endif
    out << "Run an rm2fb server using waved.\n";
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

    auto init_update = generator.push_update(
        Waved::ModeKind::INIT,
        /* immediate = */ false,
        Waved::UpdateRegion{
            /* top = */ 0, /* left = */ 0,
            /* width = */ 1404, /* height = */ 1872
        },
        std::vector<Waved::Intensity>(1404 * 1872, 30)
    ).value();
    generator.wait_for(init_update);

    SHARED_MEM = swtfb::ipc::get_shared_buffer();

    while (true) {
        auto buf = MSGQ.recv();
        switch (buf.mtype) {
        case swtfb::ipc::UPDATE_t:
            do_update(generator, buf);
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

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

void do_update(Waved::Display &display, const swtfb::swtfb_update &s) {

  auto mxcfb_update = s.mdata.update;
  auto rect = mxcfb_update.update_region;
  std::vector<Waved::Intensity> buffer(rect.width * rect.height);

#ifdef DEBUG_DIRTY
  std::cerr << "Dirty Region: " << rect.left << " " << rect.top << " "
            << rect.width << " " << rect.height << std::endl;
#endif

  // TODO: verify that the rectangle is within SHARED_MEM's bounds, otherwise the server will crash
  for (unsigned int i = 0; i < rect.height; i++) {
    for (unsigned int j = 0; j < rect.width; j++) {
      // intensity is from 0 - 30, evens only
      buffer[j + i*rect.width] = uint8_t(to_float(SHARED_MEM[j+rect.left + (i+rect.top)*WIDTH]) * 15) * 2;
    }
  }


  // There are three update modes on the rm2. But they are mapped to the five
  // rm1 modes as follows:
  //
  // 0: init (same as GL16)
  // 1: DU - direct update, fast
  // 2: GC16 - high fidelity (slow)
  // 3: GL16 - what the rm is using
  // 8: highlight (same as high fidelity)
  int waveform = mxcfb_update.waveform_mode;
  if (waveform > 3 && waveform != 8) {
    waveform = 3;
    // TODO: fb.ClearGhosting();
  }

  if (waveform < 0) {
    waveform = 3;
    // TODO: fb.ClearGhosting();
  }

  // full = 1, partial = 0
  auto update_mode = mxcfb_update.update_mode;

  auto flags = update_mode & 0x1;

  // TODO: Get sync from client (wait for second ioctl? or look at stack?)
  // There are only two occasions when the original rm1 library sets sync to
  // true. Currently we detect them by the other data. Ideally we should
  // correctly handle the corresponding ioctl (empty rect and flags == 2?).
  if (waveform == /*init*/ 0 && update_mode == /* full */ 1) {
    flags |= 2;
    std::cerr << "SERVER: sync" << std::endl;
  } else if (rect.left == 0 && rect.top > 1800 &&
             waveform == /* grayscale */ 3 && update_mode == /* full */ 1) {
    std::cerr << "server sync, x2: " << rect.width << " y2: " << rect.height
              << std::endl;
    flags |= 2;
  }

  if (waveform == /* fast */ 1 && update_mode == /* partial */ 0) {
    // fast draw
    flags = 4;
  }

#ifdef DEBUG
  std::cerr << "do_update " << std::endl;
  std::cerr << "mxc: waveform_mode " << mxcfb_update.waveform_mode << std::endl;
  std::cerr << "mxc: update mode " << mxcfb_update.update_mode << std::endl;
  std::cerr << "mxc: update marker " << mxcfb_update.update_marker << std::endl;
  std::cerr << "final: waveform " << waveform;
  std::cerr << " flags " << flags << std::endl << std::endl;
#endif

  Waved::Region region;
  region.top = rect.top;
  region.left = rect.left;
  region.width = rect.width;
  region.height = rect.height;

  display.push_update(
    waveform,
    region,
    buffer
  );

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

  SHARED_MEM = swtfb::ipc::get_shared_buffer();
  while (true) {
    auto buf = MSGQ.recv();
    switch (buf.mtype) {
    case swtfb::ipc::UPDATE_t: {
			std::cerr << "HANDLING UPDATE\n";
      do_update(display, buf);
    } break;
    case swtfb::ipc::XO_t: {
      // XO_t means that buf.xochitl_update is filled in and needs to be forwarded to xochitl or translated
      // to a compatible format with waved server
			std::cerr << "HANDLING XO_t\n";
    } break;
    case swtfb::ipc::WAIT_t: {
			std::cerr << "HANDLING WAIT\n";
      break;
//      fb.WaitForLastUpdate();
      sem_t* sem = sem_open(buf.mdata.wait_update.sem_name, O_CREAT, 0644, 0);
      if (sem != NULL) {
        sem_post(sem);
        sem_close(sem);
      }
    } break;

    default:
      std::cerr << "Error, unknown message type" << std::endl;
    }
  }
}

// This code is adapted from ddvk/remarkable2-framebuffer
#pragma once

#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "mxcfb.hpp"


namespace swtfb {
struct xochitl_data {
  int x1;
  int y1;
  int x2;
  int y2;

  int waveform;
  int flags;
};

struct wait_sem_data {
  char sem_name[512];
};

struct swtfb_update {
  long mtype;

  union {
    xochitl_data xochitl_update;

    struct mxcfb_update_data update;
    wait_sem_data wait_update;

  };
};

namespace ipc {

using namespace std;
enum MSG_TYPE { INIT_t = 1, UPDATE_t, XO_t, WAIT_t };

const int maxWidth = 1404;
const int maxHeight = 1872;
const int BUF_SIZE = maxWidth * maxHeight *
                     sizeof(uint16_t); // hardcoded size of display mem for rM2
int SWTFB_FD = 0;

// TODO: allow multiple shared buffers in one process?
static uint16_t *get_shared_buffer(string name = "/swtfb.01") {
  if (name[0] != '/') {
    name = "/" + name;
  }

  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0755);

  if (fd == -1 && errno == 13) {
    fd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0755);
  }

  if (fd < 3) {
    fprintf(stderr, "SHM FD: %i, errno: %i\n", fd, errno);
  }
  SWTFB_FD = fd;

  if (ftruncate(fd, BUF_SIZE)) {
    fprintf(stderr, "COULDN'T TRUNCATE SHARED MEM: /dev/shm%s, errno: %i\n",
            name.c_str(), errno);

  };
  uint16_t *mem =
      (uint16_t *)mmap(NULL, BUF_SIZE, PROT_WRITE, MAP_SHARED, fd, 0);

  fprintf(stderr, "OPENED SHARED MEM: /dev/shm%s at %p, errno: %i\n",
          name.c_str(), mem, errno);
  return mem;
}

#define SWTFB1_UPDATE 1
class Queue {
public:
  unsigned long id;
  int msqid = -1;

  void init() { msqid = msgget(id, IPC_CREAT | 0600); }

  Queue(int id) : id(id) { init(); }

  swtfb_update recv() {
    swtfb_update buf;
    errno = 0;
    auto len = msgrcv(msqid, &buf, sizeof(buf), 0, 0);
    if (len >= 0) {
      return buf;
    } else {
      perror("Error recv msgbuf");
    }

    return {};
  }

  void destroy() { msgctl(msqid, IPC_RMID, 0); };
};
}; // namespace ipc
}; // namespace swtfb

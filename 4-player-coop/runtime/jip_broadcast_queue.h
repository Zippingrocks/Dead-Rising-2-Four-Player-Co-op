#pragma once
#include <stdint.h>
#include <string.h>

namespace coop_jip {
struct Broadcast {
  uint32_t header[5]{};
  unsigned char payload[255]{};
  int type = 0;
  int size = 0;
};

// The caller owns synchronization and the game/scene lifetime. Never evict an older event.
template<unsigned Capacity> class BroadcastQueue {
  static_assert(Capacity > 0, "A queue needs storage");
  Broadcast entries_[Capacity]{};
  unsigned head_ = 0;
  unsigned count_ = 0;
public:
  unsigned Count() const { return count_; }
  bool Push(const uint32_t* header, const void* payload, int type, int size) {
    if (!header || size < 0 || size > 255 || (size && !payload) ||
        type <= 0 || type > 0x9D || (header[1] & 255) != static_cast<unsigned>(size) ||
        ((header[1] >> 8) & 255) != static_cast<unsigned>(type) || count_ == Capacity) return false;
    Broadcast& entry = entries_[(head_ + count_) % Capacity];
    memcpy(entry.header, header, sizeof(entry.header));
    if (size) memcpy(entry.payload, payload, size);
    entry.type = type;
    entry.size = size;
    ++count_;
    return true;
  }
  bool Pop(Broadcast& entry) {
    if (!count_) return false;
    entry = entries_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }
  void Clear() { head_ = count_ = 0; }
};
}

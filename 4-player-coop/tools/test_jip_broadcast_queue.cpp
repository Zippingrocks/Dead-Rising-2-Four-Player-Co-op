#include "../runtime/jip_broadcast_queue.h"
#include <assert.h>
#include <stdio.h>

int main() {
  coop_jip::BroadcastQueue<3> queue;
  coop_jip::Broadcast result;
  uint32_t header[5]{17, (0x16 << 8) | 4, 0x3F800000, 0x40000000, 0x40400000};
  unsigned char payload[255]{1, 2, 3, 4};
  assert(!queue.Pop(result));
  assert(queue.Push(header, payload, 0x16, 4));
  header[0] = 18;
  payload[0] = 9;
  assert(queue.Push(header, payload, 0x16, 4));
  assert(queue.Pop(result));
  assert(result.header[0] == 17 && result.payload[0] == 1 && result.header[4] == 0x40400000);
  for (int i = 0; i < 2000; ++i) {
    header[0] = static_cast<uint32_t>(19 + i);
    assert(queue.Push(header, payload, 0x16, 4));
    assert(queue.Pop(result));
    assert(result.header[0] == static_cast<uint32_t>(18 + i));
  }
  assert(queue.Push(header, payload, 0x16, 4));
  assert(queue.Push(header, payload, 0x16, 4));
  assert(!queue.Push(header, payload, 0x16, 4));
  assert(queue.Count() == 3);
  queue.Clear();
  assert(!queue.Pop(result));
  assert(!queue.Push(nullptr, payload, 0x16, 4));
  assert(!queue.Push(header, nullptr, 0x16, 4));
  assert(!queue.Push(header, payload, 0x16, -1));
  assert(!queue.Push(header, payload, 0x16, 256));
  assert(!queue.Push(header, payload, 0, 4));
  assert(!queue.Push(header, payload, 0x9E, 4));
  assert(!queue.Push(header, payload, 0x17, 4));
  assert(!queue.Push(header, payload, 0x16, 3));
  assert(queue.Count() == 0);
  header[1] = (0x16 << 8) | 255;
  payload[254] = 123;
  assert(queue.Push(header, payload, 0x16, 255));
  header[1] = 0x16 << 8;
  assert(queue.Push(header, nullptr, 0x16, 0));
  assert(queue.Pop(result) && result.size == 255 && result.payload[254] == 123);
  assert(queue.Pop(result) && result.size == 0);
  assert(!queue.Pop(result));
  puts("JIP broadcast queue: owned bytes, FIFO/wraparound, no eviction, bounds, reset and empty payload passed");
}

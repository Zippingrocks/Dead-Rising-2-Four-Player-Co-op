#pragma once
#include <stdint.h>

namespace coop_nfs {
struct Request {
  uint32_t vtable, hash, buffer, size, user, handle, transfer, link, async, flags;
};
static_assert(sizeof(Request) == 0x28, "PC NFS request stride");

struct Owner { int index = -1; int wireHandle = -1; int link = -1; int state = -1; };

inline bool Resolve(const Request* requests, uint32_t count, int index,
                    uint32_t transferAddress, Owner& owner) {
  owner = {};
  if (!requests || count == 0 || count > 32 || index < 0 || static_cast<uint32_t>(index) >= count ||
      !transferAddress) return false;
  const Request& request = requests[index];
  const int state = (request.flags >> 2) & 7;
  if (request.vtable != 0xCB8448 || (request.flags & 3) != 2 || !(request.flags & 0x100) ||
      (state != 4 && state != 6) || request.transfer != transferAddress ||
      request.handle >= 32 || request.link >= 4) return false;
  owner = {index, static_cast<int>(request.handle), static_cast<int>(request.link), state};
  return true;
}

inline bool HasRoutableSegment(const Owner& owner, uint32_t descriptor, bool hasData, int availableSize) {
  if (owner.index < 0 || owner.index >= 32 || owner.wireHandle < 0 || owner.wireHandle >= 32 ||
      owner.link < 0 || owner.link >= 4 || (descriptor & 0xFF) != static_cast<uint32_t>(owner.wireHandle))
    return false;
  const uint32_t bytes = (descriptor >> 9) & 0x7FF;
  // The native metadata segment has no payload pointer; it initializes the receiver before sequence 1.
  if (descriptor & 0x100) return (descriptor >> 20) == 0 && bytes == 0;
  return hasData && bytes > 0 && availableSize >= static_cast<int>(bytes);
}
}

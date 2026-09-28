#include "../runtime/nfs_request_owner.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  coop_nfs::Request requests[32]{};
  coop_nfs::Owner owner;
  requests[1] = {0xCB8448, 0xCAED3B91, 0x5000, 16000, 0x6000, 0, 0x7000, 2, 0, 0x55A};
  assert(coop_nfs::Resolve(requests, 32, 1, 0x7000, owner));
  assert(owner.index == 1 && owner.wireHandle == 0 && owner.link == 2 && owner.state == 6);
  assert(!coop_nfs::Resolve(requests, 32, 0, 0x7000, owner));
  assert(owner.index == -1);
  for (const int index : {-1, 32, 100}) assert(!coop_nfs::Resolve(requests, 32, index, 0x7000, owner));
  assert(!coop_nfs::Resolve(requests, 33, 1, 0x7000, owner));
  assert(!coop_nfs::Resolve(nullptr, 32, 1, 0x7000, owner));
  assert(!coop_nfs::Resolve(requests, 32, 1, 0x8000, owner));
  for (const uint32_t flags : {0x54EU, 0x45AU, 0x559U, 0x552U}) {
    requests[1].flags = flags;
    const bool activeSend = flags == 0x552U;
    assert(coop_nfs::Resolve(requests, 32, 1, 0x7000, owner) == activeSend);
  }
  requests[1].flags = 0x55A;
  requests[1].link = 4;
  assert(!coop_nfs::Resolve(requests, 32, 1, 0x7000, owner));
  requests[1].link = 3;
  requests[1].handle = 32;
  assert(!coop_nfs::Resolve(requests, 32, 1, 0x7000, owner));
  requests[1].handle = 0;
  requests[1].vtable = 0;
  assert(!coop_nfs::Resolve(requests, 32, 1, 0x7000, owner));

  owner = {1, 0, 2, 4};
  assert(coop_nfs::HasRoutableSegment(owner, 0x100, false, 0));
  assert(coop_nfs::HasRoutableSegment(owner, 0x100, false, 1078));
  const uint32_t payload = (1U << 20) | (1000U << 9);
  assert(coop_nfs::HasRoutableSegment(owner, payload, true, 1008));
  assert(!coop_nfs::HasRoutableSegment(owner, payload, false, 1008));
  assert(!coop_nfs::HasRoutableSegment(owner, payload, true, 999));
  assert(!coop_nfs::HasRoutableSegment(owner, payload | 1, true, 1008));
  assert(!coop_nfs::HasRoutableSegment(owner, 0x100100, false, 0));
  assert(!coop_nfs::HasRoutableSegment(owner, 0x300, false, 0));
  assert(!coop_nfs::HasRoutableSegment(owner, 0, false, 1078));
  assert(!coop_nfs::HasRoutableSegment({}, 0x100, false, 0));
  owner = {2, 7, 3, 4};
  assert(coop_nfs::HasRoutableSegment(owner, 0x107, false, 0));
  assert(coop_nfs::HasRoutableSegment(owner, payload | 7, true, 1008));
  assert(!coop_nfs::HasRoutableSegment(owner, payload | 7, true, -1));
  puts("NFS request owner: ownership, bounds, header-only metadata, and payload routing passed");
}

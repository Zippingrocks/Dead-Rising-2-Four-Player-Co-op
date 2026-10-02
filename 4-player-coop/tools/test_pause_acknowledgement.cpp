#include "../runtime/pause_acknowledgement.h"
#include <cassert>
#include <cstdio>

int main() {
  using coop_pause::Acknowledgement;
  for (int owner = 0; owner < 4; ++owner) {
    int realVotes = 1; // Owner's original request remains native.
    for (int local = 0; local < 4; ++local) {
      if (local == owner) continue;
      assert(coop_pause::Classify(local, owner, owner) == Acknowledgement::Forward);
      ++realVotes;
      for (int peer = 0; peer < 4; ++peer) {
        if (peer == owner || peer == local) continue;
        assert(coop_pause::Classify(local, peer, owner) == Acknowledgement::RedundantPeerReply);
      }
    }
    assert(realVotes == 4);
  }
  assert(coop_pause::Classify(0, 0, 1) == Acknowledgement::InvalidContext);
  assert(coop_pause::Classify(0, 1, 0) == Acknowledgement::InvalidContext);
  assert(coop_pause::Classify(4, 1, 0) == Acknowledgement::InvalidContext);
  assert(coop_pause::Classify(1, -1, 0) == Acknowledgement::InvalidContext);
  assert(coop_pause::Classify(1, 0, 4) == Acknowledgement::InvalidContext);
  int event = 0, scene = 0, unrelated = 0;
  coop_pause::SendContext context{&event, &scene};
  for (unsigned char previousMasks = 0; previousMasks < 16; ++previousMasks) {
    // Model native validation after any subset of other peers' replies arrived first.
    unsigned char alreadyPaused = previousMasks ? 1 : 0;
    assert(coop_pause::PreserveOriginalRequest(true, &context, &event, &scene, alreadyPaused));
    assert(alreadyPaused == 0);
  }
  unsigned char alreadyPaused = 1;
  assert(!coop_pause::PreserveOriginalRequest(false, &context, &event, &scene, alreadyPaused));
  assert(!coop_pause::PreserveOriginalRequest(true, &context, &unrelated, &scene, alreadyPaused));
  assert(!coop_pause::PreserveOriginalRequest(true, &context, &event, &unrelated, alreadyPaused));
  assert(!coop_pause::PreserveOriginalRequest(true, nullptr, &event, &scene, alreadyPaused));
  assert(alreadyPaused == 1);
  std::puts("Pause acknowledgements: all four originators retain all four real votes; peer reply loops and invalid contexts classified");
  std::puts("Pause reply metadata: reordered peer masks cannot turn an acknowledgement into a new menu request; unrelated/rejected events untouched");
}

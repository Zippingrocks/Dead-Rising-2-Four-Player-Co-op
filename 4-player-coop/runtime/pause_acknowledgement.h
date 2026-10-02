#pragma once

namespace coop_pause {
struct SendContext { const void* event; const void* scene; };

inline bool PreserveOriginalRequest(bool accepted, const SendContext* context,
                                    const void* event, const void* scene, unsigned char& alreadyPaused) {
  if (!accepted || !context || context->event != event || context->scene != scene) return false;
  alreadyPaused = 0;
  return true;
}

enum class Acknowledgement { Forward, RedundantPeerReply, InvalidContext };

inline Acknowledgement Classify(int local, int sender, int owner) {
  if (local < 0 || local > 3 || sender < 0 || sender > 3 || owner < 0 || owner > 3 ||
      local == owner || local == sender) return Acknowledgement::InvalidContext;
  return sender == owner ? Acknowledgement::Forward : Acknowledgement::RedundantPeerReply;
}
}

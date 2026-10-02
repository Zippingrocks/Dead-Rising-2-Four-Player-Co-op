#include "../runtime/matchmaking_policy.h"

#include <assert.h>
#include <string.h>

int main() {
  static_assert(coop_matchmaking::kMemberLimit == 4, "four-player lobby limit changed");
  assert(strcmp(coop_matchmaking::kProtocolKey, "dr2_4p_protocol") == 0);
  assert(coop_matchmaking::IsCompatible("1"));
  assert(!coop_matchmaking::IsCompatible(nullptr));
  assert(!coop_matchmaking::IsCompatible(""));
  assert(!coop_matchmaking::IsCompatible("0"));
  assert(!coop_matchmaking::IsCompatible("2"));
  return 0;
}

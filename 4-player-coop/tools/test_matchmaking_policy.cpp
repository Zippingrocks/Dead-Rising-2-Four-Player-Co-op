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
  assert(coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamMatchmaking"));
  assert(coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamAPI_RunCallbacks"));
  assert(!coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamNetworking"));
  assert(!coop_matchmaking::IsRequiredProductionImport("KERNEL32.dll", "SteamMatchmaking"));
  assert(!coop_matchmaking::IsRequiredProductionImport(nullptr, "SteamMatchmaking"));
  assert(!coop_matchmaking::IsRequiredProductionImport("steam_api.dll", nullptr));
  return 0;
}

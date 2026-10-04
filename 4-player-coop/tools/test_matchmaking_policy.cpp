#include "../runtime/matchmaking_policy.h"

#include <assert.h>
#include <string.h>

int main() {
  static_assert(coop_matchmaking::kMemberLimit == 4, "four-player lobby limit changed");
  static_assert(coop_matchmaking::kPublicLobbyType == 2, "Steam public lobby value changed");
  static_assert(coop_matchmaking::kInvisibleLobbyType == 3, "Steam invisible lobby value changed");
  assert(strcmp(coop_matchmaking::kProtocolKey, "dr2_4p_protocol") == 0);
  assert(coop_matchmaking::IsCompatible("1"));
  assert(!coop_matchmaking::IsCompatible(nullptr));
  assert(!coop_matchmaking::IsCompatible(""));
  assert(!coop_matchmaking::IsCompatible("0"));
  assert(!coop_matchmaking::IsCompatible("2"));
  static_assert(coop_matchmaking::kFriendsSetRichPresenceSlot == 37, "SteamFriends014 ABI changed");
  static_assert(coop_matchmaking::kFriendsGetFriendRichPresenceSlot == 39, "SteamFriends014 ABI changed");
  static_assert(coop_matchmaking::kFriendsRequestFriendRichPresenceSlot == 42, "SteamFriends014 ABI changed");
  assert(coop_matchmaking::IsMissing(nullptr));
  assert(coop_matchmaking::IsMissing(""));
  assert(!coop_matchmaking::IsMissing("0"));
  assert(!coop_matchmaking::IsMissing("1"));
  assert(coop_matchmaking::HasPeerProof("1", nullptr));
  assert(coop_matchmaking::HasPeerProof(nullptr, "1"));
  assert(coop_matchmaking::HasPeerProof("", "1"));
  assert(!coop_matchmaking::HasPeerProof(nullptr, nullptr));
  assert(!coop_matchmaking::HasPeerProof("", ""));
  assert(!coop_matchmaking::HasPeerProof("0", "1"));
  assert(!coop_matchmaking::HasPeerProof("2", "1"));
  assert(coop_matchmaking::VisibleProductionLobbyType(3) == 2);
  assert(coop_matchmaking::VisibleProductionLobbyType(2) == 2);
  assert(coop_matchmaking::VisibleProductionLobbyType(1) == 1);
  assert(coop_matchmaking::VisibleProductionLobbyType(0) == 0);
  assert(!coop_matchmaking::ShouldLogAdmissionAttempt(0));
  assert(coop_matchmaking::ShouldLogAdmissionAttempt(1));
  assert(coop_matchmaking::ShouldLogAdmissionAttempt(coop_matchmaking::kAdmissionLogBurst));
  assert(!coop_matchmaking::ShouldLogAdmissionAttempt(coop_matchmaking::kAdmissionLogBurst + 1));
  assert(coop_matchmaking::ShouldLogAdmissionAttempt(coop_matchmaking::kAdmissionLogInterval));
  assert(!coop_matchmaking::ShouldLogAdmissionAttempt(coop_matchmaking::kAdmissionLogInterval + 1));
  assert(coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamMatchmaking"));
  assert(coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamFriends"));
  assert(coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamAPI_RunCallbacks"));
  assert(!coop_matchmaking::IsRequiredProductionImport("steam_api.dll", "SteamNetworking"));
  assert(!coop_matchmaking::IsRequiredProductionImport("KERNEL32.dll", "SteamMatchmaking"));
  assert(!coop_matchmaking::IsRequiredProductionImport(nullptr, "SteamMatchmaking"));
  assert(!coop_matchmaking::IsRequiredProductionImport("steam_api.dll", nullptr));
  return 0;
}

#pragma once

#include <string.h>

namespace coop_matchmaking {

// Increment this value whenever network-visible behavior becomes incompatible.
constexpr char kProtocolKey[] = "dr2_4p_protocol";
constexpr char kProtocolValue[] = "1";
constexpr int kMemberLimit = 4;
constexpr int kPublicLobbyType = 2;
constexpr int kInvisibleLobbyType = 3;
constexpr long kAdmissionLogBurst = 8;
constexpr long kAdmissionLogInterval = 500;
constexpr int kFriendsSetRichPresenceSlot = 37;
constexpr int kFriendsGetFriendRichPresenceSlot = 39;
constexpr int kFriendsRequestFriendRichPresenceSlot = 42;

inline bool IsCompatible(const char* value) {
  return value && strcmp(value, kProtocolValue) == 0;
}

inline bool IsMissing(const char* value) {
  return !value || value[0] == '\0';
}

inline bool HasPeerProof(const char* memberProtocol, const char* richPresenceProtocol) {
  if (!IsMissing(memberProtocol)) return IsCompatible(memberProtocol);
  return IsCompatible(richPresenceProtocol);
}

inline int VisibleProductionLobbyType(int requestedType) {
  return requestedType == kInvisibleLobbyType ? kPublicLobbyType : requestedType;
}

inline bool ShouldLogAdmissionAttempt(long attempt) {
  return attempt > 0 && (attempt <= kAdmissionLogBurst || attempt % kAdmissionLogInterval == 0);
}

inline bool IsRequiredProductionImport(const char* dll, const char* name) {
  if (!dll || !name || strcmp(dll, "steam_api.dll") != 0) return false;
  return strcmp(name, "SteamMatchmaking") == 0 || strcmp(name, "SteamFriends") == 0 ||
         strcmp(name, "SteamAPI_RunCallbacks") == 0;
}

}  // namespace coop_matchmaking

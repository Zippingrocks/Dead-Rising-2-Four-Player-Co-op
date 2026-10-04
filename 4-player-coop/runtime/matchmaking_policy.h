#pragma once

#include <string.h>

namespace coop_matchmaking {

// Increment this value whenever network-visible behavior becomes incompatible.
constexpr char kProtocolKey[] = "dr2_4p_protocol";
constexpr char kProtocolValue[] = "3";
constexpr int kMemberLimit = 4;
constexpr int kPublicLobbyType = 2;
constexpr int kInvisibleLobbyType = 3;
constexpr long kAdmissionLogBurst = 8;
constexpr long kAdmissionLogInterval = 500;
constexpr int kFriendsSetRichPresenceSlot = 37;
constexpr int kFriendsGetFriendRichPresenceSlot = 39;
constexpr int kFriendsRequestFriendRichPresenceSlot = 42;
constexpr int kAppsBIsDlcInstalledSlot = 7;
constexpr int kAppsBGetDlcDataByIndexSlot = 11;

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

inline bool IsDr2OptionalSkillPack(unsigned int appId) {
  return appId >= 353050 && appId <= 353053;
}

inline int VisibleProductionLobbyType(int requestedType) {
  return requestedType == kInvisibleLobbyType ? kPublicLobbyType : requestedType;
}

inline bool ShouldLogAdmissionAttempt(long attempt) {
  return attempt > 0 && (attempt <= kAdmissionLogBurst || attempt % kAdmissionLogInterval == 0);
}

inline long CollectionTargetForOccupiedRemoteLinks(long occupiedLinks) {
  if (occupiedLinks < 0) occupiedLinks = 0;
  const long target = occupiedLinks + 2;  // host + existing remotes + incoming remote
  return target > kMemberLimit ? kMemberLimit : target;
}

inline long CollectionTargetForAcceptedClients(long acceptedClients) {
  if (acceptedClients < 0) acceptedClients = 0;
  long target = acceptedClients + 1;  // include the client currently being accepted
  if (target < 2) target = 2;
  return target > kMemberLimit ? kMemberLimit : target;
}

inline bool ShouldAllowConnectedJoin(bool nativeResult, bool connected, bool full, bool duplicate,
                                     long occupiedLinks, long linkLimit, long acceptedClients,
                                     unsigned char localServerBusy) {
  return !nativeResult && connected && !full && !duplicate && linkLimit > 0 && occupiedLinks < linkLimit &&
      acceptedClients >= 2 && localServerBusy == 0;
}

inline bool ShouldSignalFlowForMemberCount(long completedMembers, long currentMembers,
                                           bool nativeSessionReady) {
  return nativeSessionReady && currentMembers >= 3 && currentMembers <= kMemberLimit &&
      currentMembers > completedMembers;
}

inline bool IsRequiredProductionImport(const char* dll, const char* name) {
  if (!dll || !name || strcmp(dll, "steam_api.dll") != 0) return false;
  return strcmp(name, "SteamMatchmaking") == 0 || strcmp(name, "SteamFriends") == 0 ||
         strcmp(name, "SteamApps") == 0 ||
         strcmp(name, "SteamAPI_RunCallbacks") == 0;
}

}  // namespace coop_matchmaking

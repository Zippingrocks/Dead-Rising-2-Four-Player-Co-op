#pragma once

#include <string.h>

namespace coop_matchmaking {

// Increment this value whenever network-visible behavior becomes incompatible.
constexpr char kProtocolKey[] = "dr2_4p_protocol";
constexpr char kProtocolValue[] = "1";
constexpr int kMemberLimit = 4;

inline bool IsCompatible(const char* value) {
  return value && strcmp(value, kProtocolValue) == 0;
}

inline bool IsRequiredProductionImport(const char* dll, const char* name) {
  if (!dll || !name || strcmp(dll, "steam_api.dll") != 0) return false;
  return strcmp(name, "SteamMatchmaking") == 0 || strcmp(name, "SteamAPI_RunCallbacks") == 0;
}

}  // namespace coop_matchmaking

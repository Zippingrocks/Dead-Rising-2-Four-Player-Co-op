// 4-player co-op support for Dead Rising 2 PC, compiled into the Case Zero runtime's dinput8.dll proxy
// (tools/case-zero-runtime/build.ps1 adds this file when it exists).
//
// Stage 1 (this file): tracing and multi-instance groundwork.
//   * [Coop] Trace=1 in case_zero_campaign.ini (or -cooptrace on the command line) logs every call DR2 makes on
//     its Steam interfaces (SteamMatchmaking, SteamNetworking, SteamUser, SteamFriends, SteamUtils, SteamApps)
//     through transparent vtable proxies, plus Steam callback registrations and Winsock traffic.
//   * -coopinstance=N (N = 1..3) marks an extra instance started by the one-PC test harness: the game's
//     single-instance mutex "DeadRising2" gets a per-instance name and the log goes to coop_net.<N>.log.
// A normal Steam launch activates the production path only when four_player_coop.ini is installed beside the game.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <objbase.h>
#include <unknwn.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "steam_callback_abi.h"
#include "harness_sync_names.h"
#include "clothing_heap_owner.h"
#include "clothing_capacity_thunks.h"
#include "nfs_request_owner.h"
#include "jip_broadcast_queue.h"
#include "pause_acknowledgement.h"
#include "harness_cursor.h"
#include "matchmaking_policy.h"

namespace coop_clothing {
Owners g_owners;
const HeapApi g_api{
    reinterpret_cast<decltype(HeapApi::allocate)>(0x00A2BE10),
    reinterpret_cast<decltype(HeapApi::create)>(0x00A2ACC0),
    reinterpret_cast<decltype(HeapApi::configure)>(0x00A1B2D0),
    reinterpret_cast<decltype(HeapApi::destroy)>(0x00A1A460),
    reinterpret_cast<decltype(HeapApi::release)>(0x00A20FC0)
};
int __cdecl ParentHeap(void* manager, int player) {
    if (player >= 0 && player < 2)
        return *reinterpret_cast<int*>(static_cast<BYTE*>(manager) + 0x3DD0 + player * 4);
    auto* entry = g_owners.Find(manager);
    return entry && player >= 2 && player < 4 ? entry->ids[player - 2] : -1;
}
void __cdecl DestroyParents(void* manager) { g_owners.Release(manager, g_api); }
}

namespace coop {
namespace {

bool g_trace = false;
bool g_harness = false;
bool g_production = false;
bool g_baseGameDlcCompatibility = false;
void* volatile g_productionMatchmaking = nullptr;
void* volatile g_productionFriends = nullptr;
__declspec(align(8)) volatile LONG64 g_productionLobby = 0;
__declspec(align(8)) volatile LONG64 g_productionPendingPeer = 0;
volatile LONG g_productionFlowSignalMembers = 2;
bool g_silent = false;
bool g_isolatedDesktop = false;
bool g_directP2PProbe = false;
bool g_stockTransport = false;
bool g_sequentialJoins = false;
bool g_actorActivationProbe = false;
bool g_clientTransitionProbe = false;
bool g_hostStateTransferProbe = false;
bool g_nativeFlowProbe = false;
bool g_nfsOwnershipProbe = false;
bool g_jipBroadcastQueueProbe = false;
bool g_pauseAcknowledgementProbe = false;
bool g_loaderWaitProbe = false;
bool g_privateInputProbe = false;
bool g_lobbyFrontendProbe = false;
bool g_meshListenerProbe = false;
bool g_clothingCapacityProbe = false;
bool g_clothingVariantsProbe = false;
volatile LONG g_clothingVariantState = 0;
void* volatile g_clothingVariantManager = nullptr;
void* volatile g_clothingVariantActors[4]{};
volatile LONG64 g_clothingVariantCandidate = 0;
volatile LONG64 g_clothingVariantApplied = 0;
DWORD g_clothingVariantCandidateSince = 0;
volatile LONG g_clothingVariantApplyCount = 0;
DWORD g_clothingVariantFirstAppliedAt = 0;
volatile LONG g_campaignActivationState = 0;
BYTE* volatile g_campaignActivationP2P = nullptr;
volatile LONG g_connectionMeshInitState = 0;
BYTE* volatile g_connectionMeshInitP2P = nullptr;
volatile LONG g_connectionListenerRearmState = 0;
BYTE* volatile g_connectionListenerRearmObject = nullptr;
BYTE g_connectionListenerEvent[0x50] = {};
volatile LONG g_clientTransitionState = 0;
BYTE* volatile g_clientTransitionP2P = nullptr;
volatile LONG g_hostStateTransferState = 0;
BYTE* volatile g_hostStateTransferP2P = nullptr;
volatile LONG g_hostFlow7FinalizeState = 0;
BYTE* volatile g_hostFlow7FinalizeP2P = nullptr;
volatile LONG g_clientFlowSignalState = 0;
volatile LONG g_clientDataTransferRequestState = 0;
BYTE* volatile g_clientDataTransferRequestP2P = nullptr;
bool g_bridgeFourthPeer = false;
volatile LONG g_syntheticTransportDetached = 0;
volatile LONG g_syntheticOnlineCleanupGuardInstalled = 0;
int g_instance = 0;
int g_requestedPlayers = 0;
int g_testInstances = 1;
wchar_t g_root[MAX_PATH] = {};
FILE* g_log = nullptr;
CRITICAL_SECTION g_lock;

typedef void (__thiscall *AllocClientDataFn)(void* server, unsigned int count);
AllocClientDataFn g_allocClientData = nullptr;
void* volatile g_clientDataServer = nullptr;
volatile LONG g_clientDataCount = 0;
volatile LONG g_clientDataProbeInstalled = 0;
volatile LONG g_campaignAdmissionCapacityInstalled = 0;
volatile LONG g_productionEngineCapacityInstalled = 0;
volatile LONG g_productionLocalServerTraceInstalled = 0;
volatile LONG g_productionAcceptedClientCount = 0;
void* volatile g_productionLocalServer = nullptr;
bool InstallCampaignAdmissionCapacity(BYTE* base);
typedef void* (__thiscall *StarTopologyHostFn)(void* topology, void* ctorInfo);
StarTopologyHostFn g_starTopologyHost = nullptr;
typedef bool (__thiscall *P2PClientConnectFn)(void* client, const unsigned long long* address,
                                              unsigned short port, bool privateSlot);
P2PClientConnectFn g_p2pClientConnect = nullptr;
typedef bool (__thiscall *LinkManagerGetOrCreateFn)(void* manager, void* initInfo, void** link);
LinkManagerGetOrCreateFn g_linkManagerGetOrCreate = nullptr;
typedef void* (__thiscall *PeerOwnerCreateFn)(void* owner, void* initInfo);
PeerOwnerCreateFn g_peerOwnerCreate = nullptr;
typedef void (__thiscall *LobbyDataParseFn)(void* manager, void* lobbyId, void* output);
LobbyDataParseFn g_lobbyDataParse = nullptr;
volatile LONG g_lobbyDataParseCalls = 0;
void* volatile g_directProbeLink = nullptr;
unsigned long long LocalSteamId(int instance);
int LocalInstanceFromSteamId(unsigned long long steamId);
typedef bool (__thiscall *ConnectionInitLinkFn)(void* connection, void* initInfo);
ConnectionInitLinkFn g_connectionInitLink = nullptr;
typedef bool (__thiscall *ConnectionListenFn)(void* connection, void* listenInfo);
ConnectionListenFn g_connectionListen = nullptr;
void* volatile g_directProbeManager = nullptr;
bool g_fourEndpointReliableLayer = false;
volatile LONG g_reliableLayerLayoutInstalled = 0;
volatile LONG g_fiveEndpointManagerInstalled = 0;
typedef void* (__thiscall *ReliableLayerCtorFn)(void* reliable, void* ctorInfo);
ReliableLayerCtorFn g_reliableLayerCtor = nullptr;
typedef bool (__thiscall *ReliableLayerAcceptFn)(void* reliable, void* acceptInfo);
ReliableLayerAcceptFn g_reliableLayerAccept = nullptr;
typedef void (__thiscall *ConnListenerConnectSuccessFn)(void* listener, void* event);
ConnListenerConnectSuccessFn g_connListenerConnectSuccess = nullptr;
typedef bool (__thiscall *ConnListenerListenFn)(void* listener, bool listenAgain);
ConnListenerListenFn g_connListenerListen = nullptr;
typedef bool (__thiscall *ConnListenerPopFn)(void* listener, void* link);
ConnListenerPopFn g_connListenerPop = nullptr;
typedef void* (__thiscall *ReliableLayerAcquireEndpointFn)(void* reliable);
typedef bool (__thiscall *EndPointInitFn)(void* endpoint, void* initInfo);
EndPointInitFn g_endPointInit = nullptr;
BYTE g_listenerEndpointInitTemplate[0x40] = {};
volatile LONG g_listenerEndpointInitTemplateReady = 0;
BYTE g_reliableAcceptTemplate[0x14] = {};
volatile LONG g_reliableAcceptTemplateReady = 0;
typedef void (__thiscall *ConnectionServiceFn)(void* connection, void* first, void* second, DWORD third);
ConnectionServiceFn g_connectionService = nullptr;
typedef void (__thiscall *ConnectionShutdownFn)(void* connection);
ConnectionShutdownFn g_connectionShutdown = nullptr;
void* g_connectionTraceVtable[27] = {};
volatile LONG g_connectionServiceCalls = 0;
typedef void (__thiscall *ConnectionUpdateFn)(void* connection, float deltaSeconds);
typedef bool (__thiscall *LinkInitFn)(void* link, void* initInfo);
LinkInitFn g_linkInit = nullptr;
typedef bool (__thiscall *LinkSendFn)(void* link, void* packet);
LinkSendFn g_linkSend = nullptr;
typedef void (__thiscall *LinkUpdateFn)(void* link, float deltaSeconds);
LinkUpdateFn g_linkUpdate = nullptr;
volatile LONG g_linkUpdateCalls = 0;
typedef void (__thiscall *OnlineUpdateFn)(void* online, float deltaSeconds);
OnlineUpdateFn g_onlineUpdate = nullptr;
DWORD g_probeUpdateThread = 0;
volatile LONG g_onlineUpdateOwnership = 0;
volatile LONG g_nativeOnlineUpdateCalls = 0;
volatile LONG g_nativeOnlineUpdateThread = 0;
typedef bool (__thiscall *LocalServerAcceptFn)(void*, void*, void*);
LocalServerAcceptFn g_localServerAccept = nullptr;
typedef bool (__thiscall *LocalServerAdmissionCheckFn)(void*, DWORD);
LocalServerAdmissionCheckFn g_localServerCapacityCheck = nullptr;
LocalServerAdmissionCheckFn g_localServerSessionCheck = nullptr;
using CanListenFn = bool(__thiscall*)(void*, const unsigned long long*, const unsigned long long*, const unsigned short*);
CanListenFn g_canListen = nullptr;
bool InstallJoinPolicyTrace(BYTE* base);
volatile LONG g_joinPolicyInstalled = 0;
volatile LONG g_lobbyFrontendObserverStarted = 0;
bool StartLobbyFrontendObserver();
void Log(const char* format, ...);
bool WriteSlot(void* address, const void* bytes, size_t size);

LONG SetProductionCollectionTarget(LONG target, const char* reason) {
  if (!g_production || g_requestedPlayers != 4 || target < 2 || target > 4) return -1;
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* onlinePlayers = reinterpret_cast<volatile LONG*>(base + (0x00DDCAB0 - 0x00400000));
  __try {
    const LONG previous = InterlockedExchange(onlinePlayers, target);
    if (previous != target)
      Log("production engine: collection target %ld->%ld reason=%s", previous, target, reason);
    return previous;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("production engine: failed to set collection target=%ld reason=%s fault=%08lX",
        target, reason, GetExceptionCode());
    return -1;
  }
}

void __cdecl TraceLoaderWaitTick(void** returnSlot, BYTE* scheduler) {
  static volatile LONG calls = 0;
  static volatile LONG lastLogTick = 0;
  const LONG call = InterlockedIncrement(&calls);
  const DWORD now = GetTickCount();
  const LONG previous = InterlockedCompareExchange(&lastLogTick, 0, 0);
  if (call > 2 && static_cast<DWORD>(now - previous) < 1000) return;
  InterlockedExchange(&lastLogTick, static_cast<LONG>(now));

  BYTE* event = nullptr;
  BYTE complete = 0xFF;
  DWORD event0 = 0, event4 = 0, event8 = 0, eventC = 0;
  DWORD ownerThread = 0;
  LONG schedulerState = -1, pending = -1;
  __try {
    BYTE* callerStack = reinterpret_cast<BYTE*>(returnSlot) + sizeof(void*);
    event = *reinterpret_cast<BYTE**>(callerStack + 0x1C);
    if (event) {
      event0 = *reinterpret_cast<DWORD*>(event);
      event4 = *reinterpret_cast<DWORD*>(event + 4);
      event8 = *reinterpret_cast<DWORD*>(event + 8);
      eventC = *reinterpret_cast<DWORD*>(event + 0xC);
      complete = event[0x10];
    }
    if (scheduler) {
      ownerThread = *reinterpret_cast<DWORD*>(scheduler + 8);
      schedulerState = *reinterpret_cast<LONG*>(scheduler + 0xC);
      pending = *reinterpret_cast<LONG*>(scheduler + 0x4C);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    event = nullptr;
    complete = 0xFF;
  }
  Log("loader wait: tick=%ld site=%p scheduler=%p ownerThread=%lu schedulerState=%ld pending=%ld "
      "event=%p complete=%u eventWords=%08lX,%08lX,%08lX,%08lX thread=%lu",
      call, returnSlot ? *returnSlot : nullptr, scheduler, ownerThread, schedulerState, pending,
      event, complete, event0, event4, event8, eventC, GetCurrentThreadId());
}

__declspec(naked) void LoaderWaitTickThunk() {
  __asm {
    mov eax, esp
    push esi
    push eax
    call TraceLoaderWaitTick
    add esp, 8
    mov eax, 0x009A68E0
    call eax
    ret
  }
}

bool InstallLoaderWaitTrace(BYTE* base) {
  if (!g_loaderWaitProbe) return true;
  if (base != reinterpret_cast<BYTE*>(0x00400000)) return false;
  const DWORD site = 0x009E7D58;
  BYTE expected[5]{0xE8};
  const LONG originalOffset = static_cast<LONG>(0x009A68E0 - (site + 5));
  memcpy(expected + 1, &originalOffset, 4);
  if (memcmp(reinterpret_cast<void*>(site), expected, sizeof(expected))) {
    Log("loader wait: signature mismatch at %08lX; trace cancelled", site);
    return false;
  }
  BYTE replacement[5]{0xE8};
  const LONG hookOffset = static_cast<LONG>(reinterpret_cast<DWORD>(&LoaderWaitTickThunk) - (site + 5));
  memcpy(replacement + 1, &hookOffset, 4);
  if (!WriteSlot(reinterpret_cast<void*>(site), replacement, sizeof(replacement))) {
    Log("loader wait: patch write failed; trace cancelled");
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), sizeof(replacement));
  Log("loader wait: read-only completion/queue trace installed at %08lX", site);
  return true;
}

void __fastcall Hook_OnlineUpdate(void* online, void*, float deltaSeconds) {
  const DWORD thread = GetCurrentThreadId();
  const LONG ownership = InterlockedCompareExchange(&g_onlineUpdateOwnership, 0, 0);
  if (thread == g_probeUpdateThread) {
    if (ownership == 0) g_onlineUpdate(online, deltaSeconds);
    return;
  }
  InterlockedIncrement(&g_nativeOnlineUpdateCalls);
  if (ownership == 1) {
    InterlockedCompareExchange(&g_nativeOnlineUpdateThread, static_cast<LONG>(thread), 0);
    g_onlineUpdate(online, deltaSeconds);
  }
}

bool HandoffOnlineUpdateToNativeThread() {
  if (!g_directP2PProbe) return true;
  if (InterlockedCompareExchange(&g_nativeOnlineUpdateCalls, 0, 0) == 0) return false;
  if (InterlockedCompareExchange(&g_onlineUpdateOwnership, 1, 0) == 0) {
    Log("online update: handed ownership from synthetic thread=%lu to native caller; observedNativeCalls=%ld",
        g_probeUpdateThread, InterlockedCompareExchange(&g_nativeOnlineUpdateCalls, 0, 0));
  }
  return InterlockedCompareExchange(&g_onlineUpdateOwnership, 0, 0) == 1;
}

bool __fastcall Hook_LocalServerAccept(void* server, void*, void* client, void* packet) {
  BYTE* object = static_cast<BYTE*>(server);
  BYTE* message = static_cast<BYTE*>(packet);
  LONG acceptedBefore = 0;
  LONG previousTarget = -1;
  if (g_production) {
    InterlockedExchangePointer(&g_productionLocalServer, server);
    acceptedBefore = InterlockedCompareExchange(&g_productionAcceptedClientCount, 0, 0);
    if (g_requestedPlayers == 4 && acceptedBefore >= 2) {
      auto* clientSlots = reinterpret_cast<volatile LONG*>(object + 0x48);
      const LONG oldSlots = InterlockedCompareExchange(clientSlots, 4, 2);
      if (oldSlots == 2)
        Log("production admission: repaired live local-server client slots 2->4 server=%p", server);
    }
    const LONG target = coop_matchmaking::CollectionTargetForAcceptedClients(acceptedBefore);
    previousTarget = SetProductionCollectionTarget(target, "host accepted client");
  }
  Log("direct host: local Accept enter server=%p client=%p state=%ld busy=%u clientSlots=%ld packet=%p "
      "private=%ld nat=%ld protocol=%ld",
      server, client, *reinterpret_cast<LONG*>(object + 0x0C), object[0x324],
      *reinterpret_cast<LONG*>(object + 0x48), packet,
      *reinterpret_cast<LONG*>(message + 0x80), *reinterpret_cast<LONG*>(message + 0x84),
      *reinterpret_cast<LONG*>(message + 0x88));
  const bool result = g_localServerAccept(server, client, packet);
  if (g_production) {
    if (result) {
      const LONG accepted = InterlockedIncrement(&g_productionAcceptedClientCount);
      if (accepted > 4) InterlockedExchange(&g_productionAcceptedClientCount, 4);
      Log("production admission: host accepted client ordinal=%ld collectionTarget=%ld busy=%u",
          accepted > 4 ? 4 : accepted, coop_matchmaking::CollectionTargetForAcceptedClients(acceptedBefore),
          object[0x324]);
    } else if (previousTarget >= 2 && previousTarget <= 4) {
      SetProductionCollectionTarget(previousTarget, "host accept failed");
    }
  }
  Log("direct host: local Accept result=%d server=%p client=%p", result, server, client);
  return result;
}

bool __fastcall Hook_LocalServerCapacityCheck(void* server, void*, DWORD incomingCount) {
  const bool result = g_localServerCapacityCheck(server, incomingCount);
  BYTE* object = static_cast<BYTE*>(server);
  Log("production admission: capacity check result=%d incoming=%lu groups=%ld/%ld/%ld/%ld slots=%p:%u,%p:%u,%p:%u,%p:%u",
      result, incomingCount, *reinterpret_cast<LONG*>(object + 0x328),
      *reinterpret_cast<LONG*>(object + 0x338), *reinterpret_cast<LONG*>(object + 0x348),
      *reinterpret_cast<LONG*>(object + 0x358), *reinterpret_cast<void**>(object + 0x58), object[0x51],
      *reinterpret_cast<void**>(object + 0x70), object[0x69],
      *reinterpret_cast<void**>(object + 0x88), object[0x81],
      *reinterpret_cast<void**>(object + 0xA0), object[0x99]);
  return result;
}

bool __fastcall Hook_LocalServerSessionCheck(void* server, void*, DWORD privateSession) {
  const bool result = g_localServerSessionCheck(server, privateSession);
  BYTE* object = static_cast<BYTE*>(server);
  Log("production admission: session check result=%d private=%lu slots=%p:%u,%p:%u,%p:%u,%p:%u",
      result, privateSession, *reinterpret_cast<void**>(object + 0x58), object[0x51],
      *reinterpret_cast<void**>(object + 0x70), object[0x69],
      *reinterpret_cast<void**>(object + 0x88), object[0x81],
      *reinterpret_cast<void**>(object + 0xA0), object[0x99]);
  return result;
}

bool __fastcall Hook_CanListen(void* query, void*, const unsigned long long* nonce,
                              const unsigned long long* peer, const unsigned short* port) {
  if (g_production) {
    void* matchmaking = InterlockedCompareExchangePointer(&g_productionMatchmaking, nullptr, nullptr);
    const unsigned long long lobby = static_cast<unsigned long long>(
        InterlockedCompareExchange64(&g_productionLobby, 0, 0));
    const char* protocol = nullptr;
    const char* lobbyProtocol = nullptr;
    const char* richPresenceProtocol = nullptr;
    bool listedMember = false;
    if (matchmaking && lobby && peer) {
      using GetMemberData_t = const char*(__thiscall*)(void*, unsigned long long, unsigned long long, const char*);
      void** methods = *reinterpret_cast<void***>(matchmaking);
      protocol = reinterpret_cast<GetMemberData_t>(methods[24])(
          matchmaking, lobby, *peer, coop_matchmaking::kProtocolKey);
      using GetLobbyData_t = const char*(__thiscall*)(void*, unsigned long long, const char*);
      lobbyProtocol = reinterpret_cast<GetLobbyData_t>(methods[19])(
          matchmaking, lobby, coop_matchmaking::kProtocolKey);
      using GetMemberCount_t = int(__thiscall*)(void*, unsigned long long);
      using GetMemberByIndex_t = void*(__thiscall*)(void*, unsigned long long*, unsigned long long, int);
      const int memberCount = reinterpret_cast<GetMemberCount_t>(methods[17])(matchmaking, lobby);
      for (int i = 0; i < memberCount; i++) {
        unsigned long long member = 0;
        reinterpret_cast<GetMemberByIndex_t>(methods[18])(matchmaking, &member, lobby, i);
        if (member == *peer) {
          listedMember = true;
          break;
        }
      }
    }
    void* friends = InterlockedCompareExchangePointer(&g_productionFriends, nullptr, nullptr);
    if (friends && peer && coop_matchmaking::IsMissing(protocol)) {
      void** methods = *reinterpret_cast<void***>(friends);
      using GetPresence_t = const char*(__thiscall*)(void*, unsigned long long, const char*);
      richPresenceProtocol = reinterpret_cast<GetPresence_t>(
          methods[coop_matchmaking::kFriendsGetFriendRichPresenceSlot])(
          friends, *peer, coop_matchmaking::kProtocolKey);
      if (coop_matchmaking::IsMissing(richPresenceProtocol)) {
        static volatile LONG presenceRequests = 0;
        const LONG request = InterlockedIncrement(&presenceRequests);
        if (coop_matchmaking::ShouldLogAdmissionAttempt(request)) {
          using RequestPresence_t = void(__thiscall*)(void*, unsigned long long);
          reinterpret_cast<RequestPresence_t>(
              methods[coop_matchmaking::kFriendsRequestFriendRichPresenceSlot])(friends, *peer);
        }
      }
    }
    if (!coop_matchmaking::IsCompatible(lobbyProtocol) ||
        !coop_matchmaking::HasPeerProof(protocol, richPresenceProtocol)) {
        static volatile LONG rejectedAttempts = 0;
        const LONG attempt = InterlockedIncrement(&rejectedAttempts);
        if (coop_matchmaking::ShouldLogAdmissionAttempt(attempt)) {
          Log("production admission: rejected peer=%08lX%08lX memberProtocol='%s' presenceProtocol='%s' "
              "lobbyProtocol='%s' listedMember=%d attempt=%ld",
              peer ? static_cast<DWORD>(*peer >> 32) : 0, peer ? static_cast<DWORD>(*peer) : 0,
              protocol ? protocol : "<null>", richPresenceProtocol ? richPresenceProtocol : "<null>",
              lobbyProtocol ? lobbyProtocol : "<null>", listedMember, attempt);
        }
        return false;
    }
    if (!coop_matchmaking::IsCompatible(protocol)) {
      static volatile LONG pendingTagAdmissions = 0;
      const LONG attempt = InterlockedIncrement(&pendingTagAdmissions);
      if (coop_matchmaking::ShouldLogAdmissionAttempt(attempt)) {
        Log("production admission: accepting presence-verified peer=%08lX%08lX while lobby membership propagates "
            "listedMember=%d attempt=%ld",
            static_cast<DWORD>(*peer >> 32), static_cast<DWORD>(*peer), listedMember, attempt);
      }
    }
  }
  BYTE* queryObject = static_cast<BYTE*>(query);
  LONG* linkLimit = queryObject ? reinterpret_cast<LONG*>(queryObject + 0xE4) : nullptr;
  if (g_production && g_requestedPlayers == 4 && linkLimit) {
    const LONG oldLimit = InterlockedCompareExchange(linkLimit, 3, 1);
    if (oldLimit == 1) {
      Log("production admission: repaired live remote-link limit 1->3 query=%p", query);
    }
  }
  LONG occupiedLinks = 0;
  if (g_production && queryObject) {
    const size_t offsets[] = {0x94, 0xAC, 0xC4, 0xDC};
    for (size_t offset : offsets)
      if (*reinterpret_cast<void**>(queryObject + offset)) occupiedLinks++;
  }
  const bool nativeResult = g_canListen(query, nonce, peer, port);
  bool result = nativeResult;
  bool full = false;
  bool duplicate = false;
  bool connected = false;
  BYTE localServerBusy = 0xFF;
  if (g_production && queryObject && nonce && peer && port) {
    void** table = *reinterpret_cast<void***>(queryObject);
    using IsFullFn = bool(__thiscall*)(void*);
    full = reinterpret_cast<IsFullFn>(table[6])(queryObject);
    duplicate = reinterpret_cast<CanListenFn>(table[2])(queryObject, nonce, peer, port);
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    connected = reinterpret_cast<bool(__cdecl*)()>(base + (0x0085B140 - 0x00400000))();
    BYTE* localServer = static_cast<BYTE*>(
        InterlockedCompareExchangePointer(&g_productionLocalServer, nullptr, nullptr));
    if (localServer) localServerBusy = localServer[0x324];
    const LONG acceptedClients = InterlockedCompareExchange(&g_productionAcceptedClientCount, 0, 0);
    if (linkLimit && coop_matchmaking::ShouldAllowConnectedJoin(
            nativeResult, connected, full, duplicate, occupiedLinks, *linkLimit,
            acceptedClients, localServerBusy)) {
      // Retail forbids new listeners after the first campaign peer reaches gameplay. A four-player
      // session needs serialized join-in-progress, so bypass only that final predicate after the
      // protocol, capacity, and duplicate checks above have all succeeded.
      result = true;
      Log("production admission: allowed verified join-in-progress peer=%08lX%08lX occupied=%ld limit=%ld "
          "acceptedClients=%ld busy=%u",
          static_cast<DWORD>(*peer >> 32), static_cast<DWORD>(*peer), occupiedLinks, *linkLimit,
          acceptedClients, localServerBusy);
    }
  }
  if (g_production && result && peer)
    InterlockedExchange64(&g_productionPendingPeer, static_cast<LONG64>(*peer));
  if (g_production && queryObject && nonce && peer && port) {
    static volatile LONG productionNativeAttempts = 0;
    const LONG attempt = InterlockedIncrement(&productionNativeAttempts);
    if (coop_matchmaking::ShouldLogAdmissionAttempt(attempt)) {
      Log("production admission: result peer=%08lX%08lX native=%d effective=%d full=%d duplicate=%d "
          "connected=%d acceptedClients=%ld busy=%u limit=%ld linkSlots=%p,%p,%p,%p attempt=%ld",
          static_cast<DWORD>(*peer >> 32), static_cast<DWORD>(*peer), nativeResult, result, full, duplicate,
          connected, InterlockedCompareExchange(&g_productionAcceptedClientCount, 0, 0), localServerBusy,
          linkLimit ? *linkLimit : -1, *reinterpret_cast<void**>(queryObject + 0x94),
          *reinterpret_cast<void**>(queryObject + 0xAC), *reinterpret_cast<void**>(queryObject + 0xC4),
          *reinterpret_cast<void**>(queryObject + 0xDC), attempt);
    }
  }
  static volatile LONG diagnosticCalls = 0;
  if (g_meshListenerProbe && InterlockedIncrement(&diagnosticCalls) <= 16) {
    Log("mesh admission: query=%p nonce=%08lX%08lX remoteAddress=%08lX%08lX port=%u accepted=%d",
        query, static_cast<DWORD>(*nonce >> 32), static_cast<DWORD>(*nonce),
        static_cast<DWORD>(*peer >> 32), static_cast<DWORD>(*peer), *port, result);
  }
  static int previous[4] = {-1, -1, -1, -1};
  const int instance = LocalInstanceFromSteamId(*peer);
  if (instance >= 0) {
    BYTE* object = static_cast<BYTE*>(query);
    void** table = *reinterpret_cast<void***>(query);
    using IsFullFn = bool(__thiscall*)(void*);
    const bool diagnosticFull = reinterpret_cast<IsFullFn>(table[6])(query);
    const bool diagnosticDuplicate = reinterpret_cast<CanListenFn>(table[2])(query, nonce, peer, port);
    const bool diagnosticConnected = reinterpret_cast<bool(__cdecl*)()>(0x0085B140)();
    const int state = static_cast<int>(result) | (diagnosticFull << 1) |
        (diagnosticDuplicate << 2) | (diagnosticConnected << 3);
    if (previous[instance] != state) {
      previous[instance] = state;
      Log("join policy: peer=%08lX%08lX query=%p result=%d full=%d duplicate=%d reallyConnected=%d "
          "limit=%ld linkSlots=%p,%p,%p,%p",
          static_cast<DWORD>(*peer >> 32), static_cast<DWORD>(*peer), query, result, diagnosticFull,
          diagnosticDuplicate, diagnosticConnected,
          *reinterpret_cast<LONG*>(object + 0xE4), *reinterpret_cast<void**>(object + 0x94),
          *reinterpret_cast<void**>(object + 0xAC), *reinterpret_cast<void**>(object + 0xC4),
          *reinterpret_cast<void**>(object + 0xDC));
    }
  }
  return result;
}

LONG CaptureNativeHandoffException(EXCEPTION_POINTERS* exception) {
  if (!exception || !exception->ExceptionRecord || !exception->ContextRecord) {
    Log("direct p2p: native handoff exception details unavailable");
    return EXCEPTION_EXECUTE_HANDLER;
  }
  const EXCEPTION_RECORD* record = exception->ExceptionRecord;
  CONTEXT* context = exception->ContextRecord;
  Log("direct p2p: native handoff exception code=%08lX address=%p "
      "eip=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
      record->ExceptionCode, record->ExceptionAddress, context->Eip, context->Eax, context->Ebx,
      context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Esp);
  return EXCEPTION_EXECUTE_HANDLER;
}

LONG CaptureCampaignActivationException(EXCEPTION_POINTERS* exception) {
  if (!exception || !exception->ExceptionRecord || !exception->ContextRecord) {
    Log("campaign activation: exception details unavailable");
    return EXCEPTION_EXECUTE_HANDLER;
  }
  const EXCEPTION_RECORD* record = exception->ExceptionRecord;
  CONTEXT* context = exception->ContextRecord;
  Log("campaign activation: exception code=%08lX address=%p "
      "eip=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
      record->ExceptionCode, record->ExceptionAddress, context->Eip, context->Eax, context->Ebx,
      context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Esp);
  return EXCEPTION_EXECUTE_HANDLER;
}

typedef void (__thiscall *RemoteServerDieFn)(void* server);
typedef bool (__thiscall *RemoteServerBoolFn)(void* server);
typedef bool (__thiscall *RemoteServerLinkShutdownFn)(void* server, const void* node);
typedef bool (__thiscall *RemoteServerShutdownFn)(void* server, const void* node);
RemoteServerDieFn g_remoteServerDie = nullptr;
RemoteServerBoolFn g_remoteServerLinkConnected = nullptr;
RemoteServerLinkShutdownFn g_remoteServerLinkShutdown = nullptr;
RemoteServerShutdownFn g_remoteServerShutdown = nullptr;
// Vanilla DR2's cRemoteServer cEventListener vtable has 19 entries. Preserve all of them so the
// trace remains transparent when packet send/broadcast methods beyond Shutdown are dispatched.
void* g_remoteServerTraceVtable[19] = {};

void Log(const char* format, ...) {
  if (!g_log) return;
  EnterCriticalSection(&g_lock);
  SYSTEMTIME now;
  GetLocalTime(&now);
  fprintf(g_log, "%02u:%02u:%02u.%03u [%lu] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentThreadId());
  va_list args;
  va_start(args, format);
  vfprintf(g_log, format, args);
  va_end(args);
  fputc('\n', g_log);
  fflush(g_log);
  LeaveCriticalSection(&g_lock);
}

bool WriteSlot(void* address, const void* bytes, size_t size) {
  DWORD oldProtect;
  if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
  memcpy(address, bytes, size);
  VirtualProtect(address, size, oldProtect, &oldProtect);
  return true;
}

void __fastcall Hook_ClothingReserve(void* manager, void*, int requested) {
  using ReserveFn = void (__thiscall*)(void*, int);
  auto* object = static_cast<BYTE*>(manager);
  if (requested == 2 && g_clothingCapacityProbe && g_requestedPlayers == 4) {
    const int reserved = *reinterpret_cast<int*>(object + 0x3DC4);
    const int pending = *reinterpret_cast<int*>(object + 0x3DEC);
    const int completed = *reinterpret_cast<int*>(object + 0x3CD8);
    if (reserved != 4 && (pending || completed)) {
      Log("clothing capacity: pending loads prevent reservation change manager=%p pending=%d completed=%d", manager, pending, completed);
      // Abort only this disposable harness child; rebuilding under pending callbacks is unsafe.
      TerminateProcess(GetCurrentProcess(), 0xE043C004);
      return;
    }
    const bool parentsExist = coop_clothing::g_owners.Find(manager) != nullptr;
    if (reserved != 4 || !parentsExist) {
      int freeHeaps = 0;
      for (int id = 1; id < 128; ++id)
        if (!reinterpret_cast<DWORD*>(0x00E11900)[id] && !reinterpret_cast<DWORD*>(0x00E11640)[id]) ++freeHeaps;
      if (reserved < 0 || reserved > 4 || (reserved == 4 && !parentsExist)) {
        Log("clothing capacity: invalid reservation ownership count=%d parents=%d; aborting harness child", reserved, parentsExist);
        TerminateProcess(GetCurrentProcess(), 0xE043C004);
        return;
      }
      bool reclaimed[128]{};
      int reclaimable = 0;
      for (int record = 0; record < reserved * 13; ++record) {
        // Native teardown frees the original heap at record+0x30, not its active alias at +0x28.
        const int id = *reinterpret_cast<int*>(object + 0x40 + record * 0x10C);
        if (id > 0 && id < 128 && !reclaimed[id] &&
            (reinterpret_cast<DWORD*>(0x00E11900)[id] || reinterpret_cast<DWORD*>(0x00E11640)[id])) {
          reclaimed[id] = true;
          ++reclaimable;
        }
      }
      const int requiredHeaps = coop_clothing::RequiredFreeHeapSlots(reclaimable, parentsExist);
      if (freeHeaps < requiredHeaps) {
        Log("clothing capacity: insufficient native heap slots free=%d required=%d reclaimable=%d; aborting harness child",
            freeHeaps, requiredHeaps, reclaimable);
        TerminateProcess(GetCurrentProcess(), 0xE043C004);
        return;
      }
    }
    auto* heaps = coop_clothing::g_owners.Acquire(manager, coop_clothing::g_api);
    if (!heaps) {
      Log("clothing capacity: parent heap allocation failed; aborting harness child");
      TerminateProcess(GetCurrentProcess(), 0xE043C004);
      return;
    }
    Log("clothing capacity: native reserve manager=%p %d->4 parents=%d,%d thread=%lu", manager,
        *reinterpret_cast<int*>(object + 0x3DC4), heaps->ids[0], heaps->ids[1], GetCurrentThreadId());
    requested = 4;
  }
  reinterpret_cast<ReserveFn>(0x004AFCD0)(manager, requested);
  if (requested == 4) {
    for (int player = 0; player < 4; ++player) {
      int ids[13]{};
      bool valid = true;
      for (int slot = 0; slot < 13; ++slot) {
        ids[slot] = *reinterpret_cast<int*>(object + 0x38 + (player * 13 + slot) * 0x10C);
        valid = valid && ids[slot] > 0 && ids[slot] < 128;
      }
      Log("clothing capacity: player=%d parent=%d valid=%d heaps=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
          player, coop_clothing::ParentHeap(manager, player), valid,
          ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7], ids[8], ids[9], ids[10], ids[11], ids[12]);
      if (!valid) {
        Log("clothing capacity: native allocation incomplete; aborting harness child");
        TerminateProcess(GetCurrentProcess(), 0xE043C004);
        return;
      }
    }
  }
}

bool InstallClothingCapacityProbe(BYTE* base) {
  if (!g_clothingCapacityProbe || g_requestedPlayers != 4) return true;
  if (base != reinterpret_cast<BYTE*>(0x00400000)) return false;
  struct Patch { DWORD address; size_t size; BYTE original[7]; void* target; };
  Patch patches[] = {
      {0x004698B4, 7, {0x8B, 0xBC, 0x9D, 0xD0, 0x3D, 0, 0}, &coop_clothing::ParentHeapThunk},
      {0x004D07BA, 6, {0x8B, 0x86, 0xD0, 0x3D, 0, 0}, &coop_clothing::DestroyParentsThunk},
      {0x004D08BD, 5, {}, &Hook_ClothingReserve},
      {0x0051D1AD, 5, {}, &Hook_ClothingReserve},
  };
  for (int index = 2; index < 4; ++index) {
    patches[index].original[0] = 0xE8;
    const LONG offset = static_cast<LONG>(0x004AFCD0 - (patches[index].address + 5));
    memcpy(patches[index].original + 1, &offset, 4);
  }
  for (const auto& patch : patches) {
    if (memcmp(reinterpret_cast<void*>(patch.address), patch.original, patch.size) != 0) {
      Log("clothing capacity: signature mismatch at %08lX; probe cancelled", patch.address);
      return false;
    }
  }
  for (int index = 0; index < 4; ++index) {
    const auto& patch = patches[index];
    BYTE replacement[7]{0xE8, 0, 0, 0, 0, 0x90, 0x90};
    const LONG offset = static_cast<LONG>(reinterpret_cast<DWORD>(patch.target) - (patch.address + 5));
    memcpy(replacement + 1, &offset, 4);
    if (!WriteSlot(reinterpret_cast<void*>(patch.address), replacement, patch.size)) {
      for (int undo = index - 1; undo >= 0; --undo)
        WriteSlot(reinterpret_cast<void*>(patches[undo].address), patches[undo].original, patches[undo].size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      Log("clothing capacity: patch write failed; probe cancelled");
      return false;
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  Log("clothing capacity: four-player reserve, parent selection and native-destructor cleanup installed");
  return true;
}

LONG64 ClothingVariantSignature(void* manager, void* const* actors) {
  unsigned long long value = reinterpret_cast<ULONG_PTR>(manager);
  for (int player = 0; player < 4; ++player)
    value ^= static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(actors[player])) << (player * 11);
  return static_cast<LONG64>(value);
}

bool ReadClothingVariantTargets(void*& manager, void** actors) {
  manager = nullptr;
  memset(actors, 0, sizeof(void*) * 4);
  if (!g_clothingVariantsProbe || g_requestedPlayers != 4) return false;
  __try {
    void* game = *reinterpret_cast<void**>(0x00DCB0FC);
    if (!game) return false;
    auto* clothing = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(game) + 0x7EB8);
    if (!clothing || *reinterpret_cast<int*>(clothing + 0x08) != 2 ||
        *reinterpret_cast<int*>(clothing + 0x3DC4) != 4 ||
        *reinterpret_cast<int*>(clothing + 0x3DEC) != 0 ||
        *reinterpret_cast<int*>(clothing + 0x3CD8) != 0) return false;
    for (int player = 0; player < 4; ++player) {
      actors[player] = *reinterpret_cast<void**>(clothing + 0x3CDC + player * 4);
      if (!actors[player]) return false;
    }
    manager = clothing;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    manager = nullptr;
    memset(actors, 0, sizeof(void*) * 4);
    return false;
  }
}

bool ClothingVariantsPendingInternal() {
  // Every client fixes its local view of actor slot 4. P1-P3 are never wardrobe targets.
  if (!g_clothingVariantsProbe ||
      InterlockedCompareExchange(&g_clothingVariantState, 0, 0) == 3) return false;
  if (InterlockedCompareExchange(&g_clothingVariantState, 0, 0) == 1) return true;
  void* manager = nullptr;
  void* actors[4]{};
  if (!ReadClothingVariantTargets(manager, actors)) return false;
  const LONG64 signature = ClothingVariantSignature(manager, actors);
  const DWORD now = GetTickCount();
  if (signature == InterlockedCompareExchange64(&g_clothingVariantApplied, 0, 0)) {
    const LONG applyCount = InterlockedCompareExchange(&g_clothingVariantApplyCount, 0, 0);
    // The final native admission synchronizes P4's stock chest after the first local request.
    // Reapply actor slot 4 once the observed sync window has passed; P1-P3 remain untouched.
    if (applyCount != 1 ||
        static_cast<DWORD>(now - g_clothingVariantFirstAppliedAt) < 60000) return false;
    Log("clothing variants: scheduling post-admission P4 jacket reapply");
    InterlockedExchange64(&g_clothingVariantApplied, 0);
    InterlockedExchange64(&g_clothingVariantCandidate, 0);
    g_clothingVariantCandidateSince = now;
    return false;
  }
  if (signature != InterlockedCompareExchange64(&g_clothingVariantCandidate, 0, 0)) {
    InterlockedExchange64(&g_clothingVariantCandidate, signature);
    g_clothingVariantCandidateSince = now;
    return false;
  }
  if (now - g_clothingVariantCandidateSince < 1000) return false;
  g_clothingVariantManager = manager;
  for (int player = 0; player < 4; ++player) g_clothingVariantActors[player] = actors[player];
  MemoryBarrier();
  return InterlockedCompareExchange(&g_clothingVariantState, 1, 0) == 0;
}

void ApplyClothingVariantsOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_clothingVariantState, 2, 1) != 1) return;
  void* manager = nullptr;
  void* actors[4]{};
  if (!ReadClothingVariantTargets(manager, actors) || manager != g_clothingVariantManager) {
    InterlockedExchange(&g_clothingVariantState, 0);
    return;
  }
  for (int player = 0; player < 4; ++player) {
    if (actors[player] != g_clothingVariantActors[player]) {
      InterlockedExchange(&g_clothingVariantState, 0);
      return;
    }
  }
  const BYTE lookupExpected[] = {0x8B, 0x44, 0x24, 0x04, 0x3D, 0xBE, 0x00, 0x00,
                                 0x00, 0x77, 0x10, 0x69, 0xC0, 0x1C, 0x01, 0x00};
  const BYTE localSetExpected[] = {0x33, 0xC0, 0x83, 0x79, 0x08, 0x02, 0x56, 0x75,
                                   0x1B, 0x8B, 0x74, 0x24, 0x08, 0x8D, 0x91, 0xDC,
                                   0x3C, 0x00, 0x00};
  auto* lookupFunction = reinterpret_cast<BYTE*>(0x0041FEF0);
  auto* localSetFunction = reinterpret_cast<BYTE*>(0x0051C9D0);
  if (memcmp(lookupFunction, lookupExpected, sizeof(lookupExpected)) != 0 ||
      memcmp(localSetFunction, localSetExpected, sizeof(localSetExpected)) != 0) {
    Log("clothing variants: actor-local clothing signatures mismatch; feature disabled");
    InterlockedExchange(&g_clothingVariantState, 3);
    return;
  }
  using LookupOutfitFn = BYTE* (__thiscall*)(void*, unsigned);
  using SetClothingInfoFn = void (__thiscall*)(void*, void*, int, const char*, bool);
  auto lookupOutfit = reinterpret_cast<LookupOutfitFn>(lookupFunction);
  auto setClothingInfo = reinterpret_cast<SetClothingInfoFn>(localSetFunction);
  const LONG64 signature = ClothingVariantSignature(manager, actors);
  struct LocalOutfitTarget {
    int actor;
    unsigned outfit;
    int part;
  };
  // P3 gets the native TIR helmet, one-piece torso/legs, gloves and boots. P4 keeps only its yellow chest.
  const LocalOutfitTarget targets[] = {
      {2, 5u, 0}, {2, 5u, 3}, {2, 5u, 4}, {2, 5u, 5}, {2, 5u, 6},
      {3, 151u, 3},
  };
  Log("clothing variants: applying actor-local P3 TIR suit and P4 yellow jacket manager=%p thread=%lu",
      manager, GetCurrentThreadId());
  __try {
    void* outfitDatabase = *reinterpret_cast<void**>(static_cast<BYTE*>(manager) + 0x3CEC);
    for (const auto& target : targets) {
      BYTE* outfitEntry = outfitDatabase ? lookupOutfit(outfitDatabase, target.outfit) : nullptr;
      BYTE* partName = outfitEntry ? outfitEntry + target.part * 36 + 0x1C : nullptr;
      if (!partName) {
        Log("clothing variants: P%d outfit=%u part=%d lookup failed; feature disabled",
            target.actor + 1, target.outfit, target.part);
        InterlockedExchange(&g_clothingVariantState, 3);
        return;
      }
      if (*(partName + 0x20) >= 0x1F) partName = *reinterpret_cast<BYTE**>(partName);
      if (!partName || !*partName) {
        Log("clothing variants: P%d outfit=%u part=%d asset is empty; feature disabled",
            target.actor + 1, target.outfit, target.part);
        InterlockedExchange(&g_clothingVariantState, 3);
        return;
      }
      // SetClothingInfo resolves the selected actor to its own slot and never creates a broadcast outfit event.
      setClothingInfo(manager, actors[target.actor], target.part,
                      reinterpret_cast<const char*>(partName), false);
      Log("clothing variants: actor-local P%d outfit=%u part=%d asset=%s",
          target.actor + 1, target.outfit, target.part, reinterpret_cast<const char*>(partName));
    }
    if (InterlockedCompareExchange(&g_clothingVariantApplyCount, 0, 0) == 0)
      g_clothingVariantFirstAppliedAt = GetTickCount();
    const LONG applyCount = InterlockedIncrement(&g_clothingVariantApplyCount);
    InterlockedExchange64(&g_clothingVariantApplied, signature);
    Log("clothing variants: actor-local wardrobe requests queued successfully pass=%ld", applyCount);
    InterlockedExchange(&g_clothingVariantState, 0);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("clothing variants: native jacket request fault=%08lX; feature disabled", GetExceptionCode());
    InterlockedExchange(&g_clothingVariantState, 3);
  }
}

SRWLOCK g_jipQueueLock = SRWLOCK_INIT;
coop_jip::BroadcastQueue<256> g_jipQueue;
BYTE* g_jipQueueGame = nullptr;
BYTE* g_jipQueueScene = nullptr;
BYTE* g_jipQueueClient = nullptr;
bool g_jipQueueDraining = false;
volatile LONG g_jipApplyDepth = 0;

BYTE* NativeP2PClient() {
  BYTE* online = *reinterpret_cast<BYTE**>(0x00E5F428);
  BYTE* p2p = online ? *reinterpret_cast<BYTE**>(online + 0xD8) : nullptr;
  return p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
}

__declspec(noreturn) void FailJipQueue(const char* reason) {
  Log("jip broadcast queue: FAILED reason=%s; aborting owned harness child without discarding active events", reason);
  TerminateProcess(GetCurrentProcess(), 0xE043C005);
  ExitProcess(0xE043C005);
}

void ResetJipQueue() {
  AcquireSRWLockExclusive(&g_jipQueueLock);
  const unsigned count = g_jipQueue.Count();
  g_jipQueue.Clear();
  g_jipQueueGame = g_jipQueueScene = g_jipQueueClient = nullptr;
  g_jipQueueDraining = false;
  ReleaseSRWLockExclusive(&g_jipQueueLock);
  if (count) Log("jip broadcast queue: native disconnect invalidated %u pending events", count);
}

void __fastcall ReceiveJipBroadcast(void* object, void*, uint32_t* event, const void* data,
                                   int type, int size, int direction) {
  using Fn = void (__thiscall*)(void*, uint32_t*, const void*, int, int, int);
  if (!g_jipBroadcastQueueProbe || g_instance == 0 || direction != 1) {
    reinterpret_cast<Fn>(0x007B7890)(object, event, data, type, size, direction);
    return;
  }
  BYTE* client = NativeP2PClient();
  BYTE* game = static_cast<BYTE*>(object);
  BYTE* scene = *reinterpret_cast<BYTE**>(game + 0x2C);
  const LONG state = client ? *reinterpret_cast<LONG*>(client + 0x3F4) : 0;
  AcquireSRWLockExclusive(&g_jipQueueLock);
  const bool pending = g_jipQueue.Count() || g_jipQueueDraining;
  const bool defer = pending || state == 1 || state == 2 || InterlockedCompareExchange(&g_jipApplyDepth, 0, 0);
  const bool sameOwner = !pending || (g_jipQueueGame == game && g_jipQueueScene == scene && g_jipQueueClient == client);
  bool stored = false;
  if (defer && sameOwner && client && scene) {
    stored = g_jipQueue.Push(event, data, type, size);
    if (stored) { g_jipQueueGame = game; g_jipQueueScene = scene; g_jipQueueClient = client; }
  }
  const unsigned count = g_jipQueue.Count();
  ReleaseSRWLockExclusive(&g_jipQueueLock);
  if (defer) {
    if (!stored) FailJipQueue(sameOwner ? "invalid event or queue capacity" : "game/scene/client changed");
    if (count <= 16) Log("jip broadcast queue: retained type=%d size=%d sequence=%lu state=%ld count=%u thread=%lu",
                        type, size, event[0], state, count, GetCurrentThreadId());
    return;
  }
  reinterpret_cast<Fn>(0x007B7890)(object, event, data, type, size, direction);
}

void DrainJipQueue(BYTE* client) {
  if (!g_jipBroadcastQueueProbe || g_instance == 0) return;
  BYTE* node = *reinterpret_cast<BYTE**>(client + 0x90);
  BYTE* mesh = *reinterpret_cast<BYTE**>(0x00DDEA04);
  if (*reinterpret_cast<LONG*>(client + 0x3F4) != 0 || *reinterpret_cast<LONG*>(client + 0x88) != 2 ||
      !node || *reinterpret_cast<LONG*>(node + 0xC) != 3 || !mesh || *reinterpret_cast<LONG*>(mesh + 0x3C) != 3) return;
  AcquireSRWLockExclusive(&g_jipQueueLock);
  if (g_jipQueueDraining || !g_jipQueue.Count()) { ReleaseSRWLockExclusive(&g_jipQueueLock); return; }
  g_jipQueueDraining = true;
  ReleaseSRWLockExclusive(&g_jipQueueLock);
  unsigned delivered = 0;
  for (;;) {
    coop_jip::Broadcast event;
    AcquireSRWLockExclusive(&g_jipQueueLock);
    BYTE* game = g_jipQueueGame;
    const bool sameOwner = g_jipQueueClient == client && game == *reinterpret_cast<BYTE**>(0x00DDC3F0) &&
        game && g_jipQueueScene == *reinterpret_cast<BYTE**>(game + 0x2C);
    if (!g_jipQueue.Count()) {
      g_jipQueueDraining = false;
      ReleaseSRWLockExclusive(&g_jipQueueLock);
      break;
    }
    if (!sameOwner) { ReleaseSRWLockExclusive(&g_jipQueueLock); FailJipQueue("owner changed before replay"); }
    g_jipQueue.Pop(event);
    ReleaseSRWLockExclusive(&g_jipQueueLock);
    reinterpret_cast<void (__thiscall*)(void*, uint32_t*, const void*, int, int, int)>(0x007B7890)(
        game, event.header, event.payload, event.type, event.size, 1);
    ++delivered;
  }
  Log("jip broadcast queue: replayed=%u after native world application thread=%lu", delivered, GetCurrentThreadId());
}

void __fastcall TraceNativeWorldApply(void* object, void*) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  auto* client = static_cast<BYTE*>(object);
  if (call <= 16)
    Log("native transition: world apply enter call=%ld client=%p jipState=%ld bytes=%ld data=%p thread=%lu", call, client,
        *reinterpret_cast<LONG*>(client + 0x3F4), *reinterpret_cast<LONG*>(client + 0x3F8),
        *reinterpret_cast<void**>(client + 0x3FC), GetCurrentThreadId());
  if (g_jipBroadcastQueueProbe) InterlockedIncrement(&g_jipApplyDepth);
  __try {
    reinterpret_cast<void (__thiscall*)(void*)>(0x0088A180)(client);
  } __finally {
    if (g_jipBroadcastQueueProbe) InterlockedDecrement(&g_jipApplyDepth);
  }
  DrainJipQueue(client);
  if (call <= 16)
    Log("native transition: world apply return call=%ld jipState=%ld flag94=%u started=%u transfer=%u", call,
        *reinterpret_cast<LONG*>(client + 0x3F4), client[0x94], client[0x251], client[0x97]);
}

void __fastcall TraceNativeStateTransfer(void* object, void*) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  auto* server = static_cast<BYTE*>(object);
  if (call <= 16)
    Log("native transition: automatic state transfer enter call=%ld server=%p state=%ld active=%ld users=%ld thread=%lu",
        call, server, *reinterpret_cast<LONG*>(server + 0x138), *reinterpret_cast<LONG*>(server + 0x13C),
        *reinterpret_cast<LONG*>(server + 0x140), GetCurrentThreadId());
  reinterpret_cast<void (__thiscall*)(void*)>(0x008744B0)(server);
  if (call <= 16)
    Log("native transition: automatic state transfer return call=%ld state=%ld active=%ld users=%ld", call,
        *reinterpret_cast<LONG*>(server + 0x138), *reinterpret_cast<LONG*>(server + 0x13C),
        *reinterpret_cast<LONG*>(server + 0x140));
}

void __fastcall TraceNativeProcessFlow(void* object, void*, void* packet) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  auto* client = static_cast<BYTE*>(object);
  if (call <= 64)
    Log("native transition: ProcessFlow enter call=%ld client=%p packet=%p jipState=%ld flag94=%u started=%u transfer=%u thread=%lu",
        call, client, packet, *reinterpret_cast<LONG*>(client + 0x3F4), client[0x94], client[0x251], client[0x97], GetCurrentThreadId());
  reinterpret_cast<void (__thiscall*)(void*, void*)>(0x0088A210)(client, packet);
  if (call <= 64)
    Log("native transition: ProcessFlow return call=%ld jipState=%ld flag94=%u started=%u transfer=%u", call,
        *reinterpret_cast<LONG*>(client + 0x3F4), client[0x94], client[0x251], client[0x97]);
}

void __fastcall TraceNativeFlowRegistration(void* object, void*, void* packet, void* callback, void* user) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  auto* client = static_cast<BYTE*>(object);
  const LONG command = *reinterpret_cast<LONG*>(static_cast<BYTE*>(packet) + 0x40);
  BYTE* cached = command >= 0 && command < 10 ? client + 0x258 + command * 12 : nullptr;
  if (call <= 96)
    Log("native transition: flow registration call=%ld client=%p command=%ld callback=%p user=%p pending=%d cachedCallback=%p cachedUser=%p caller=%p",
        call, client, command, callback, user, cached ? cached[0] : -1,
        cached ? *reinterpret_cast<void**>(cached + 4) : nullptr,
        cached ? *reinterpret_cast<void**>(cached + 8) : nullptr, _ReturnAddress());
  reinterpret_cast<void (__thiscall*)(void*, void*, void*, void*)>(0x0087CF50)(client, packet, callback, user);
}

bool CurrentNfsOwner(void* object, coop_nfs::Owner& owner) {
  auto* nfs = static_cast<BYTE*>(object);
  if (!nfs || *reinterpret_cast<DWORD*>(nfs) != 0x00CB8468 ||
      *reinterpret_cast<DWORD*>(nfs + 0x24) != 1 || *reinterpret_cast<DWORD*>(nfs + 0x28) != 1) return false;
  BYTE* transfer = *reinterpret_cast<BYTE**>(nfs + 0x40);
  if (!transfer) return false;
  return coop_nfs::Resolve(*reinterpret_cast<coop_nfs::Request**>(nfs + 0x38),
      *reinterpret_cast<DWORD*>(nfs + 0x20), *reinterpret_cast<int*>(transfer),
      reinterpret_cast<uint32_t>(transfer), owner);
}

thread_local int g_nfsRecipient = -1;

bool __fastcall RouteNfsOutbound(void* object, void*, void* segment, char** data, int* size, int link, bool* done) {
  g_nfsRecipient = -1;
  coop_nfs::Owner owner;
  const bool supported = CurrentNfsOwner(object, owner);
  using Fn = bool (__thiscall*)(void*, void*, char**, int*, int, bool*);
  const bool result = reinterpret_cast<Fn>(0x00872C90)(object, segment, data, size, link, done);
  if (supported && result && segment && size && coop_nfs::HasRoutableSegment(
      owner, *static_cast<const DWORD*>(segment), data && *data, *size)) g_nfsRecipient = owner.link;
  return result;
}

bool __fastcall RouteNfsAddresses(void* manager, void*, const int* links, unsigned count,
                                 void* addresses, unsigned short* ports, unsigned capacity) {
  const int recipient = g_nfsRecipient;
  g_nfsRecipient = -1;
  using Fn = bool (__thiscall*)(void*, const int*, unsigned, void*, unsigned short*, unsigned);
  const bool route = recipient >= 0 && recipient < 4 && count == 1 && capacity == 1;
  static LONG logs = 0;
  const bool result = reinterpret_cast<Fn>(0x008639F0)(manager, route ? &recipient : links, count, addresses, ports, capacity);
  if (route && InterlockedIncrement(&logs) <= 64) {
    const auto* address = static_cast<const DWORD*>(addresses);
    Log("nfs ownership: chunk address link=%d originalLink=%d result=%d peer=%08lX%08lX port=%u manager=%p",
        recipient, links[0], result, address[1], address[0], ports[0], manager);
  }
  return result;
}

bool __fastcall TraceNfsInbound(void* object, void*, const DWORD* segment, const char* data, int size) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  const DWORD descriptor = segment ? segment[0] : 0;
  const bool result = reinterpret_cast<bool (__thiscall*)(void*, const DWORD*, const char*, int)>(0x00881E80)(
      object, segment, data, size);
  if (call <= 64)
    Log("nfs ownership: inbound call=%ld nfs=%p descriptor=%08lX handle=%lu bytes=%d accepted=%d",
        call, object, descriptor, descriptor & 0xFF, size, result);
  return result;
}

void __fastcall CompleteNfsOwnedRequest(void* object, void*, int wireHandle) {
  coop_nfs::Owner owner;
  const bool supported = CurrentNfsOwner(object, owner) && owner.state == 6 && owner.wireHandle == wireHandle;
  const int index = supported ? owner.index : wireHandle;
  static LONG logs = 0;
  if (InterlockedIncrement(&logs) <= 32)
    Log("nfs ownership: completion wireHandle=%d localIndex=%d recipient=%d validated=%d", wireHandle,
        index, supported ? owner.link : -1, supported);
  reinterpret_cast<void (__thiscall*)(void*, int)>(0x00881E10)(object, index);
}

bool InstallNfsOwnershipProbe(BYTE* base) {
  if (!g_nfsOwnershipProbe) return true;
  if (base != reinterpret_cast<BYTE*>(0x00400000)) return false;
  const BYTE getPrefix[] = {0x83, 0x7C, 0x24, 0x10, 0x00};
  const BYTE cleanupPrefix[] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0xFF};
  if (memcmp(reinterpret_cast<void*>(0x00872C90), getPrefix, sizeof(getPrefix)) ||
      memcmp(reinterpret_cast<void*>(0x00881E10), cleanupPrefix, sizeof(cleanupPrefix))) {
    Log("nfs ownership: signature mismatch; probe cancelled");
    return false;
  }
  struct Patch { DWORD site; DWORD originalTarget; void* target; BYTE original[5]; };
  Patch patches[] = {{0x0087EE3B, 0x00872C90, &RouteNfsOutbound, {}},
                     {0x0087EEB9, 0x008639F0, &RouteNfsAddresses, {}},
                     {0x00882FEF, 0x00881E10, &CompleteNfsOwnedRequest, {}},
                     {0x0088327D, 0x00881E80, &TraceNfsInbound, {}}};
  for (auto& patch : patches) {
    patch.original[0] = 0xE8;
    const LONG offset = static_cast<LONG>(patch.originalTarget - (patch.site + 5));
    memcpy(patch.original + 1, &offset, 4);
    if (memcmp(reinterpret_cast<void*>(patch.site), patch.original, 5)) {
      Log("nfs ownership: signature mismatch at %08lX; probe cancelled", patch.site);
      return false;
    }
  }
  for (int index = 0; index < static_cast<int>(sizeof(patches) / sizeof(patches[0])); ++index) {
    BYTE replacement[5]{0xE8};
    const LONG offset = static_cast<LONG>(reinterpret_cast<DWORD>(patches[index].target) - (patches[index].site + 5));
    memcpy(replacement + 1, &offset, 4);
    if (!WriteSlot(reinterpret_cast<void*>(patches[index].site), replacement, 5)) {
      for (int undo = index - 1; undo >= 0; --undo)
        WriteSlot(reinterpret_cast<void*>(patches[undo].site), patches[undo].original, 5);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      Log("nfs ownership: write failure; probe cancelled");
      return false;
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  Log("nfs ownership: single-outbound request routing and completion probe installed");
  return true;
}

void LogNativeTeardown(const char* operation, void* object, void* caller) {
  static LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  if (call > 32) return;
  void* frames[12]{};
  const USHORT count = CaptureStackBackTrace(1, 12, frames, nullptr);
  char hints[160]{};
  size_t used = 0;
  for (USHORT index = 0; index < count; ++index) {
    const int written = sprintf_s(hints + used, sizeof(hints) - used, "%s%p", index ? "," : "", frames[index]);
    if (written <= 0) break;
    used += static_cast<size_t>(written);
  }
  Log("native teardown: call=%ld operation=%s object=%p caller=%p thread=%lu stackHints=%s",
      call, operation, object, caller, GetCurrentThreadId(), hints);
}

__declspec(noinline) void __fastcall TraceNativeClientShutdown(void* object, void*) {
  LogNativeTeardown("client-shutdown", object, _ReturnAddress());
  if (g_jipBroadcastQueueProbe) ResetJipQueue();
  reinterpret_cast<void (__thiscall*)(void*)>(0x0087C550)(object);
}

__declspec(noinline) void __fastcall TraceNativeP2PShutdown(void* object, void*) {
  LogNativeTeardown("p2p-shutdown", object, _ReturnAddress());
  reinterpret_cast<void (__thiscall*)(void*)>(0x00876B00)(object);
}

__declspec(noinline) void __fastcall TraceNativeServerDown(void* object, void*) {
  if (g_nativeFlowProbe) LogNativeTeardown("server-down-request", object, _ReturnAddress());
  reinterpret_cast<void (__thiscall*)(void*)>(0x00873BE0)(object);
}

__declspec(noinline) void __fastcall TraceNativeServerQuit(void* object, void*) {
  LogNativeTeardown("server-handle-quit", object, _ReturnAddress());
  reinterpret_cast<void (__thiscall*)(void*)>(0x00861A40)(object);
}

void LogTopologyEvent(const BYTE* event) {
  static LONG calls = 0;
  if (InterlockedIncrement(&calls) > 32) return;
  Log("native teardown event: event=%p vtable=%08lX recipient=%08lX kind=%lu source=%p detail=%08lX",
      event, *reinterpret_cast<const DWORD*>(event), *reinterpret_cast<const DWORD*>(event + 0x18),
      *reinterpret_cast<const DWORD*>(event + 0x1C), *reinterpret_cast<void* const*>(event + 0x20),
      *reinterpret_cast<const DWORD*>(event + 0x24));
}

struct NativeDispatchContext { void* client; DWORD category; LONG topologyKind; };
__declspec(thread) const NativeDispatchContext* g_nativeDispatchContext = nullptr;
__declspec(thread) LONG g_inventoryWireItem = -2;

void LogInventoryHandoff(const char* operation, const BYTE* payload, const BYTE* scene, LONG itemId) {
  static LONG calls = 0;
  if (InterlockedIncrement(&calls) > 96) return;
  const BYTE* actor = *reinterpret_cast<BYTE* const*>(payload + 0x10);
  const BYTE* prop = *reinterpret_cast<BYTE* const*>(payload + 0x14);
  const BYTE* online = *reinterpret_cast<BYTE**>(0x00E5F428);
  const BYTE* p2p = online ? *reinterpret_cast<BYTE* const*>(online + 0xD8) : nullptr;
  const BYTE* client = p2p ? *reinterpret_cast<BYTE* const*>(p2p + 0x38) : nullptr;
  Log("inventory handoff: operation=%s actor=%p user=%ld itemId=%08lX prop=%p localUser=%ld sender=%lu jipState=%ld started=%u transfer=%u",
      operation, actor, actor ? *reinterpret_cast<const LONG*>(actor + 0x3A0) : -1, itemId, prop,
      scene ? *reinterpret_cast<const LONG*>(scene + 0x98) : -1,
      (*reinterpret_cast<const DWORD*>(payload + 0xC) >> 6) & 3,
      client ? *reinterpret_cast<const LONG*>(client + 0x3F4) : -1,
      client ? client[0x251] : 0, client ? client[0x97] : 0);
}

int __fastcall TraceInventoryItemRead(void* message, void*) {
  const int result = reinterpret_cast<int (__thiscall*)(void*)>(0x00A4FA00)(message);
  g_inventoryWireItem = result;
  return result;
}

void __fastcall TraceInventoryItemUnpack(void* object, void*, const BYTE* scene, void* message) {
  const LONG previous = g_inventoryWireItem;
  g_inventoryWireItem = -2;
  __try {
    reinterpret_cast<void (__thiscall*)(void*, const BYTE*, void*)>(0x007A8B00)(object, scene, message);
    LogInventoryHandoff("unpack", static_cast<const BYTE*>(object), scene, g_inventoryWireItem);
  } __finally {
    g_inventoryWireItem = previous;
  }
}

void __fastcall TraceInventoryItemPack(void* object, void*, const BYTE* scene, void* message) {
  const BYTE* payload = static_cast<const BYTE*>(object);
  const BYTE* prop = *reinterpret_cast<BYTE* const*>(payload + 0x14);
  const BYTE* properties = prop ? *reinterpret_cast<BYTE* const*>(prop + 0xAC) : nullptr;
  LogInventoryHandoff("pack", payload, scene, properties ? *reinterpret_cast<const LONG*>(properties + 0x40) : -1);
  reinterpret_cast<void (__thiscall*)(void*, const BYTE*, void*)>(0x007A8AB0)(object, scene, message);
}

void __fastcall TraceNativeClientEvent(void* object, void*, const BYTE* event) {
  const DWORD category = *reinterpret_cast<const DWORD*>(event + 4);
  if (category == 0x33) {
    static LONG failures = 0;
    if (InterlockedIncrement(&failures) <= 32)
      Log("native connection failure: client=%p recipient=%08lX%08lX flag=%u reason=%ld",
          object, *reinterpret_cast<const DWORD*>(event + 0x1C), *reinterpret_cast<const DWORD*>(event + 0x18),
          event[0x20], *reinterpret_cast<const LONG*>(event + 0x24));
  }
  const NativeDispatchContext context{object, category,
      category == 0x2F ? *reinterpret_cast<const LONG*>(event + 0x1C) : -1};
  const NativeDispatchContext* previous = g_nativeDispatchContext;
  g_nativeDispatchContext = &context;
  __try {
    reinterpret_cast<void (__thiscall*)(void*, const BYTE*)>(0x00887380)(object, event);
  } __finally {
    g_nativeDispatchContext = previous;
  }
}

__declspec(noinline) bool __cdecl TraceNativeDesyncAssert(bool condition, const char* expression, const char* file, int line) {
  if (!condition) {
    static LONG failures = 0;
    if (InterlockedIncrement(&failures) <= 32)
      Log("native desync assert: caller=%p expression=%.320s file=%.240s line=%d thread=%lu",
          _ReturnAddress(), expression ? expression : "(null)", file ? file : "(null)", line, GetCurrentThreadId());
  }
  return reinterpret_cast<bool (__cdecl*)(bool, const char*, const char*, int)>(0x0086EA50)(
      condition, expression, file, line);
}

void* __fastcall TraceNativeShutdownEventCtor(void* object, void*, unsigned long long recipient, bool flag, int reason) {
  static LONG calls = 0;
  if (InterlockedIncrement(&calls) <= 32)
    Log("native shutdown producer: event=%p caller=%p recipient=%08lX%08lX flag=%d reason=%d",
        object, _ReturnAddress(), static_cast<DWORD>(recipient >> 32), static_cast<DWORD>(recipient), flag, reason);
  return reinterpret_cast<void* (__thiscall*)(void*, unsigned long long, bool, int)>(0x008585E0)(object, recipient, flag, reason);
}

bool __fastcall TraceNativeWorldReady(void* object, void*) {
  const bool result = reinterpret_cast<bool (__thiscall*)(void*)>(0x008537D0)(object);
  static LONG calls = 0;
  if (InterlockedIncrement(&calls) <= 32) {
    const BYTE* client = static_cast<const BYTE*>(object);
    const BYTE* server = *reinterpret_cast<BYTE* const*>(client + 0x90);
    const BYTE* gateway = *reinterpret_cast<BYTE**>(0x00DDEA04);
    Log("native event readiness: client=%p ready=%d stage=%ld server=%p serverState=%ld gateway=%p gatewayState=%ld",
        object, result, *reinterpret_cast<const LONG*>(client + 0x88), server,
        server ? *reinterpret_cast<const LONG*>(server + 0xC) : -1, gateway,
        gateway ? *reinterpret_cast<const LONG*>(gateway + 0x3C) : -1);
  }
  return result;
}

__declspec(noinline) int __fastcall TraceNativeQuitRequest(void* object, void*, int reason) {
  static LONG calls = 0;
  if (InterlockedIncrement(&calls) <= 32) {
    const BYTE* bytes = static_cast<const BYTE*>(object);
    Log("native quit request: object=%p caller=%p reason=%d stage=%ld previousReason=%ld deferredReason=%ld thread=%lu",
        object, _ReturnAddress(), reason, *reinterpret_cast<const LONG*>(bytes + 0x118),
        *reinterpret_cast<const LONG*>(bytes + 0x110), *reinterpret_cast<const LONG*>(bytes + 0x114),
        GetCurrentThreadId());
    if (g_nativeDispatchContext)
      Log("native quit context: client=%p category=%lu topologyKind=%ld",
          g_nativeDispatchContext->client, g_nativeDispatchContext->category, g_nativeDispatchContext->topologyKind);
  }
  return reinterpret_cast<int (__thiscall*)(void*, int)>(0x00889F50)(object, reason);
}

__declspec(noinline) bool __fastcall TraceNativeClientTopology(void* object, void*, const BYTE* event) {
  if (*reinterpret_cast<const DWORD*>(event + 0x1C) == 16) {
    LogNativeTeardown("client-event-16", object, _ReturnAddress());
    LogTopologyEvent(event);
  }
  return reinterpret_cast<bool (__thiscall*)(void*, const BYTE*)>(0x008827B0)(object, event);
}

__declspec(noinline) bool __fastcall TraceNativeServerTopology(void* object, void*, const BYTE* event) {
  if (*reinterpret_cast<const DWORD*>(event + 0x1C) == 16) {
    LogNativeTeardown("server-event-16", object, _ReturnAddress());
    LogTopologyEvent(event);
  }
  return reinterpret_cast<bool (__thiscall*)(void*, const BYTE*)>(0x00874F20)(object, event);
}

__declspec(thread) coop_pause::SendContext* g_pauseSendContext = nullptr;

bool __fastcall ValidatePauseAcknowledgement(BYTE* event, void*, BYTE* scene) {
  const bool accepted = reinterpret_cast<bool (__thiscall*)(BYTE*, BYTE*)>(0x007AB7E0)(event, scene);
  if (g_pauseAcknowledgementProbe && g_pauseSendContext &&
      coop_pause::PreserveOriginalRequest(accepted, g_pauseSendContext, event, scene, event[0x34]))
    Log("pause acknowledgement: original request semantics retained after native validation owner=%ld", *reinterpret_cast<LONG*>(event + 0x1C));
  return accepted;
}

bool InstallNativeTeardownTrace(BYTE* base) {
  if (!g_nativeFlowProbe) return true;
  if (base != reinterpret_cast<BYTE*>(0x00400000)) return false;
  struct Patch { DWORD slot; void* original; void* replacement; };
  Patch patches[] = {
      {0x00CB862C, reinterpret_cast<void*>(0x0087C550), &TraceNativeClientShutdown},
      {0x00CB86AC, reinterpret_cast<void*>(0x00876B00), &TraceNativeP2PShutdown},
      {0x00CB8624, reinterpret_cast<void*>(0x008827B0), &TraceNativeClientTopology},
      {0x00CBCF04, reinterpret_cast<void*>(0x00874F20), &TraceNativeServerTopology},
      {0x00CB81F0, reinterpret_cast<void*>(0x00861A40), &TraceNativeServerQuit},
      {0x00CB9F50, reinterpret_cast<void*>(0x00861A40), &TraceNativeServerQuit},
      {0x00CBC868, reinterpret_cast<void*>(0x00861A40), &TraceNativeServerQuit},
      {0x00CBC4B0, reinterpret_cast<void*>(0x00889F50), &TraceNativeQuitRequest},
      {0x00CC0C10, reinterpret_cast<void*>(0x00889F50), &TraceNativeQuitRequest},
      {0x00CC0CF0, reinterpret_cast<void*>(0x00889F50), &TraceNativeQuitRequest},
      {0x00CB861C, reinterpret_cast<void*>(0x00887380), &TraceNativeClientEvent},
      {0x00C372D0, reinterpret_cast<void*>(0x007A8AB0), &TraceInventoryItemPack},
      {0x00C372D4, reinterpret_cast<void*>(0x007A8B00), &TraceInventoryItemUnpack},
      {0x00C3B3B4, reinterpret_cast<void*>(0x007AB7E0), &ValidatePauseAcknowledgement},
  };
  // These are original PC vtable entries, not OTR offsets or inline detours.
  for (const auto& patch : patches) {
    if (*reinterpret_cast<void**>(patch.slot) != patch.original) {
      Log("native teardown: signature mismatch at %08lX; trace cancelled", patch.slot);
      return false;
    }
  }
  for (int index = 0; index < static_cast<int>(sizeof(patches) / sizeof(patches[0])); ++index) {
    if (!WriteSlot(reinterpret_cast<void*>(patches[index].slot), &patches[index].replacement, sizeof(void*))) {
      for (int undo = index - 1; undo >= 0; --undo)
        WriteSlot(reinterpret_cast<void*>(patches[undo].slot), &patches[undo].original, sizeof(void*));
      Log("native teardown: write failure; trace cancelled");
      return false;
    }
  }
  Log("native teardown: bounded PC client/P2P shutdown tracing installed; original calls preserved");
  return true;
}

void LogPauseNegotiation(const char* phase, const char* kind, BYTE* scene, BYTE* event) {
  __try {
    BYTE* game = *reinterpret_cast<BYTE**>(0x00DDC3F0);
    BYTE* state = game ? *reinterpret_cast<BYTE**>(game + 0x38) : nullptr;
    BYTE* managers = scene ? *reinterpret_cast<BYTE**>(scene + 0x90) : nullptr;
    BYTE* online = managers ? *reinterpret_cast<BYTE**>(managers + 0x98) : nullptr;
    if (!scene || !event || !state || !online) { Log("pause negotiation: unavailable context"); return; }
    const DWORD* status = reinterpret_cast<DWORD*>(state + 0x1DC);
    Log("pause negotiation: phase=%s kind=%s local=%ld sender=%lu operation=%ld owner=%ld flag34=%u flag35=%u statuses=%08lX,%08lX,%08lX,%08lX pendingOwner=%ld pendingFlag=%u members=%u,%u,%u,%u thread=%lu",
        phase, kind, *reinterpret_cast<LONG*>(scene + 0x98), (*reinterpret_cast<DWORD*>(event + 0xC) >> 6) & 3,
        *reinterpret_cast<LONG*>(event + 0x14), *reinterpret_cast<LONG*>(event + 0x1C), event[0x34], event[0x35],
        status[0], status[1], status[2], status[3], *reinterpret_cast<LONG*>(online + 0x8850), online[0x8854],
        online[0x1A14], online[0x3424], online[0x4E34], online[0x6844], GetCurrentThreadId());
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("pause negotiation: observation fault=%08lX; original callback remains enabled", GetExceptionCode());
  }
}

struct PauseDispatchContext { BYTE* scene; BYTE* event; };
__declspec(thread) PauseDispatchContext* g_pauseDispatchContext = nullptr;

void __fastcall SendPauseAcknowledgement(BYTE* manager, void*, BYTE* outgoing, const char* file, int line) {
  if (g_pauseAcknowledgementProbe) {
    const PauseDispatchContext* context = g_pauseDispatchContext;
    bool valid = false;
    int local = -1, sender = -1, owner = -1;
    __try {
      if (context && context->scene && context->event && manager && outgoing) {
        BYTE* incoming = context->event;
        local = *reinterpret_cast<int*>(context->scene + 0x98);
        sender = (*reinterpret_cast<DWORD*>(incoming + 0xC) >> 6) & 3;
        owner = *reinterpret_cast<int*>(incoming + 0x1C);
        valid = *reinterpret_cast<BYTE**>(manager + 0xC) == context->scene &&
            *reinterpret_cast<int*>(incoming + 0x14) == 0 &&
            *reinterpret_cast<int*>(incoming + 0x18) == 5 && !incoming[0x34] &&
            *reinterpret_cast<int*>(outgoing + 0x14) == 0 &&
            *reinterpret_cast<int*>(outgoing + 0x18) == 5 &&
            *reinterpret_cast<int*>(outgoing + 0x1C) == owner;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
    const auto decision = coop_pause::Classify(local, sender, owner);
    if (!valid || decision == coop_pause::Acknowledgement::InvalidContext) {
      Log("pause acknowledgement: FAILED context local=%d sender=%d owner=%d; stopping owned harness child", local, sender, owner);
      TerminateProcess(GetCurrentProcess(), 0xE043C007);
      return;
    }
    if (decision == coop_pause::Acknowledgement::RedundantPeerReply) {
      Log("pause acknowledgement: no reply to peer acknowledgement local=%d sender=%d owner=%d; native receipt/quorum retained", local, sender, owner);
      return;
    }
    Log("pause acknowledgement: forwarding original request reply local=%d sender=%d owner=%d", local, sender, owner);
    // Other peers may acknowledge before this process receives the originator's
    // request. Its own reply must retain that request's semantics, not open a new menu.
    coop_pause::SendContext sendContext{outgoing, context->scene};
    coop_pause::SendContext* previous = g_pauseSendContext;
    g_pauseSendContext = &sendContext;
    __try {
      reinterpret_cast<void (__thiscall*)(BYTE*, BYTE*, const char*, int)>(0x00440BE0)(manager, outgoing, file, line);
    } __finally { g_pauseSendContext = previous; }
    return;
  }
  reinterpret_cast<void (__thiscall*)(BYTE*, BYTE*, const char*, int)>(0x00440BE0)(manager, outgoing, file, line);
}

void __cdecl TraceNativePauseMenu(BYTE* scene, BYTE* event) {
  static LONG count = 0;
  const bool trace = InterlockedIncrement(&count) <= 128;
  if (trace) LogPauseNegotiation("enter", "pause", scene, event);
  PauseDispatchContext context{scene, event};
  PauseDispatchContext* previous = g_pauseDispatchContext;
  g_pauseDispatchContext = &context;
  __try { reinterpret_cast<void (__cdecl*)(BYTE*, BYTE*)>(0x0048CC30)(scene, event); }
  __finally { g_pauseDispatchContext = previous; }
  if (trace) LogPauseNegotiation("return", "pause", scene, event);
}

void __cdecl TraceNativeQuasiPause(BYTE* scene, BYTE* event) {
  static LONG count = 0;
  const bool trace = InterlockedIncrement(&count) <= 128;
  if (trace) LogPauseNegotiation("enter", "quasi-pause", scene, event);
  reinterpret_cast<void (__cdecl*)(BYTE*, BYTE*)>(0x0048CFA0)(scene, event);
  if (trace) LogPauseNegotiation("return", "quasi-pause", scene, event);
}

bool InstallNativeTransitionTrace(BYTE* base) {
  if (!g_harness || !g_hostStateTransferProbe) return true;
  if (base != reinterpret_cast<BYTE*>(0x00400000)) return false;
  struct Patch { DWORD site; DWORD originalTarget; void* target; BYTE original[5]; };
  Patch patches[] = {
      {0x0088A5A6, 0x0088A180, &TraceNativeWorldApply, {}},
      {0x0088A61E, 0x0088A180, &TraceNativeWorldApply, {}},
      {0x0087578E, 0x008744B0, &TraceNativeStateTransfer, {}},
      {0x0088ACAC, 0x0088A210, &TraceNativeProcessFlow, {}},
      {0x00882088, 0x0087CF50, &TraceNativeFlowRegistration, {}},
      {0x008820E1, 0x0087CF50, &TraceNativeFlowRegistration, {}},
      {0x00882162, 0x0087CF50, &TraceNativeFlowRegistration, {}},
      {0x00882573, 0x0087CF50, &TraceNativeFlowRegistration, {}},
      {0x00875082, 0x00873BE0, &TraceNativeServerDown, {}},
      {0x008829BF, 0x00873BE0, &TraceNativeServerDown, {}},
      {0x00887B5F, 0x008537D0, &TraceNativeWorldReady, {}},
      {0x0086ECCA, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x0087AE55, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x0087AE7C, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x00886FB7, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x00888404, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x008D5CCD, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x008D907D, 0x008585E0, &TraceNativeShutdownEventCtor, {}},
      {0x0045B428, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0045B532, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0045B59F, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0045C421, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0045F1B7, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x004AA144, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x004BC965, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0051C735, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x007B8402, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x007B842A, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x00871F9D, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x00872364, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x0087290E, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x00873533, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x008735A2, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x00873632, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x00883C28, 0x0086EA50, &TraceNativeDesyncAssert, {}},
      {0x007A8B1F, 0x00A4FA00, &TraceInventoryItemRead, {}},
      {0x00865B4D, 0x007B7890, &ReceiveJipBroadcast, {}},
      {0x00509E50, 0x0048CC30, &TraceNativePauseMenu, {}},
      {0x00509E5F, 0x0048CFA0, &TraceNativeQuasiPause, {}},
      {0x0048CD95, 0x00440BE0, &SendPauseAcknowledgement, {}},
  };
  for (auto& patch : patches) {
    patch.original[0] = 0xE8;
    const LONG offset = static_cast<LONG>(patch.originalTarget - (patch.site + 5));
    memcpy(patch.original + 1, &offset, 4);
    if (memcmp(reinterpret_cast<void*>(patch.site), patch.original, 5) != 0) {
      Log("native transition: signature mismatch at %08lX; trace cancelled", patch.site);
      return false;
    }
  }
  for (int index = 0; index < static_cast<int>(sizeof(patches) / sizeof(patches[0])); ++index) {
    BYTE replacement[5]{0xE8};
    const LONG offset = static_cast<LONG>(reinterpret_cast<DWORD>(patches[index].target) - (patches[index].site + 5));
    memcpy(replacement + 1, &offset, 4);
    if (!WriteSlot(reinterpret_cast<void*>(patches[index].site), replacement, 5)) {
      for (int undo = index - 1; undo >= 0; --undo)
        WriteSlot(reinterpret_cast<void*>(patches[undo].site), patches[undo].original, 5);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      Log("native transition: patch write failed; trace cancelled");
      return false;
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  Log("native transition: bounded original-call tracing installed; no flow state overrides");
  if (g_jipBroadcastQueueProbe)
    Log("jip broadcast queue: enabled for four-player harness; 256-event FIFO, native world-completion replay");
  if (g_pauseAcknowledgementProbe)
    Log("pause acknowledgement: four-player probe enabled; reply only to originating request, native quorum unchanged");
  return InstallNativeTeardownTrace(base);
}

bool InstallSyntheticOnlineCleanupGuard(BYTE* base) {
  if (InterlockedCompareExchange(&g_syntheticOnlineCleanupGuardInstalled, 0, 0)) return true;
  BYTE* entry = base + (0x0086E890 - 0x00400000);
  constexpr BYTE kExpectedEntry = 0x56;
  constexpr BYTE kReturn = 0xC3;
  if (*entry == kReturn) {
    InterlockedExchange(&g_syntheticOnlineCleanupGuardInstalled, 1);
    return true;
  }
  if (*entry != kExpectedEntry || !WriteSlot(entry, &kReturn, sizeof(kReturn))) {
    Log("direct harness: online cleanup guard signature/write failure address=0086E890 actual=%02X", *entry);
    return false;
  }
  InterlockedExchange(&g_syntheticOnlineCleanupGuardInstalled, 1);
  Log("direct harness: suppressed frontend online cleanup entry=0086E890 for synthetic transport lifetime");
  return true;
}

void* __fastcall Hook_ReliableLayerCtor(void* reliable, void*, void* ctorInfo) {
  void* result = g_reliableLayerCtor(reliable, ctorInfo);
  if (!result) return result;
  __try {
    BYTE* object = static_cast<BYTE*>(result);
    const DWORD bandwidth0 = *reinterpret_cast<DWORD*>(object + 0x8C);
    const DWORD bandwidth1 = *reinterpret_cast<DWORD*>(object + 0x90);
    const DWORD bandwidth2 = *reinterpret_cast<DWORD*>(object + 0x94);
    const DWORD receiveIndex = *reinterpret_cast<DWORD*>(object + 0x98);
    const DWORD listenerHandle = *reinterpret_cast<DWORD*>(object + 0x9C);
    const DWORD clockLow = *reinterpret_cast<DWORD*>(object + 0xA0);
    const DWORD clockHigh = *reinterpret_cast<DWORD*>(object + 0xA4);
    *reinterpret_cast<void**>(object + 0x8C) = nullptr;
    *reinterpret_cast<DWORD*>(object + 0x90) = bandwidth0;
    *reinterpret_cast<DWORD*>(object + 0x94) = bandwidth1;
    *reinterpret_cast<DWORD*>(object + 0x98) = bandwidth2;
    *reinterpret_cast<DWORD*>(object + 0x9C) = 0xC7F12000;
    *reinterpret_cast<DWORD*>(object + 0xA0) = receiveIndex;
    *reinterpret_cast<DWORD*>(object + 0xA4) = listenerHandle;
    *reinterpret_cast<DWORD*>(object + 0xA8) = clockLow;
    *reinterpret_cast<DWORD*>(object + 0xAC) = clockHigh;
    Log("direct capacity: constructed expanded reliable layer=%p size=0xB0 endpoint3=%p", object,
        *reinterpret_cast<void**>(object + 0x8C));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct capacity: reliable-layer relocation fault=%08lX", GetExceptionCode());
  }
  return result;
}

bool __fastcall Hook_ReliableLayerAccept(void* reliable, void*, void* acceptInfo) {
  __try {
    BYTE* object = static_cast<BYTE*>(reliable);
    BYTE* info = static_cast<BYTE*>(acceptInfo);
    const unsigned long long peerId = *reinterpret_cast<unsigned long long*>(info);
    Log("direct capacity: Accept enter reliable=%p peer=%08lX%08lX endpoint=%p count=%ld "
        "slots=%p,%p,%p,%p",
        reliable, static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId),
        *reinterpret_cast<void**>(info + 0x10), *reinterpret_cast<LONG*>(object + 0x7C),
        *reinterpret_cast<void**>(object + 0x80), *reinterpret_cast<void**>(object + 0x84),
        *reinterpret_cast<void**>(object + 0x88), *reinterpret_cast<void**>(object + 0x8C));
    memcpy(g_reliableAcceptTemplate, info, sizeof(g_reliableAcceptTemplate));
    InterlockedExchange(&g_reliableAcceptTemplateReady, 1);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct capacity: Accept input snapshot fault=%08lX", GetExceptionCode());
  }
  const bool result = g_reliableLayerAccept(reliable, acceptInfo);
  __try {
    BYTE* object = static_cast<BYTE*>(reliable);
    Log("direct capacity: Accept returned=%d reliable=%p count=%ld slots=%p,%p,%p,%p",
        result, reliable, *reinterpret_cast<LONG*>(object + 0x7C),
        *reinterpret_cast<void**>(object + 0x80), *reinterpret_cast<void**>(object + 0x84),
        *reinterpret_cast<void**>(object + 0x88), *reinterpret_cast<void**>(object + 0x8C));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct capacity: Accept output snapshot fault=%08lX", GetExceptionCode());
  }
  return result;
}

void LogConnectionListenerSnapshot(const char* eventName, void* listener) {
  __try {
    BYTE* object = static_cast<BYTE*>(listener);
    BYTE* connection = *reinterpret_cast<BYTE**>(object + 0x68);
    BYTE* reliable = connection ? *reinterpret_cast<BYTE**>(connection + 0x100) : nullptr;
    BYTE* endpoint = *reinterpret_cast<BYTE**>(object + 0x70);
    BYTE* slot0 = reliable ? *reinterpret_cast<BYTE**>(reliable + 0x80) : nullptr;
    BYTE* slot1 = reliable ? *reinterpret_cast<BYTE**>(reliable + 0x84) : nullptr;
    BYTE* slot2 = reliable ? *reinterpret_cast<BYTE**>(reliable + 0x88) : nullptr;
    Log("mesh listener: %s listener=%p state=%ld started=%u connected=%p connectedState=%ld "
        "ready=%u connection=%p reliable=%p reliableState=%ld count=%ld slots=%p,%p,%p",
        eventName, listener, *reinterpret_cast<LONG*>(object + 0x64), object[0x6C], endpoint,
        endpoint ? *reinterpret_cast<LONG*>(endpoint + 0x98) : -1, endpoint ? endpoint[0x9C] : 0,
        connection, reliable, reliable ? *reinterpret_cast<LONG*>(reliable + 0x0C) : -1,
        reliable ? *reinterpret_cast<LONG*>(reliable + 0x7C) : -1, slot0, slot1, slot2);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("mesh listener: %s snapshot fault=%08lX", eventName, GetExceptionCode());
  }
}

void __fastcall Hook_ConnListenerConnectSuccess(void* listener, void*, void* event) {
  LogConnectionListenerSnapshot("connect-success enter", listener);
  g_connListenerConnectSuccess(listener, event);
  LogConnectionListenerSnapshot("connect-success leave", listener);
}

bool __fastcall Hook_ConnListenerListen(void* listener, void*, bool listenAgain) {
  void* caller = _ReturnAddress();
  Log("mesh listener: Listen caller=%p again=%d", caller, listenAgain);
  LogConnectionListenerSnapshot(listenAgain ? "Listen(again) enter" : "Listen enter", listener);
  const bool result = g_connListenerListen(listener, listenAgain);
  LogConnectionListenerSnapshot(result ? "Listen leave success" : "Listen leave failure", listener);
  return result;
}

bool __fastcall Hook_ConnListenerPop(void* listener, void*, void* link) {
  void* caller = _ReturnAddress();
  Log("mesh listener: Pop caller=%p listener=%p link=%p", caller, listener, link);
  LogConnectionListenerSnapshot("Pop enter", listener);
  const bool result = g_connListenerPop(listener, link);
  Log("mesh listener: Pop result=%d listener=%p link=%p", result, listener, link);
  LogConnectionListenerSnapshot("Pop leave", listener);
  return result;
}

bool InstallConnectionListenerRearmProbe(BYTE*) {
  if (!g_meshListenerProbe) return true;
  Log("mesh listener: native topology Listen startup probe enabled; no listener vtable hooks");
  return true;
}

bool __fastcall Hook_EndPointInit(void* endpoint, void*, void* initInfo) {
  __try {
    BYTE* object = static_cast<BYTE*>(endpoint);
    DWORD* info = static_cast<DWORD*>(initInfo);
    if (!memcmp(static_cast<BYTE*>(initInfo) + 0x14, "cConnListener", 13)) {
      memcpy(g_listenerEndpointInitTemplate, initInfo, sizeof(g_listenerEndpointInitTemplate));
      InterlockedExchange(&g_listenerEndpointInitTemplateReady, 1);
    }
    Log("direct capacity: endpoint Init enter endpoint=%p vtable=%08lX state=%ld "
        "p90=%p p2bc=%p p30c=%p p330=%p info=%p "
        "info0-7=%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX",
        endpoint, *reinterpret_cast<DWORD*>(object), *reinterpret_cast<LONG*>(object + 0x98),
        *reinterpret_cast<void**>(object + 0x90), *reinterpret_cast<void**>(object + 0x2BC),
        *reinterpret_cast<void**>(object + 0x30C), *reinterpret_cast<void**>(object + 0x330),
        initInfo, info[0], info[1], info[2], info[3], info[4], info[5], info[6], info[7]);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct capacity: endpoint Init snapshot fault endpoint=%p code=%08lX", endpoint,
        GetExceptionCode());
  }
  const bool result = g_endPointInit(endpoint, initInfo);
  Log("direct capacity: endpoint Init returned=%d endpoint=%p", result, endpoint);
  return result;
}

bool HarnessAcceptAdditionalPeer(unsigned long long peerId) {
  if (g_instance != 0 || !g_directP2PProbe || !g_bridgeFourthPeer ||
      !InterlockedCompareExchange(&g_reliableAcceptTemplateReady, 0, 0)) return false;
  __try {
    BYTE* manager = static_cast<BYTE*>(g_directProbeManager);
    BYTE* connection = manager ? *reinterpret_cast<BYTE**>(manager + 0x84) : nullptr;
    BYTE* reliable = connection ? *reinterpret_cast<BYTE**>(connection + 0x100) : nullptr;
    if (!reliable || *reinterpret_cast<LONG*>(reliable + 0x7C) != 3 ||
        *reinterpret_cast<void**>(reliable + 0x8C)) return false;
    for (int slot = 0; slot < 3; slot++) {
      BYTE* endpoint = *reinterpret_cast<BYTE**>(reliable + 0x80 + slot * 4);
      if (endpoint && *reinterpret_cast<unsigned long long*>(endpoint + 0x80) == peerId) return false;
    }
    using ReliableLayerConnectFn = bool (__thiscall*)(void*, void*);
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto connect = reinterpret_cast<ReliableLayerConnectFn>(
        base + (0x008889D0 - 0x00400000));
    BYTE address[sizeof(g_reliableAcceptTemplate)] = {};
    memcpy(address, g_reliableAcceptTemplate, sizeof(address));
    *reinterpret_cast<unsigned long long*>(address) = peerId;
    *reinterpret_cast<void**>(address + 0x10) = nullptr;
    Log("direct capacity: harness native connect peer=%08lX%08lX ports=%u,%u,%u",
        static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId),
        *reinterpret_cast<unsigned short*>(address + 0x08),
        *reinterpret_cast<unsigned short*>(address + 0x0A),
        *reinterpret_cast<unsigned short*>(address + 0x0C));
    const bool result = connect(reliable, address);
    Log("direct capacity: harness native connect returned=%d peer=%08lX%08lX count=%ld slot3=%p",
        result, static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId),
        *reinterpret_cast<LONG*>(reliable + 0x7C), *reinterpret_cast<void**>(reliable + 0x8C));
    return result;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct capacity: harness bridge fault peer=%08lX%08lX code=%08lX",
        static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId), GetExceptionCode());
    return false;
  }
}

void LogRemoteServerState(const char* event, void* server, void* caller) {
  __try {
    BYTE* object = static_cast<BYTE*>(server);
    const unsigned long long peerId = *reinterpret_cast<unsigned long long*>(object + 0x38);
    const unsigned long long matchId = *reinterpret_cast<unsigned long long*>(object + 0x40);
    Log("direct client: remote %s server=%p caller=%p state=%ld topology=%p nodeId=%ld role=%ld "
        "peer=%08lX%08lX match=%08lX%08lX remoteId=%08lX link=%p",
        event, server, caller, *reinterpret_cast<LONG*>(object + 0x0C),
        *reinterpret_cast<void**>(object + 0x20), *reinterpret_cast<LONG*>(object + 0x1C),
        *reinterpret_cast<LONG*>(object + 0x24), static_cast<DWORD>(peerId >> 32),
        static_cast<DWORD>(peerId), static_cast<DWORD>(matchId >> 32), static_cast<DWORD>(matchId),
        *reinterpret_cast<DWORD*>(object + 0xE0), *reinterpret_cast<void**>(object + 0xE4));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: remote %s server=%p caller=%p state snapshot fault=%08lX",
        event, server, caller, GetExceptionCode());
  }
}

LONG ReadLinkState(void* link) {
  __try {
    return link ? *reinterpret_cast<LONG*>(static_cast<BYTE*>(link) + 0x04) : -1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -2;
  }
}

LONG ReadLinkConnectionState(void* link) {
  __try {
    return link ? *reinterpret_cast<LONG*>(static_cast<BYTE*>(link) + 0x64) : -1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -2;
  }
}

void* ReadLinkEndpoint(void* link) {
  __try {
    return link ? *reinterpret_cast<void**>(static_cast<BYTE*>(link) + 0x68) : nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

LONG ReadLinkNodeCount(void* link) {
  __try {
    return link ? *reinterpret_cast<LONG*>(static_cast<BYTE*>(link) + 0x24) : -1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -2;
  }
}

void LogLinkState(const char* event, void* link) {
  __try {
    BYTE* object = static_cast<BYTE*>(link);
    void** vtable = *reinterpret_cast<void***>(object);
    Log("direct client: link %s link=%p state=%ld id=%ld manager=%p netConfig=%p broadcaster=%p "
        "address=%08lX%08lX signalingPort=%u nodes=%ld firstNode=%p vtable=%p",
        event, link, *reinterpret_cast<LONG*>(object + 0x04), *reinterpret_cast<LONG*>(object + 0x08),
        *reinterpret_cast<void**>(object + 0x0C), *reinterpret_cast<void**>(object + 0x10),
        *reinterpret_cast<void**>(object + 0x14), *reinterpret_cast<DWORD*>(object + 0x1C),
        *reinterpret_cast<DWORD*>(object + 0x18), *reinterpret_cast<unsigned short*>(object + 0x20),
        *reinterpret_cast<LONG*>(object + 0x24), *reinterpret_cast<void**>(object + 0x28), vtable);
    Log("direct client: link dwords "
        "00=%08lX 04=%08lX 08=%08lX 0C=%08lX 10=%08lX 14=%08lX 18=%08lX 1C=%08lX "
        "20=%08lX 24=%08lX 28=%08lX 2C=%08lX 30=%08lX 34=%08lX 38=%08lX 3C=%08lX 40=%08lX 44=%08lX 48=%08lX",
        *reinterpret_cast<DWORD*>(object + 0x00), *reinterpret_cast<DWORD*>(object + 0x04),
        *reinterpret_cast<DWORD*>(object + 0x08), *reinterpret_cast<DWORD*>(object + 0x0C),
        *reinterpret_cast<DWORD*>(object + 0x10), *reinterpret_cast<DWORD*>(object + 0x14),
        *reinterpret_cast<DWORD*>(object + 0x18), *reinterpret_cast<DWORD*>(object + 0x1C),
        *reinterpret_cast<DWORD*>(object + 0x20), *reinterpret_cast<DWORD*>(object + 0x24),
        *reinterpret_cast<DWORD*>(object + 0x28), *reinterpret_cast<DWORD*>(object + 0x2C),
        *reinterpret_cast<DWORD*>(object + 0x30), *reinterpret_cast<DWORD*>(object + 0x34),
        *reinterpret_cast<DWORD*>(object + 0x38), *reinterpret_cast<DWORD*>(object + 0x3C),
        *reinterpret_cast<DWORD*>(object + 0x40), *reinterpret_cast<DWORD*>(object + 0x44),
        *reinterpret_cast<DWORD*>(object + 0x48));
    Log("direct client: link vtable slots "
        "0=%p 1=%p 2=%p 3=%p 4=%p 5=%p 6=%p 7=%p 8=%p 9=%p 10=%p 11=%p 12=%p 13=%p 14=%p 15=%p",
        vtable[0], vtable[1], vtable[2], vtable[3], vtable[4], vtable[5], vtable[6], vtable[7],
        vtable[8], vtable[9], vtable[10], vtable[11], vtable[12], vtable[13], vtable[14], vtable[15]);
    const unsigned long long remoteId = *reinterpret_cast<unsigned long long*>(object + 0xA8);
    Log("direct client: link pc-state connectionState=%ld endpoint=%p connected=%u "
        "remoteId=%08lX%08lX sequenceB4=%u sequenceB5=%u heartbeatSeconds=%.3f",
        *reinterpret_cast<LONG*>(object + 0x64), *reinterpret_cast<void**>(object + 0x68), object[0x6C],
        static_cast<DWORD>(remoteId >> 32), static_cast<DWORD>(remoteId), object[0xB4], object[0xB5],
        *reinterpret_cast<float*>(object + 0xB8));
    BYTE* manager = *reinterpret_cast<BYTE**>(object + 0x0C);
    BYTE* connection = manager ? *reinterpret_cast<BYTE**>(manager + 0x84) : nullptr;
    BYTE* firstParty = connection ? *reinterpret_cast<BYTE**>(connection + 0x100) : nullptr;
    void** connectionVtable = connection ? *reinterpret_cast<void***>(connection) : nullptr;
    Log("direct client: link children manager=%p connection=%p connectionVtable=%p initSlot18=%p firstParty=%p",
        manager, connection, connectionVtable, connectionVtable ? connectionVtable[18] : nullptr, firstParty);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: link %s link=%p snapshot fault=%08lX", event, link, GetExceptionCode());
  }
}

void LogReliableLayer(const char* event, void* connection) {
  __try {
    BYTE* object = static_cast<BYTE*>(connection);
    BYTE* reliable = *reinterpret_cast<BYTE**>(object + 0x100);
    BYTE* transport = *reinterpret_cast<BYTE**>(object + 0x104);
    BYTE* backend = transport ? *reinterpret_cast<BYTE**>(transport + 0x8C) : nullptr;
    BYTE* queue = backend ? backend + 0x260 : nullptr;
    Log("direct client: reliable layer %s connection=%p mode=%ld reliable=%p transport=%p "
        "transportState=%ld backend=%p queueStorage=%p queueCapacity=%ld queueBaseIndex=%ld "
        "queueRead=%ld queueWrite=%ld queueCompleted=%ld",
        event, connection, *reinterpret_cast<LONG*>(object + 0xFC), reliable, transport,
        transport ? *reinterpret_cast<LONG*>(transport + 0x3C) : -1,
        backend, queue ? *reinterpret_cast<void**>(queue) : nullptr,
        queue ? *reinterpret_cast<LONG*>(queue + 0x04) : -1,
        queue ? *reinterpret_cast<LONG*>(queue + 0x08) : -1,
        queue ? *reinterpret_cast<LONG*>(queue + 0x0C) : -1,
        queue ? *reinterpret_cast<LONG*>(queue + 0x10) : -1,
        queue ? *reinterpret_cast<LONG*>(queue + 0x14) : -1);
    if (transport && backend) {
      void** transportVtable = *reinterpret_cast<void***>(transport);
      void** backendVtable = *reinterpret_cast<void***>(backend);
      Log("direct client: transport vtable=%p slots 0=%p 1=%p 2=%p 3=%p 4=%p 5=%p 6=%p 7=%p",
          transportVtable, transportVtable[0], transportVtable[1], transportVtable[2], transportVtable[3],
          transportVtable[4], transportVtable[5], transportVtable[6], transportVtable[7]);
      Log("direct client: backend vtable=%p slots 0=%p 1=%p 2=%p 3=%p 4=%p 5=%p 6=%p 7=%p "
          "8=%p 9=%p 10=%p 11=%p 12=%p 13=%p 14=%p 15=%p",
          backendVtable, backendVtable[0], backendVtable[1], backendVtable[2], backendVtable[3],
          backendVtable[4], backendVtable[5], backendVtable[6], backendVtable[7], backendVtable[8],
          backendVtable[9], backendVtable[10], backendVtable[11], backendVtable[12], backendVtable[13],
          backendVtable[14], backendVtable[15]);
    }
    if (!reliable) return;
    void** vtable = *reinterpret_cast<void***>(reliable);
    BYTE* socket = *reinterpret_cast<BYTE**>(reliable + 0x78);
    void** socketVtable = socket ? *reinterpret_cast<void***>(socket) : nullptr;
    Log("direct client: reliable state vtable=%p state=%ld socket=%p socketVtable=%p count=%ld "
        "endpoint0=%p endpoint1=%p endpoint2=%p endpoint3=%p",
        vtable, *reinterpret_cast<LONG*>(reliable + 0x0C), socket, socketVtable,
        *reinterpret_cast<LONG*>(reliable + 0x7C), *reinterpret_cast<void**>(reliable + 0x80),
        *reinterpret_cast<void**>(reliable + 0x84), *reinterpret_cast<void**>(reliable + 0x88),
        g_fourEndpointReliableLayer ? *reinterpret_cast<void**>(reliable + 0x8C) : nullptr);
    BYTE* endpointManager = *reinterpret_cast<BYTE**>(reliable + 0x1C);
    if (endpointManager) {
      const int objectCount = InterlockedCompareExchange(&g_fiveEndpointManagerInstalled, 0, 0) ? 5 : 4;
      Log("direct client: endpoint manager=%p used=%ld objects=%d busy=%u,%u,%u,%u,%u "
          "states=%ld,%ld,%ld,%ld,%ld",
          endpointManager, *reinterpret_cast<LONG*>(endpointManager), objectCount,
          endpointManager[0x28], endpointManager[0x28 + 0x4A8], endpointManager[0x28 + 0x950],
          endpointManager[0x28 + 0xDF8], objectCount == 5 ? endpointManager[0x28 + 0x12A0] : 0,
          *reinterpret_cast<LONG*>(endpointManager + 0x30 + 0x98),
          *reinterpret_cast<LONG*>(endpointManager + 0x30 + 0x4A8 + 0x98),
          *reinterpret_cast<LONG*>(endpointManager + 0x30 + 0x950 + 0x98),
          *reinterpret_cast<LONG*>(endpointManager + 0x30 + 0xDF8 + 0x98),
          objectCount == 5 ? *reinterpret_cast<LONG*>(endpointManager + 0x30 + 0x12A0 + 0x98) : -1);
    }
    if (socketVtable) {
      Log("direct client: reliable socket slots 0=%p 1=%p 2=%p 3=%p 4=%p 5=%p 6=%p 7=%p "
          "8=%p 9=%p 10=%p 11=%p 12=%p 13=%p 14=%p 15=%p",
          socketVtable[0], socketVtable[1], socketVtable[2], socketVtable[3], socketVtable[4],
          socketVtable[5], socketVtable[6], socketVtable[7], socketVtable[8], socketVtable[9],
          socketVtable[10], socketVtable[11], socketVtable[12], socketVtable[13], socketVtable[14],
          socketVtable[15]);
    }
    const int reliableEndpointCount = g_fourEndpointReliableLayer ? 4 : 3;
    for (int i = 0; i < reliableEndpointCount; i++) {
      BYTE* endpoint = *reinterpret_cast<BYTE**>(reliable + 0x80 + i * 4);
      if (!endpoint) continue;
      void** endpointVtable = *reinterpret_cast<void***>(endpoint);
      const unsigned long long peerId = *reinterpret_cast<unsigned long long*>(endpoint + 0x80);
      Log("direct client: endpoint[%d]=%p vtable=%p connectPrefix=%08lX/%08lX "
          "peer=%08lX%08lX remotePort=%u signalingPort=%u previousState=%ld state=%ld ready=%u "
          "retransmitSize=%ld reliableQueued=%ld unreliableQueued=%ld voiceQueued=%ld "
          "connectionTimer=%p timerStarted=%u",
          i, endpoint, endpointVtable,
          *reinterpret_cast<DWORD*>(endpoint + 0x78), *reinterpret_cast<DWORD*>(endpoint + 0x7C),
          static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId),
          *reinterpret_cast<unsigned short*>(endpoint + 0x88),
          *reinterpret_cast<unsigned short*>(endpoint + 0x8A),
          *reinterpret_cast<LONG*>(endpoint + 0x94), *reinterpret_cast<LONG*>(endpoint + 0x98),
          endpoint[0x9C], *reinterpret_cast<LONG*>(endpoint + 0x2C0),
          *reinterpret_cast<LONG*>(endpoint + 0x2E4), *reinterpret_cast<LONG*>(endpoint + 0x2F4),
          *reinterpret_cast<LONG*>(endpoint + 0x304), *reinterpret_cast<void**>(endpoint + 0x30C),
          endpoint[0x310]);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: reliable layer %s snapshot fault=%08lX", event, GetExceptionCode());
  }
}

bool PumpDirectProbeConnection(void* link, int tick) {
  __try {
    BYTE* object = static_cast<BYTE*>(link);
    BYTE* manager = object ? *reinterpret_cast<BYTE**>(object + 0x0C) : nullptr;
    BYTE* connection = manager ? *reinterpret_cast<BYTE**>(manager + 0x84) : nullptr;
    void** vtable = connection ? *reinterpret_cast<void***>(connection) : nullptr;
    BYTE* transport = connection ? *reinterpret_cast<BYTE**>(connection + 0x104) : nullptr;
    BYTE* backend = transport ? *reinterpret_cast<BYTE**>(transport + 0x8C) : nullptr;
    if (!connection || !vtable || !vtable[9]) return false;
    const LONG readBefore = backend ? *reinterpret_cast<LONG*>(backend + 0x26C) : -1;
    const LONG writeBefore = backend ? *reinterpret_cast<LONG*>(backend + 0x270) : -1;
    auto update = reinterpret_cast<ConnectionUpdateFn>(vtable[9]);
    update(connection, 1.0f / 60.0f);
    const LONG readAfter = backend ? *reinterpret_cast<LONG*>(backend + 0x26C) : -1;
    const LONG writeAfter = backend ? *reinterpret_cast<LONG*>(backend + 0x270) : -1;
    if (tick < 4 || readBefore != readAfter || writeBefore != writeAfter || tick % 60 == 0) {
      Log("direct client: native connection update tick=%d connection=%p function=%p "
          "queueRead=%ld->%ld queueWrite=%ld->%ld", tick, connection, vtable[9],
          readBefore, readAfter, writeBefore, writeAfter);
      LogReliableLayer("during native connection update", connection);
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: native packet pump tick=%d fault=%08lX", tick, GetExceptionCode());
    return false;
  }
}

bool __fastcall Hook_ConnectionInitLink(void* connection, void*, void* initInfo) {
  LogReliableLayer("before InitLink", connection);
  const bool result = g_connectionInitLink(connection, initInfo);
  Log("direct client: connection InitLink returned=%d", result);
  LogReliableLayer("after InitLink", connection);
  return result;
}

void __fastcall Hook_ConnectionService(void* connection, void*, void* first, void* second, DWORD third) {
  const LONG call = InterlockedIncrement(&g_connectionServiceCalls);
  if (call <= 4 || call % 60 == 0) {
    Log("direct client: connection service call=%ld connection=%p first=%p second=%p third=%08lX",
        call, connection, first, second, third);
    LogReliableLayer("before connection service", connection);
  }
  g_connectionService(connection, first, second, third);
  if (call <= 4 || call % 60 == 0) LogReliableLayer("after connection service", connection);
}

bool SyntheticConnectionIsHealthy(void* connection) {
  __try {
    BYTE* object = static_cast<BYTE*>(connection);
    BYTE* reliable = *reinterpret_cast<BYTE**>(object + 0x100);
    if (!reliable || *reinterpret_cast<LONG*>(object + 0xFC) != 1) return false;
    const LONG count = *reinterpret_cast<LONG*>(reliable + 0x7C);
    if (count < 1 || count > 4) return false;
    for (LONG index = 0; index < count; index++) {
      BYTE* endpoint = *reinterpret_cast<BYTE**>(reliable + 0x80 + index * 4);
      if (!endpoint) return false;
      const LONG state = *reinterpret_cast<LONG*>(endpoint + 0x98);
      if (state == 8 || state == 9) return false;
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void __fastcall Hook_ConnectionShutdown(void* connection, void*) {
  void* caller = _ReturnAddress();
  Log("direct client: connection Shutdown enter connection=%p caller=%p", connection, caller);
  LogReliableLayer("before connection Shutdown", connection);
  if (g_directP2PProbe && SyntheticConnectionIsHealthy(connection)) {
    InterlockedExchange(&g_syntheticTransportDetached, 1);
    Log("direct client: suppressed frontend connection Shutdown for healthy synthetic transport probe");
    return;
  }
  g_connectionShutdown(connection);
  LogReliableLayer("after connection Shutdown", connection);
  Log("direct client: connection Shutdown leave connection=%p caller=%p", connection, caller);
}

bool InstallConnectionInitLinkTrace(void* manager) {
  __try {
    BYTE* connection = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(manager) + 0x84);
    void** source = connection ? *reinterpret_cast<void***>(connection) : nullptr;
    if (source == g_connectionTraceVtable) return true;
    if (!connection || !source || source[8] != reinterpret_cast<void*>(0x00889B90) ||
        source[18] != reinterpret_cast<void*>(0x00889C30)) {
      Log("direct client: connection trace signature mismatch connection=%p vtable=%p slot8=%p slot18=%p",
          connection, source, source ? source[8] : nullptr, source ? source[18] : nullptr);
      return false;
    }
    static const BYTE kListenSignature[] = {
      0x8B, 0x89, 0x00, 0x01, 0x00, 0x00, 0x83, 0xEC,
      0x08, 0x32, 0xC0, 0x85, 0xC9, 0x74, 0x1B
    };
    BYTE* moduleBase = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    void* nativeListen = moduleBase + (0x00889C00 - 0x00400000);
    if (memcmp(nativeListen, kListenSignature, sizeof(kListenSignature)) != 0) {
      Log("direct host: native connection Listen signature mismatch function=%p", nativeListen);
      return false;
    }
    memcpy(g_connectionTraceVtable, source, sizeof(g_connectionTraceVtable));
    g_connectionShutdown = reinterpret_cast<ConnectionShutdownFn>(source[8]);
    g_connectionInitLink = reinterpret_cast<ConnectionInitLinkFn>(source[18]);
    g_connectionListen = reinterpret_cast<ConnectionListenFn>(nativeListen);
    g_connectionService = reinterpret_cast<ConnectionServiceFn>(source[24]);
    g_connectionTraceVtable[8] = &Hook_ConnectionShutdown;
    g_connectionTraceVtable[18] = &Hook_ConnectionInitLink;
    g_connectionTraceVtable[24] = &Hook_ConnectionService;
    *reinterpret_cast<void***>(connection) = g_connectionTraceVtable;
    Log("direct client: connection shutdown/InitLink trace installed connection=%p vtable=%p", connection, source);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: connection InitLink trace installation fault=%08lX", GetExceptionCode());
    return false;
  }
}

bool InstallFourEndpointReliableLayerProbe(BYTE* base) {
  if (InterlockedCompareExchange(&g_reliableLayerLayoutInstalled, 0, 0)) return true;
  static const BYTE kLoop3EdxJl[] = {0x83, 0xFA, 0x03};
  static const BYTE kLoop4EdxJl[] = {0x83, 0xFA, 0x04};
  static const BYTE kLoop3EaxJl[] = {0x83, 0xF8, 0x03};
  static const BYTE kLoop4EaxJl[] = {0x83, 0xF8, 0x04};
  static const BYTE kLoop3EbxJl[] = {0x83, 0xFB, 0x03};
  static const BYTE kLoop4EbxJl[] = {0x83, 0xFB, 0x04};
  static const BYTE kLoop3EdiJl[] = {0x83, 0xFF, 0x03};
  static const BYTE kLoop4EdiJl[] = {0x83, 0xFF, 0x04};
  static const BYTE kEndpointCount3Edi[] = {0xBF, 0x03, 0x00, 0x00, 0x00};
  static const BYTE kEndpointCount4Edi[] = {0xBF, 0x04, 0x00, 0x00, 0x00};
  static const BYTE kLoop3EbpJl[] = {0x83, 0xFD, 0x03};
  static const BYTE kLoop4EbpJl[] = {0x83, 0xFD, 0x04};
  static const BYTE kLoop3EcxJb[] = {0x83, 0xF9, 0x03};
  static const BYTE kLoop4EcxJb[] = {0x83, 0xF9, 0x04};
  static const BYTE kResetCount3[] = {0xC7, 0x44, 0x24, 0x0C, 0x03, 0x00, 0x00, 0x00};
  static const BYTE kResetCount4[] = {0xC7, 0x44, 0x24, 0x0C, 0x04, 0x00, 0x00, 0x00};
  static const BYTE kAllocationA8[] = {0x68, 0xA8, 0x00, 0x00, 0x00};
  static const BYTE kAllocationB0[] = {0x68, 0xB0, 0x00, 0x00, 0x00};
  static const BYTE kBandwidthEvent3[] = {0xF3, 0x0F, 0x11, 0x84, 0xBD, 0x88, 0x00, 0x00, 0x00};
  static const BYTE kBandwidthEvent4[] = {0xF3, 0x0F, 0x11, 0x84, 0xBD, 0x8C, 0x00, 0x00, 0x00};
  struct Patch {
    DWORD address;
    const BYTE* expected;
    const BYTE* replacement;
    size_t size;
  };
  const Patch patches[] = {
    {0x0087F796, kAllocationA8, kAllocationB0, sizeof(kAllocationA8)},
    {0x0084BDA5, kLoop3EdxJl, kLoop4EdxJl, sizeof(kLoop3EdxJl)},
    {0x0084BDE6, kLoop3EdxJl, kLoop4EdxJl, sizeof(kLoop3EdxJl)},
    {0x0084BE32, kLoop3EdxJl, kLoop4EdxJl, sizeof(kLoop3EdxJl)},
    {0x0084BE6E, kLoop3EdxJl, kLoop4EdxJl, sizeof(kLoop3EdxJl)},
    {0x00859171, kLoop3EdxJl, kLoop4EdxJl, sizeof(kLoop3EdxJl)},
    {0x0087F8CF, kLoop3EdiJl, kLoop4EdiJl, sizeof(kLoop3EdiJl)},
    {0x0087FA61, kLoop3EaxJl, kLoop4EaxJl, sizeof(kLoop3EaxJl)},
    {0x0087FBD1, kLoop3EaxJl, kLoop4EaxJl, sizeof(kLoop3EaxJl)},
    {0x0087FCD6, kLoop3EaxJl, kLoop4EaxJl, sizeof(kLoop3EaxJl)},
    {0x0087FD4D, kLoop3EdiJl, kLoop4EdiJl, sizeof(kLoop3EdiJl)},
    {0x00888890, kResetCount3, kResetCount4, sizeof(kResetCount3)},
    {0x00888B45, kLoop3EdiJl, kLoop4EdiJl, sizeof(kLoop3EdiJl)},
    {0x00888BAF, kLoop3EcxJb, kLoop4EcxJb, sizeof(kLoop3EcxJb)},
    {0x00888C4F, kLoop3EdiJl, kLoop4EdiJl, sizeof(kLoop3EdiJl)},
    {0x00883E49, kLoop3EcxJb, kLoop4EcxJb, sizeof(kLoop3EcxJb)},
    {0x00883E93, kLoop3EbpJl, kLoop4EbpJl, sizeof(kLoop3EbpJl)},
    {0x00889D34, kLoop3EbxJl, kLoop4EbxJl, sizeof(kLoop3EbxJl)},
    {0x00889E80, kLoop3EdiJl, kLoop4EdiJl, sizeof(kLoop3EdiJl)},
    {0x00889EF3, kEndpointCount3Edi, kEndpointCount4Edi, sizeof(kEndpointCount3Edi)},
    {0x00888C95, kBandwidthEvent3, kBandwidthEvent4, sizeof(kBandwidthEvent3)},
  };
  struct BytePatch { DWORD address; BYTE expected; BYTE replacement; };
  const BytePatch fieldPatches[] = {
    {0x00858EEB, 0x8C, 0x90}, {0x00858EFB, 0x90, 0x94}, {0x00858F0B, 0x94, 0x98},
    {0x00858F63, 0xA0, 0xA8}, {0x00858F69, 0xA4, 0xAC},
    {0x0087FCF7, 0x8C, 0x90}, {0x0087FD05, 0x8C, 0x90},
    {0x00888858, 0x8C, 0x90}, {0x00888868, 0x90, 0x94}, {0x00888878, 0x94, 0x98},
    {0x00888ABD, 0xA0, 0xA8}, {0x00888AC7, 0xA4, 0xAC},
    {0x00888AE0, 0xA0, 0xA8}, {0x00888AE6, 0xA4, 0xAC},
    {0x00883E42, 0x98, 0xA0}, {0x00883E4E, 0x98, 0xA0}, {0x00883E56, 0x98, 0xA0},
    {0x0088ABBE, 0x9C, 0xA4}, {0x0088ABC5, 0x9C, 0xA4},
  };
  for (const Patch& patch : patches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (memcmp(address, patch.expected, patch.size) != 0) {
      Log("direct capacity: signature mismatch address=%08lX", patch.address);
      return false;
    }
  }
  for (const BytePatch& patch : fieldPatches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (*address != patch.expected) {
      Log("direct capacity: field signature mismatch address=%08lX expected=%02X actual=%02X",
          patch.address, patch.expected, *address);
      return false;
    }
  }
  BYTE* ctorCall = base + (0x0087F7B3 - 0x00400000);
  BYTE* ctor = base + (0x00858D50 - 0x00400000);
  LONG ctorRelative = 0;
  memcpy(&ctorRelative, ctorCall + 1, sizeof(ctorRelative));
  if (ctorCall[0] != 0xE8 || ctorCall + 5 + ctorRelative != ctor) {
    Log("direct capacity: reliable constructor call signature mismatch");
    return false;
  }
  BYTE* acceptCall = base + (0x00883DAF - 0x00400000);
  BYTE* accept = base + (0x0087F880 - 0x00400000);
  LONG acceptRelative = 0;
  memcpy(&acceptRelative, acceptCall + 1, sizeof(acceptRelative));
  if (acceptCall[0] != 0xE8 || acceptCall + 5 + acceptRelative != accept) {
    Log("direct capacity: reliable Accept call signature mismatch");
    return false;
  }
  for (const Patch& patch : patches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (!WriteSlot(address, patch.replacement, patch.size)) {
      Log("direct capacity: write failed address=%08lX", patch.address);
      return false;
    }
  }
  for (const BytePatch& patch : fieldPatches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (!WriteSlot(address, &patch.replacement, sizeof(patch.replacement))) return false;
  }
  g_reliableLayerCtor = reinterpret_cast<ReliableLayerCtorFn>(ctor);
  g_reliableLayerAccept = reinterpret_cast<ReliableLayerAcceptFn>(accept);
  BYTE ctorReplacement[5] = {0xE8};
  ctorRelative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_ReliableLayerCtor) - (ctorCall + 5));
  memcpy(ctorReplacement + 1, &ctorRelative, sizeof(ctorRelative));
  if (!WriteSlot(ctorCall, ctorReplacement, sizeof(ctorReplacement))) return false;
  BYTE acceptReplacement[5] = {0xE8};
  acceptRelative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_ReliableLayerAccept) - (acceptCall + 5));
  memcpy(acceptReplacement + 1, &acceptRelative, sizeof(acceptRelative));
  if (!WriteSlot(acceptCall, acceptReplacement, sizeof(acceptReplacement))) return false;
  InterlockedExchange(&g_reliableLayerLayoutInstalled, 1);
  g_fourEndpointReliableLayer = true;
  Log("direct capacity: reliable layer expanded 0xA8 -> 0xB0 with four endpoint and bandwidth slots");
  return true;
}

bool InstallFiveEndpointManagerProbe(BYTE* base) {
  if (InterlockedCompareExchange(&g_fiveEndpointManagerInstalled, 0, 0)) return true;
  static const BYTE kAllocation4[] = {0x68, 0xA8, 0x12, 0x00, 0x00};
  static const BYTE kAllocation5[] = {0x68, 0x50, 0x17, 0x00, 0x00};
  static const BYTE kConstruct4[] = {0xBF, 0x03, 0x00, 0x00, 0x00};
  static const BYTE kConstruct5[] = {0xBF, 0x04, 0x00, 0x00, 0x00};
  static const BYTE kInit4[] = {0x83, 0xFD, 0x04};
  static const BYTE kInit5[] = {0x83, 0xFD, 0x05};
  static const BYTE kBound4Edx[] = {0x83, 0xFA, 0x04};
  static const BYTE kBound5Edx[] = {0x83, 0xFA, 0x05};
  static const BYTE kBound4Eax[] = {0x83, 0xF8, 0x04};
  static const BYTE kBound5Eax[] = {0x83, 0xF8, 0x05};
  struct Patch {
    DWORD address;
    const BYTE* expected;
    const BYTE* replacement;
    size_t size;
  };
  const Patch patches[] = {
    {0x0088909A, kAllocation4, kAllocation5, sizeof(kAllocation4)},
    {0x008886C5, kConstruct4, kConstruct5, sizeof(kConstruct4)},
    {0x00883E00, kInit4, kInit5, sizeof(kInit4)},
    {0x0084BD05, kBound4Edx, kBound5Edx, sizeof(kBound4Edx)},
    {0x0084BD5D, kBound4Eax, kBound5Eax, sizeof(kBound4Eax)},
  };
  for (const Patch& patch : patches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (memcmp(address, patch.expected, patch.size) != 0) return false;
  }
  BYTE* initCall = base + (0x00888785 - 0x00400000);
  BYTE* init = base + (0x008841C0 - 0x00400000);
  LONG initRelative = 0;
  memcpy(&initRelative, initCall + 1, sizeof(initRelative));
  if (initCall[0] != 0xE8 || initCall + 5 + initRelative != init) return false;
  for (const Patch& patch : patches) {
    BYTE* address = base + (patch.address - 0x00400000);
    if (!WriteSlot(address, patch.replacement, patch.size)) return false;
  }
  g_endPointInit = reinterpret_cast<EndPointInitFn>(init);
  BYTE initReplacement[5] = {0xE8};
  initRelative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_EndPointInit) - (initCall + 5));
  memcpy(initReplacement + 1, &initRelative, sizeof(initRelative));
  if (!WriteSlot(initCall, initReplacement, sizeof(initReplacement))) return false;
  InterlockedExchange(&g_fiveEndpointManagerInstalled, 1);
  Log("direct capacity: endpoint manager expanded 4 -> 5 allocation=0x1750 stride=0x4A8");
  return true;
}

DWORD WINAPI EarlyEndpointManagerPatchThread(LPVOID) {
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  for (int attempt = 0; attempt < 200; attempt++) {
    const bool managerReady = InstallFiveEndpointManagerProbe(base);
    const bool reliableReady = InstallFourEndpointReliableLayerProbe(base);
    if (managerReady && reliableReady) return 0;
    Sleep(10);
  }
  Log("direct capacity: early layout patches never became ready manager=%ld reliable=%ld",
      InterlockedCompareExchange(&g_fiveEndpointManagerInstalled, 0, 0),
      InterlockedCompareExchange(&g_reliableLayerLayoutInstalled, 0, 0));
  return 0;
}

bool __fastcall Hook_LinkInit(void* link, void*, void* initInfo) {
  void* manager = nullptr;
  __try {
    manager = *reinterpret_cast<void**>(static_cast<BYTE*>(link) + 0x0C);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: cLink::Init manager snapshot fault=%08lX", GetExceptionCode());
  }
  Log("direct client: cLink::Init enter link=%p manager=%p init=%p", link, manager, initInfo);
  g_directProbeManager = manager;
  if (manager) InstallConnectionInitLinkTrace(manager);
  const bool result = g_linkInit(link, initInfo);
  Log("direct client: cLink::Init returned=%d link=%p", result, link);
  return result;
}

bool __fastcall Hook_LinkSend(void* link, void*, void* packet) {
  // 0x00887010 is cConnLink2::Send, not a receive-event callback. Preserve both
  // its destination object and boolean result, including across trace calls.
  const bool result = g_linkSend(link, packet);
  Log("direct link: Send link=%p packet=%p result=%d", link, packet, result);
  return result;
}

void __fastcall Hook_LinkUpdate(void* link, void*, float deltaSeconds) {
  const LONG call = InterlockedIncrement(&g_linkUpdateCalls);
  float before = 0.0f;
  __try {
    before = *reinterpret_cast<float*>(static_cast<BYTE*>(link) + 0xB8);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  g_linkUpdate(link, deltaSeconds);
  float after = before;
  __try {
    after = *reinterpret_cast<float*>(static_cast<BYTE*>(link) + 0xB8);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  if (call == 1 || call % 30 == 0 || after < before) {
    Log("direct client: cLink::Update call=%ld link=%p dt=%.4f timeout=%.3f->%.3f",
        call, link, deltaSeconds, before, after);
  }
}

bool InstallLinkInitTrace(BYTE* base) {
  void** initSlot = reinterpret_cast<void**>(base + (0x00CBA214 - 0x00400000));
  void** eventSlot = reinterpret_cast<void**>(base + (0x00CBA218 - 0x00400000));
  void** updateSlot = reinterpret_cast<void**>(base + (0x00CBA220 - 0x00400000));
  void* expectedInit = base + (0x00863200 - 0x00400000);
  void* expectedEvent = base + (0x00887010 - 0x00400000);
  void* expectedUpdate = base + (0x00863430 - 0x00400000);
  if (*initSlot != expectedInit || *eventSlot != expectedEvent || *updateSlot != expectedUpdate) {
    Log("direct client: cLink vtable signature mismatch initSlot=%p init=%p expectedInit=%p "
        "eventSlot=%p event=%p expectedEvent=%p updateSlot=%p update=%p expectedUpdate=%p",
        initSlot, *initSlot, expectedInit, eventSlot, *eventSlot, expectedEvent,
        updateSlot, *updateSlot, expectedUpdate);
    return false;
  }
  g_linkInit = reinterpret_cast<LinkInitFn>(*initSlot);
  g_linkSend = reinterpret_cast<LinkSendFn>(*eventSlot);
  g_linkUpdate = reinterpret_cast<LinkUpdateFn>(*updateSlot);
  void* initReplacement = &Hook_LinkInit;
  void* eventReplacement = &Hook_LinkSend;
  void* updateReplacement = &Hook_LinkUpdate;
  const bool initInstalled = WriteSlot(initSlot, &initReplacement, sizeof(initReplacement));
  const bool eventInstalled = WriteSlot(eventSlot, &eventReplacement, sizeof(eventReplacement));
  const bool updateInstalled = WriteSlot(updateSlot, &updateReplacement, sizeof(updateReplacement));
  Log("direct client: cLink Init/Send/update trace init=%s send=%s update=%s",
      initInstalled ? "installed" : "FAILED", eventInstalled ? "installed" : "FAILED",
      updateInstalled ? "installed" : "FAILED");
  return initInstalled && eventInstalled && updateInstalled;
}

void __fastcall Hook_RemoteServerDie(void* server, void*) {
  LogRemoteServerState("Die enter", server, _ReturnAddress());
  if (g_directP2PProbe) {
    InterlockedExchange(&g_syntheticTransportDetached, 1);
    Log("direct client: suppressed frontend cRemoteServer::Die for active synthetic transport probe");
    return;
  }
  g_remoteServerDie(server);
  LogRemoteServerState("Die leave", server, _ReturnAddress());
}

bool __fastcall Hook_RemoteServerLinkConnected(void* server, void*) {
  LogRemoteServerState("HandleLinkConnected enter", server, _ReturnAddress());
  const bool result = g_remoteServerLinkConnected(server);
  LogRemoteServerState(result ? "HandleLinkConnected leave=true" : "HandleLinkConnected leave=false",
                       server, _ReturnAddress());
  return result;
}

bool __fastcall Hook_RemoteServerLinkShutdown(void* server, void*, const void* node) {
  LogRemoteServerState("HandleLinkShutdown enter", server, _ReturnAddress());
  Log("direct client: remote HandleLinkShutdown node=%p", node);
  const bool result = g_remoteServerLinkShutdown(server, node);
  LogRemoteServerState(result ? "HandleLinkShutdown leave=true" : "HandleLinkShutdown leave=false",
                       server, _ReturnAddress());
  return result;
}

bool __fastcall Hook_RemoteServerShutdown(void* server, void*, const void* node) {
  LogRemoteServerState("Shutdown enter", server, _ReturnAddress());
  Log("direct client: remote Shutdown node=%p", node);
  const bool result = g_remoteServerShutdown(server, node);
  LogRemoteServerState(result ? "Shutdown leave=true" : "Shutdown leave=false", server, _ReturnAddress());
  return result;
}

bool InstallRemoteServerTeardownTrace(void* server) {
  __try {
    void** source = *reinterpret_cast<void***>(server);
    // The object points at a contiguous block: two primary iterator slots, four stateful-object
    // slots, then the 13-slot event-listener table. These are the absolute block indices for the
    // four OTR-PDB-identified lifecycle methods in vanilla DR2.
    if (!source || !source[10] || !source[13] || !source[14] || !source[18]) {
      Log("direct client: remote teardown trace rejected invalid vtable=%p", source);
      return false;
    }
    memcpy(g_remoteServerTraceVtable, source, sizeof(g_remoteServerTraceVtable));
    g_remoteServerDie = reinterpret_cast<RemoteServerDieFn>(source[10]);
    g_remoteServerLinkConnected = reinterpret_cast<RemoteServerBoolFn>(source[13]);
    g_remoteServerLinkShutdown = reinterpret_cast<RemoteServerLinkShutdownFn>(source[14]);
    g_remoteServerShutdown = reinterpret_cast<RemoteServerShutdownFn>(source[18]);
    g_remoteServerTraceVtable[10] = &Hook_RemoteServerDie;
    g_remoteServerTraceVtable[13] = &Hook_RemoteServerLinkConnected;
    g_remoteServerTraceVtable[14] = &Hook_RemoteServerLinkShutdown;
    g_remoteServerTraceVtable[18] = &Hook_RemoteServerShutdown;
    *reinterpret_cast<void***>(server) = g_remoteServerTraceVtable;
    Log("direct client: remote teardown trace installed server=%p vtable=%p "
        "die=%p connected=%p linkShutdown=%p shutdown=%p",
        server, source, g_remoteServerDie, g_remoteServerLinkConnected,
        g_remoteServerLinkShutdown, g_remoteServerShutdown);
    LogRemoteServerState("trace baseline", server, nullptr);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: remote teardown trace installation fault=%08lX", GetExceptionCode());
    return false;
  }
}

bool __fastcall Hook_LinkManagerGetOrCreate(void* manager, void*, void* initInfo, void** link) {
  __try {
    BYTE* info = static_cast<BYTE*>(initInfo);
    Log("direct client: GetOrCreate link init=%p isP2P=%u bypassFirstParty=%u "
        "local=%08lX remote=%08lX bind=%u remotePort=%u signaling=%u security=%ld socket=%p endpoint=%p type=%ld",
        initInfo, info[0], info[1], *reinterpret_cast<DWORD*>(info + 0x10),
        *reinterpret_cast<DWORD*>(info + 0x14), *reinterpret_cast<unsigned short*>(info + 0x18),
        *reinterpret_cast<unsigned short*>(info + 0x1A), *reinterpret_cast<unsigned short*>(info + 0x1C),
        *reinterpret_cast<LONG*>(info + 0x20), *reinterpret_cast<void**>(info + 0x24),
        *reinterpret_cast<void**>(info + 0x28), *reinterpret_cast<LONG*>(info + 0x2C));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct client: GetOrCreate init snapshot fault=%08lX", GetExceptionCode());
  }
  const bool result = g_linkManagerGetOrCreate(manager, initInfo, link);
  void* created = nullptr;
  __try {
    created = link ? *link : nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  g_directProbeLink = created;
  Log("direct client: GetOrCreate returned=%d link=%p", result, created);
  if (created) LogLinkState("created", created);
  return result;
}

void* __fastcall Hook_PeerOwnerCreate(void* owner, void*, void* initInfo) {
  Log("direct peer owner: native create enter owner=%p manager=%p init=%p primary=%p", owner,
      owner ? *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x50) : nullptr, initInfo,
      owner ? *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x7C) : nullptr);
  void* result = g_peerOwnerCreate(owner, initInfo);
  Log("direct peer owner: native create leave owner=%p result=%p primary=%p", owner, result,
      owner ? *reinterpret_cast<void**>(static_cast<BYTE*>(owner) + 0x7C) : nullptr);
  return result;
}

bool InstallLinkGetOrCreateTrace(BYTE* base) {
  BYTE* callSite = base + (0x0087B3F3 - 0x00400000);
  BYTE* original = base + (0x00872620 - 0x00400000);
  LONG relative = 0;
  memcpy(&relative, callSite + 1, sizeof(relative));
  if (callSite[0] != 0xE8 || callSite + 5 + relative != original) {
    Log("direct client: LinkManager::GetOrCreate call signature mismatch");
    return false;
  }
  g_linkManagerGetOrCreate = reinterpret_cast<LinkManagerGetOrCreateFn>(original);
  BYTE replacement[5] = {0xE8};
  relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_LinkManagerGetOrCreate) - (callSite + 5));
  memcpy(replacement + 1, &relative, sizeof(relative));
  const bool installed = WriteSlot(callSite, replacement, sizeof(replacement));
  Log("direct client: LinkManager::GetOrCreate trace %s", installed ? "installed" : "FAILED");
  return installed;
}

bool InstallPeerOwnerCreateTrace(BYTE* base) {
  BYTE* callSite = base + (0x0087C980 - 0x00400000);
  BYTE* original = base + (0x0087B3D0 - 0x00400000);
  LONG relative = 0;
  memcpy(&relative, callSite + 1, sizeof(relative));
  if (callSite[0] != 0xE8 || callSite + 5 + relative != original) {
    Log("direct peer owner: create call signature mismatch target=%p", callSite + 5 + relative);
    return false;
  }
  g_peerOwnerCreate = reinterpret_cast<PeerOwnerCreateFn>(original);
  BYTE replacement[5] = {0xE8};
  relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_PeerOwnerCreate) - (callSite + 5));
  memcpy(replacement + 1, &relative, sizeof(relative));
  const bool installed = WriteSlot(callSite, replacement, sizeof(replacement));
  Log("direct peer owner: native create trace %s", installed ? "installed" : "FAILED");
  return installed;
}

void __fastcall Hook_LobbyDataParse(void* manager, void*, void* lobbyId, void* output) {
  g_lobbyDataParse(manager, lobbyId, output);
  const LONG call = InterlockedIncrement(&g_lobbyDataParseCalls);
  if (call > 5 && call % 100 != 0) return;
  __try {
    const auto* fields = static_cast<const BYTE*>(output);
    const int gameMode = *reinterpret_cast<const int*>(fields + 0x10);
    const int ranked = *reinterpret_cast<const int*>(fields + 0x14);
    const int publicOpen = *reinterpret_cast<const int*>(fields + 0x18);
    const int privateOpen = *reinterpret_cast<const int*>(fields + 0x1C);
    const int publicFilled = *reinterpret_cast<const int*>(fields + 0x20);
    const int privateFilled = *reinterpret_cast<const int*>(fields + 0x24);
    const int searchType = *reinterpret_cast<const int*>(fields + 0x28);
    const int expectedGameMode = *reinterpret_cast<const int*>(static_cast<const BYTE*>(manager) + 0x284);
    const void* settings = *reinterpret_cast<void* const*>(static_cast<const BYTE*>(manager) + 0x290);
    const int expectedRanked = *reinterpret_cast<const int*>(static_cast<const BYTE*>(settings) + 0x28);
    Log("lobby candidate #%ld parsed gameMode=%d ranked=%d public=%d/%d private=%d/%d search=%d expected gameMode=%d ranked=%d",
        call, gameMode, ranked, publicFilled, publicOpen, privateFilled, privateOpen, searchType,
        expectedGameMode, expectedRanked);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("lobby candidate #%ld telemetry unreadable exception=%08lX", call, GetExceptionCode());
  }
}

bool InstallLobbyCandidateTrace(BYTE* base) {
  BYTE* callSite = base + (0x008D117C - 0x00400000);
  BYTE* original = base + (0x008CE690 - 0x00400000);
  LONG relative = 0;
  memcpy(&relative, callSite + 1, sizeof(relative));
  if (callSite[0] != 0xE8 || callSite + 5 + relative != original) {
    Log("lobby candidate trace: call signature mismatch target=%p", callSite + 5 + relative);
    return false;
  }
  g_lobbyDataParse = reinterpret_cast<LobbyDataParseFn>(original);
  BYTE replacement[5] = {0xE8};
  relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_LobbyDataParse) - (callSite + 5));
  memcpy(replacement + 1, &relative, sizeof(relative));
  const bool installed = WriteSlot(callSite, replacement, sizeof(replacement));
  Log("lobby candidate trace: %s", installed ? "installed" : "FAILED");
  return installed;
}

// Printable copy of a C string argument, or "<bad>" when the pointer is not readable.
const char* SafeString(DWORD pointer, char* buffer, size_t size) {
  __try {
    const char* text = reinterpret_cast<const char*>(pointer);
    size_t i = 0;
    for (; i + 1 < size && text[i]; i++) buffer[i] = (text[i] >= 32 && text[i] < 127) ? text[i] : '?';
    buffer[i] = 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    strcpy_s(buffer, size, "<bad>");
  }
  return buffer;
}

// ---- Steam interface proxies -------------------------------------------------------------------------------------
// A proxy object is {vtable, real object, interface}. Each vtable slot is a naked thunk that logs the call (slot and
// raw stack arguments) and then jumps to the real method with ecx = real object, so arguments, hidden return
// pointers and callee stack cleanup are untouched.

enum Interface { kMatchmaking, kNetworking, kUser, kFriends, kUtils, kApps, kInterfaceCount };
const char* const kInterfaceNames[kInterfaceCount] = {"SteamMatchmaking", "SteamNetworking", "SteamUser",
                                                      "SteamFriends", "SteamUtils", "SteamApps"};

// Method names for the interface versions DR2's steam_api.dll exposes (SteamMatchMaking009, SteamNetworking005;
// SteamUser017 / SteamUtils007 prefixes match the SDK header order used here). SteamFriends014 and SteamApps are
// logged by slot number only.
const char* const kMatchmakingMethods[] = {
    "GetFavoriteGameCount", "GetFavoriteGame", "AddFavoriteGame", "RemoveFavoriteGame", "RequestLobbyList",
    "AddRequestLobbyListStringFilter", "AddRequestLobbyListNumericalFilter", "AddRequestLobbyListNearValueFilter",
    "AddRequestLobbyListFilterSlotsAvailable", "AddRequestLobbyListDistanceFilter", "AddRequestLobbyListResultCountFilter",
    "AddRequestLobbyListCompatibleMembersFilter", "GetLobbyByIndex", "CreateLobby", "JoinLobby", "LeaveLobby",
    "InviteUserToLobby", "GetNumLobbyMembers", "GetLobbyMemberByIndex", "GetLobbyData", "SetLobbyData",
    "GetLobbyDataCount", "GetLobbyDataByIndex", "DeleteLobbyData", "GetLobbyMemberData", "SetLobbyMemberData",
    "SendLobbyChatMsg", "GetLobbyChatEntry", "RequestLobbyData", "SetLobbyGameServer", "GetLobbyGameServer",
    "SetLobbyMemberLimit", "GetLobbyMemberLimit", "SetLobbyType", "SetLobbyJoinable", "GetLobbyOwner", "SetLobbyOwner",
    "SetLinkedLobby"};
const char* const kNetworkingMethods[] = {
    "SendP2PPacket", "IsP2PPacketAvailable", "ReadP2PPacket", "AcceptP2PSessionWithUser", "CloseP2PSessionWithUser",
    "CloseP2PChannelWithUser", "GetP2PSessionState", "AllowP2PPacketRelay", "CreateListenSocket",
    "CreateP2PConnectionSocket", "CreateConnectionSocket", "DestroySocket", "DestroyListenSocket", "SendDataOnSocket",
    "IsDataAvailableOnSocket", "RetrieveDataFromSocket", "IsDataAvailable", "RetrieveData", "GetSocketInfo",
    "GetListenSocketInfo", "GetSocketConnectionType", "GetMaxPacketSize"};
const char* const kUserMethods[] = {"GetHSteamUser", "BLoggedOn", "GetSteamID", "InitiateGameConnection",
                                    "TerminateGameConnection", "TrackAppUsageEvent", "GetUserDataFolder",
                                    "StartVoiceRecording", "StopVoiceRecording", "GetAvailableVoice", "GetVoice",
                                    "DecompressVoice", "GetVoiceOptimalSampleRate", "GetAuthSessionTicket",
                                    "BeginAuthSession", "EndAuthSession", "CancelAuthTicket", "UserHasLicenseForApp",
                                     "BIsBehindNAT", "AdvertiseGame", "RequestEncryptedAppTicket", "GetEncryptedAppTicket"};
const char* const kAppsMethods[] = {
    "BIsSubscribed", "BIsLowViolence", "BIsCybercafe", "BIsVACBanned", "GetCurrentGameLanguage",
    "GetAvailableGameLanguages", "BIsSubscribedApp", "BIsDlcInstalled", "GetEarliestPurchaseUnixTime",
    "BIsSubscribedFromFreeWeekend", "GetDLCCount", "BGetDLCDataByIndex", "InstallDLC", "UninstallDLC",
    "RequestAppProofOfPurchaseKey", "GetCurrentBetaName", "MarkContentCorrupt", "GetInstalledDepots",
    "GetAppInstallDir", "BIsAppInstalled", "GetAppOwner", "GetLaunchQueryParam", "RegisterActivationCode"};
const char* const kUtilsMethods[] = {"GetSecondsSinceAppActive", "GetSecondsSinceComputerActive", "GetConnectedUniverse",
                                     "GetServerRealTime", "GetIPCountry", "GetImageSize", "GetImageRGBA", "GetCSERIPPort",
                                     "GetCurrentBatteryPower", "GetAppID", "SetOverlayNotificationPosition",
                                     "IsAPICallCompleted", "GetAPICallFailureReason", "GetAPICallResult", "RunFrame"};

const char* MethodName(int iface, int slot, char* buffer) {
  const char* const* table = nullptr;
  int count = 0;
  switch (iface) {
    case kMatchmaking: table = kMatchmakingMethods; count = _countof(kMatchmakingMethods); break;
    case kNetworking: table = kNetworkingMethods; count = _countof(kNetworkingMethods); break;
    case kUser: table = kUserMethods; count = _countof(kUserMethods); break;
    case kApps: table = kAppsMethods; count = _countof(kAppsMethods); break;
    case kUtils: table = kUtilsMethods; count = _countof(kUtilsMethods); break;
  }
  if (slot < count) return table[slot];
  sprintf_s(buffer, 16, "slot%d", slot);
  return buffer;
}

struct Proxy {
  void** vtable;
  void* real;
  int iface;
};

constexpr int kSlots = 80;
LONG g_callCounts[kInterfaceCount][kSlots];

// Decodes the arguments that matter for co-op (lobby sizes, lobby data keys and values); everything else is logged as
// raw stack words.
void __stdcall LogSlot(Proxy* proxy, int slot, const DWORD* args) {
  const LONG count = InterlockedIncrement(&g_callCounts[proxy->iface][slot]);
  // Frequent calls (per-frame polling, packet traffic) are logged for the first 40 calls and then every 2000th.
  if (count > 40 && count % 2000 != 0) return;
  char name[16], a[128], b[128];
  const char* method = MethodName(proxy->iface, slot, name);
  if (proxy->iface == kMatchmaking) {
    switch (slot) {
      case 13: Log("%s.%s(type=%lu, maxMembers=%lu) #%ld", kInterfaceNames[proxy->iface], method, args[0], args[1], count); return;
      case 19: case 23: Log("%s.%s(lobby=%08lX%08lX, key=\"%s\") #%ld", kInterfaceNames[proxy->iface], method, args[1], args[0], SafeString(args[2], a, sizeof(a)), count); return;
      case 20: case 25: Log("%s.%s(lobby=%08lX%08lX, key=\"%s\", value=\"%s\") #%ld", kInterfaceNames[proxy->iface], method, args[1], args[0], SafeString(args[2], a, sizeof(a)), SafeString(args[3], b, sizeof(b)), count); return;
      case 24: Log("%s.%s(lobby=%08lX%08lX, user=%08lX%08lX, key=\"%s\") #%ld", kInterfaceNames[proxy->iface], method, args[1], args[0], args[3], args[2], SafeString(args[4], a, sizeof(a)), count); return;
      case 31: Log("%s.%s(lobby=%08lX%08lX, max=%lu) #%ld", kInterfaceNames[proxy->iface], method, args[1], args[0], args[2], count); return;
      case 5: Log("%s.%s(key=\"%s\", value=\"%s\", cmp=%ld) #%ld", kInterfaceNames[proxy->iface], method, SafeString(args[0], a, sizeof(a)), SafeString(args[1], b, sizeof(b)), static_cast<LONG>(args[2]), count); return;
      case 6: case 7: Log("%s.%s(key=\"%s\", value=%ld, arg=%ld) #%ld", kInterfaceNames[proxy->iface], method, SafeString(args[0], a, sizeof(a)), static_cast<LONG>(args[1]), static_cast<LONG>(args[2]), count); return;
    }
  }
  if (proxy->iface == kNetworking && (slot == 0 || slot == 13))  // SendP2PPacket(id, data, size, type, channel) / SendDataOnSocket(socket, data, size, reliable)
    { Log("%s.%s(%08lX %08lX %08lX %08lX %08lX %08lX) #%ld", kInterfaceNames[proxy->iface], method, args[0], args[1], args[2], args[3], args[4], args[5], count); return; }
  Log("%s.%s(%08lX %08lX %08lX %08lX %08lX %08lX) #%ld", kInterfaceNames[proxy->iface], method, args[0], args[1], args[2], args[3], args[4], args[5], count);
}

#define COOP_THUNK(i)                                                                                              \
  __declspec(naked) void Thunk##i() {                                                                              \
    __asm pushad                                                                                                   \
    __asm lea eax, [esp + 36]                                                                                      \
    __asm push eax                                                                                                 \
    __asm push i                                                                                                   \
    __asm push ecx                                                                                                 \
    __asm call LogSlot                                                                                             \
    __asm popad                                                                                                    \
    __asm mov ecx, [ecx + 4]                                                                                       \
    __asm mov eax, [ecx]                                                                                           \
    __asm jmp dword ptr [eax + i * 4]                                                                              \
  }
#define COOP_THUNKS10(t) COOP_THUNK(t##0) COOP_THUNK(t##1) COOP_THUNK(t##2) COOP_THUNK(t##3) COOP_THUNK(t##4) \
                         COOP_THUNK(t##5) COOP_THUNK(t##6) COOP_THUNK(t##7) COOP_THUNK(t##8) COOP_THUNK(t##9)
COOP_THUNK(0) COOP_THUNK(1) COOP_THUNK(2) COOP_THUNK(3) COOP_THUNK(4) COOP_THUNK(5) COOP_THUNK(6) COOP_THUNK(7) COOP_THUNK(8) COOP_THUNK(9)
COOP_THUNKS10(1) COOP_THUNKS10(2) COOP_THUNKS10(3) COOP_THUNKS10(4) COOP_THUNKS10(5) COOP_THUNKS10(6) COOP_THUNKS10(7)
#define COOP_REF10(t) Thunk##t##0, Thunk##t##1, Thunk##t##2, Thunk##t##3, Thunk##t##4, Thunk##t##5, Thunk##t##6, Thunk##t##7, Thunk##t##8, Thunk##t##9
void (*const kThunks[kSlots])() = {Thunk0, Thunk1, Thunk2, Thunk3, Thunk4, Thunk5, Thunk6, Thunk7, Thunk8, Thunk9,
                                   COOP_REF10(1), COOP_REF10(2), COOP_REF10(3), COOP_REF10(4), COOP_REF10(5),
                                   COOP_REF10(6), COOP_REF10(7)};
void* g_thunkTables[kInterfaceCount][kSlots];

template <typename Method>
Method RealMethod(Proxy* proxy, int slot) {
  return reinterpret_cast<Method>((*reinterpret_cast<void***>(proxy->real))[slot]);
}

unsigned long long __fastcall Mod_Matchmaking_RequestLobbyList(Proxy* proxy, void*) {
  using AddFilter_t = void(__thiscall*)(void*, const char*, const char*, int);
  using Request_t = unsigned long long(__thiscall*)(void*);
  RealMethod<AddFilter_t>(proxy, 5)(proxy->real, coop_matchmaking::kProtocolKey,
      coop_matchmaking::kProtocolValue, 0);
  Log("production matchmaking: required %s=%s", coop_matchmaking::kProtocolKey,
      coop_matchmaking::kProtocolValue);
  return RealMethod<Request_t>(proxy, 4)(proxy->real);
}

unsigned long long __fastcall Mod_Matchmaking_CreateLobby(Proxy* proxy, void*, int type, int) {
  using Method_t = unsigned long long(__thiscall*)(void*, int, int);
  const int visibleType = coop_matchmaking::VisibleProductionLobbyType(type);
  InterlockedExchange(&g_productionAcceptedClientCount, 0);
  InterlockedExchangePointer(&g_productionLocalServer, nullptr);
  InterlockedExchange64(&g_productionPendingPeer, 0);
  InterlockedExchange(&g_productionFlowSignalMembers, 2);
  SetProductionCollectionTarget(2, "new host lobby");
  Log("production matchmaking: CreateLobby requestedType=%d effectiveType=%d limit=%d", type, visibleType,
      coop_matchmaking::kMemberLimit);
  return RealMethod<Method_t>(proxy, 13)(proxy->real, visibleType, coop_matchmaking::kMemberLimit);
}

unsigned long long __fastcall Mod_Matchmaking_JoinLobby(Proxy* proxy, void*, unsigned long long lobby) {
  using GetData_t = const char*(__thiscall*)(void*, unsigned long long, const char*);
  using Join_t = unsigned long long(__thiscall*)(void*, unsigned long long);
  const char* protocol = RealMethod<GetData_t>(proxy, 19)(proxy->real, lobby, coop_matchmaking::kProtocolKey);
  if (!coop_matchmaking::IsCompatible(protocol)) {
    Log("production matchmaking: rejected incompatible lobby=%08lX%08lX protocol='%s'",
        static_cast<DWORD>(lobby >> 32), static_cast<DWORD>(lobby), protocol ? protocol : "<null>");
    return 0;
  }
  Log("production matchmaking: accepted compatible lobby=%08lX%08lX",
      static_cast<DWORD>(lobby >> 32), static_cast<DWORD>(lobby));
  InterlockedExchange64(&g_productionPendingPeer, 0);
  InterlockedExchange(&g_productionFlowSignalMembers, 2);
  InterlockedExchange64(&g_productionLobby, static_cast<LONG64>(lobby));
  return RealMethod<Join_t>(proxy, 14)(proxy->real, lobby);
}

const char* __fastcall Mod_Friends_GetFriendPersonaName(Proxy* proxy, void*, unsigned long long peer) {
  using Method_t = const char*(__thiscall*)(void*, unsigned long long);
  const char* name = RealMethod<Method_t>(proxy, 7)(proxy->real, peer);
  const unsigned long long pending = static_cast<unsigned long long>(
      InterlockedCompareExchange64(&g_productionPendingPeer, 0, 0));
  Log("production identity: persona peer=%08lX%08lX name='%s' pendingPeer=%08lX%08lX acceptedClients=%ld",
      static_cast<DWORD>(peer >> 32), static_cast<DWORD>(peer), name ? name : "<null>",
      static_cast<DWORD>(pending >> 32), static_cast<DWORD>(pending),
      InterlockedCompareExchange(&g_productionAcceptedClientCount, 0, 0));
  return name;
}

bool __fastcall Mod_Matchmaking_SetLobbyData(Proxy* proxy, void*, unsigned long long lobby,
                                              const char* key, const char* value) {
  using Method_t = bool(__thiscall*)(void*, unsigned long long, const char*, const char*);
  Method_t set = RealMethod<Method_t>(proxy, 20);
  const bool protocolKey = key && _stricmp(key, coop_matchmaking::kProtocolKey) == 0;
  const bool original = set(proxy->real, lobby, protocolKey ? coop_matchmaking::kProtocolKey : key,
                            protocolKey ? coop_matchmaking::kProtocolValue : value);
  const bool tagged = protocolKey ? original : set(proxy->real, lobby, coop_matchmaking::kProtocolKey,
                                                    coop_matchmaking::kProtocolValue);
  if (tagged) InterlockedExchange64(&g_productionLobby, static_cast<LONG64>(lobby));
  Log("production matchmaking: tagged lobby=%08lX%08lX %s=%s result=%d",
      static_cast<DWORD>(lobby >> 32), static_cast<DWORD>(lobby), coop_matchmaking::kProtocolKey,
      coop_matchmaking::kProtocolValue, tagged);
  return original && tagged;
}

bool __fastcall Mod_Matchmaking_SetLobbyMemberLimit(Proxy* proxy, void*, unsigned long long lobby, int) {
  using Method_t = bool(__thiscall*)(void*, unsigned long long, int);
  return RealMethod<Method_t>(proxy, 31)(proxy->real, lobby, coop_matchmaking::kMemberLimit);
}

bool __fastcall Mod_Matchmaking_SetLobbyType(Proxy* proxy, void*, unsigned long long lobby, int type) {
  using Method_t = bool(__thiscall*)(void*, unsigned long long, int);
  const int visibleType = coop_matchmaking::VisibleProductionLobbyType(type);
  Log("production matchmaking: SetLobbyType lobby=%08lX%08lX requestedType=%d effectiveType=%d",
      static_cast<DWORD>(lobby >> 32), static_cast<DWORD>(lobby), type, visibleType);
  return RealMethod<Method_t>(proxy, 33)(proxy->real, lobby, visibleType);
}

bool __fastcall Mod_Apps_BIsDlcInstalled(Proxy* proxy, void*, unsigned int appId) {
  using Method_t = bool(__thiscall*)(void*, unsigned int);
  const bool installed = RealMethod<Method_t>(proxy, coop_matchmaking::kAppsBIsDlcInstalledSlot)(
      proxy->real, appId);
  if (!g_baseGameDlcCompatibility || !coop_matchmaking::IsDr2OptionalSkillPack(appId)) return installed;
  Log("production DLC compatibility: app=%u SteamInstalled=%d effectiveInstalled=0", appId, installed);
  return false;
}

bool __fastcall Mod_Apps_BGetDLCDataByIndex(Proxy* proxy, void*, int index, unsigned int* appId,
                                             bool* available, char* name, int nameBytes) {
  using Method_t = bool(__thiscall*)(void*, int, unsigned int*, bool*, char*, int);
  const bool result = RealMethod<Method_t>(proxy, coop_matchmaking::kAppsBGetDlcDataByIndexSlot)(
      proxy->real, index, appId, available, name, nameBytes);
  if (!result || !g_baseGameDlcCompatibility || !appId || !available ||
      !coop_matchmaking::IsDr2OptionalSkillPack(*appId)) return result;
  const bool steamAvailable = *available;
  *available = false;
  Log("production DLC compatibility: index=%d app=%u SteamAvailable=%d effectiveAvailable=0",
      index, *appId, steamAvailable);
  return true;
}

constexpr unsigned long long kLocalSteamIdPrefix = 0x0110000100000000ULL;
constexpr unsigned int kLocalAccountBase = 0x70000000;
const char* const kLocalPlayerNames[] = {"Player 1", "Player 2", "Player 3", "Player 4"};

constexpr DWORD kBusMagic = 0x34503244;  // "D2P4"
constexpr int kBusPacketsPerPlayer = 8192;
constexpr DWORD kBusPacketBytes = 2048;
constexpr int kGameP2PChannel = 5679;
constexpr int kBusTestChannel = 0x7FFFFF40;
struct LocalBusSlot {
  DWORD pid;
  unsigned long long steamId;
  volatile LONG heartbeat;
  volatile LONG directProbeReady;
  char name[16];
};
struct LocalBusPacket {
  DWORD occupied;
  unsigned long long sequence;
  unsigned long long source;
  DWORD size;
  int channel;
  BYTE data[kBusPacketBytes];
};
struct LocalLobbyData {
  char key[32];
  char value[128];
};
struct LocalBus {
  DWORD magic;
  DWORD version;
  unsigned long long nextSequence;
  DWORD lobbyActive;
  DWORD lobbyOwner;
  DWORD lobbyMemberMask;
  DWORD lobbyLimit;
  LocalLobbyData lobbyData[16];
  LocalBusSlot slots[4];
  DWORD p2pRequestMask[4];
  LocalBusPacket packets[4][kBusPacketsPerPlayer];
};
HANDLE g_busMapping = nullptr;
HANDLE g_busMutex = nullptr;
LocalBus* g_bus = nullptr;
volatile LONG g_busSendCalls = 0;
volatile LONG g_busAvailableCalls = 0;
volatile LONG g_busReadCalls = 0;

unsigned long long LocalSteamId(int instance) {
  return kLocalSteamIdPrefix | static_cast<unsigned long long>(kLocalAccountBase + instance);
}
int LocalInstanceFromSteamId(unsigned long long steamId) {
  if ((steamId & 0xFFFFFFFF00000000ULL) != kLocalSteamIdPrefix) return -1;
  const unsigned int account = static_cast<unsigned int>(steamId);
  return account >= kLocalAccountBase && account < kLocalAccountBase + 4 ? static_cast<int>(account - kLocalAccountBase) : -1;
}

bool ProcessAlive(DWORD pid) {
  if (!pid) return false;
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!process) return false;
  const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
  CloseHandle(process);
  return alive;
}

int LiveBusInstanceCount() {
  if (!g_bus || !g_busMutex) return 0;
  int count = 0;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    for (int index = 0; index < 4; index++) {
      const DWORD pid = g_bus->slots[index].pid;
      if (pid && ProcessAlive(pid)) count++;
    }
    ReleaseMutex(g_busMutex);
  }
  return count;
}

int ReadyBusInstanceCount() {
  if (!g_bus || !g_busMutex) return 0;
  int count = 0;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    for (int index = 0; index < 4; index++) {
      const LocalBusSlot& slot = g_bus->slots[index];
      if (slot.pid && slot.directProbeReady && ProcessAlive(slot.pid)) count++;
    }
    ReleaseMutex(g_busMutex);
  }
  return count;
}

void MarkDirectProbeReady() {
  if (!g_bus || !g_busMutex) return;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    g_bus->slots[g_instance].directProbeReady = 1;
    ReleaseMutex(g_busMutex);
  }
}

bool PriorHarnessPeersConfirmed() {
  if (!g_bus || !g_busMutex) return false;
  if (WaitForSingleObject(g_busMutex, 1000) != WAIT_OBJECT_0) return false;
  bool ready = true;
  for (int index = 0; index < g_instance; index++) {
    const auto& slot = g_bus->slots[index];
    if (slot.directProbeReady != 2 || !ProcessAlive(slot.pid)) ready = false;
  }
  ReleaseMutex(g_busMutex);
  return ready;
}

bool AllHarnessPeersConfirmed() {
  if (!g_bus || !g_busMutex) return false;
  if (WaitForSingleObject(g_busMutex, 1000) != WAIT_OBJECT_0) return false;
  bool ready = true;
  for (int index = 0; index < g_testInstances; index++) {
    const auto& slot = g_bus->slots[index];
    if (slot.directProbeReady != 2 || !ProcessAlive(slot.pid)) ready = false;
  }
  ReleaseMutex(g_busMutex);
  return ready;
}

void MarkNativePeerConfirmed(unsigned long long peer) {
  const int instance = LocalInstanceFromSteamId(peer);
  if (!g_sequentialJoins || !g_bus || !g_busMutex || instance < 0) return;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    g_bus->slots[instance].directProbeReady = 2;
    ReleaseMutex(g_busMutex);
  }
}

bool BusSend(unsigned long long targetId, const void* data, unsigned int size, int channel) {
  const int target = LocalInstanceFromSteamId(targetId);
  if (!g_bus || target < 0 || !data || size > kBusPacketBytes) return false;
  bool sent = false;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    for (auto& packet : g_bus->packets[target]) {
      if (packet.occupied) continue;
      packet.sequence = ++g_bus->nextSequence;
      packet.source = LocalSteamId(g_instance);
      packet.size = size;
      packet.channel = channel;
      memcpy(packet.data, data, size);
      packet.occupied = 1;
      if (channel == kGameP2PChannel) g_bus->p2pRequestMask[target] |= 1u << g_instance;
      sent = true;
      break;
    }
    ReleaseMutex(g_busMutex);
  }
  return sent;
}

LocalBusPacket* OldestPacket(int channel) {
  LocalBusPacket* oldest = nullptr;
  for (auto& packet : g_bus->packets[g_instance]) {
    if (!packet.occupied || packet.channel != channel) continue;
    if (!oldest || packet.sequence < oldest->sequence) oldest = &packet;
  }
  return oldest;
}

bool BusAvailable(unsigned int* size, int channel) {
  if (!g_bus) return false;
  bool available = false;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    LocalBusPacket* packet = OldestPacket(channel);
    if (packet) {
      if (size) *size = packet->size;
      available = true;
    }
    ReleaseMutex(g_busMutex);
  }
  return available;
}

bool BusPeekSource(int channel, unsigned long long* source) {
  if (!g_bus || !source) return false;
  bool available = false;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    LocalBusPacket* packet = OldestPacket(channel);
    if (packet) {
      *source = packet->source;
      available = true;
    }
    ReleaseMutex(g_busMutex);
  }
  return available;
}

DWORD BusSourceMask(int channel) {
  if (!g_bus) return 0;
  DWORD mask = 0;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    if (channel == kGameP2PChannel) mask = g_bus->p2pRequestMask[g_instance];
    for (const auto& packet : g_bus->packets[g_instance]) {
      if (!packet.occupied || packet.channel != channel) continue;
      const int sourceInstance = LocalInstanceFromSteamId(packet.source);
      if (sourceInstance >= 0 && sourceInstance != g_instance) mask |= 1u << sourceInstance;
    }
    ReleaseMutex(g_busMutex);
  }
  return mask;
}

bool BusRead(void* data, unsigned int capacity, unsigned int* size, unsigned long long* source, int channel) {
  if (!g_bus || !data) return false;
  bool read = false;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    LocalBusPacket* packet = OldestPacket(channel);
    if (packet && packet->size <= capacity) {
      memcpy(data, packet->data, packet->size);
      if (size) *size = packet->size;
      if (source) *source = packet->source;
      memset(packet, 0, sizeof(*packet));
      read = true;
    }
    ReleaseMutex(g_busMutex);
  }
  return read;
}

DWORD WINAPI LocalBusThread(LPVOID) {
  int previousPeers = -1;
  bool testSent = false;
  bool testReceived = false;
  for (;;) {
    if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
      if (g_bus->magic != kBusMagic || g_bus->version != 6) {
        memset(g_bus, 0, sizeof(*g_bus));
        g_bus->magic = kBusMagic;
        g_bus->version = 6;
        g_bus->lobbyLimit = 4;
      }
      for (auto& slot : g_bus->slots)
        if (slot.pid && !ProcessAlive(slot.pid)) memset(&slot, 0, sizeof(slot));
      auto& self = g_bus->slots[g_instance];
      self.pid = GetCurrentProcessId();
      self.steamId = LocalSteamId(g_instance);
      InterlockedIncrement(&self.heartbeat);
      strcpy_s(self.name, kLocalPlayerNames[g_instance]);
      int peers = 0;
      for (int i = 0; i < 4; i++)
        if (i != g_instance && g_bus->slots[i].pid && ProcessAlive(g_bus->slots[i].pid)) peers++;
      ReleaseMutex(g_busMutex);
      if (peers != previousPeers) {
        Log("loopback bus: %d peer%s online", peers, peers == 1 ? "" : "s");
        previousPeers = peers;
      }
    }
    if (previousPeers == 3 && !testSent) {
      const int target = (g_instance + 1) % 4;
      const DWORD payload = static_cast<DWORD>(g_instance);
      testSent = BusSend(LocalSteamId(target), &payload, sizeof(payload), kBusTestChannel);
      if (testSent) Log("loopback packet test: sent to Player %d", target + 1);
    }
    if (!testReceived) {
      DWORD payload = 0;
      unsigned int size = 0;
      unsigned long long source = 0;
      if (BusRead(&payload, sizeof(payload), &size, &source, kBusTestChannel)) {
        testReceived = size == sizeof(payload);
        Log("loopback packet test: received from Player %lu (%s)", payload + 1, testReceived ? "valid" : "invalid");
      }
    }
    Sleep(500);
  }
}

void StartLocalBus() {
  if (!g_harness) return;
  g_busMutex = CreateMutexW(nullptr, FALSE, L"Local\\DR2FourPlayerBusMutex_v1");
  g_busMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LocalBus),
                                   L"Local\\DR2FourPlayerBus_v1");
  if (!g_busMutex || !g_busMapping) {
    Log("loopback bus: initialization failed error=%lu", GetLastError());
    return;
  }
  g_bus = static_cast<LocalBus*>(MapViewOfFile(g_busMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LocalBus)));
  if (!g_bus) {
    Log("loopback bus: mapping failed error=%lu", GetLastError());
    return;
  }
  HANDLE thread = CreateThread(nullptr, 0, LocalBusThread, nullptr, 0, nullptr);
  if (thread) CloseHandle(thread);
}

BOOL __fastcall Fake_User_BLoggedOn(Proxy*, void*) { return TRUE; }
void* __fastcall Fake_User_GetSteamID(Proxy*, void*, unsigned long long* output) {
  const unsigned long long id = LocalSteamId(g_instance);
  *output = id;
  Log("identity: GetSteamID -> %08lX%08lX (%s)", static_cast<DWORD>(id >> 32), static_cast<DWORD>(id), kLocalPlayerNames[g_instance]);
  return output;
}
const char* __fastcall Fake_Friends_GetPersonaName(Proxy*, void*) { return kLocalPlayerNames[g_instance]; }
int __fastcall Fake_Friends_GetFriendCount(Proxy*, void*, int) { return 3; }
void* __fastcall Fake_Friends_GetFriendByIndex(Proxy*, void*, unsigned long long* output, int index, int) {
  unsigned long long id = 0;
  if (index >= 0 && index < 3) {
    const int other = index >= g_instance ? index + 1 : index;
    id = LocalSteamId(other);
  }
  *output = id;
  Log("identity: GetFriendByIndex(%d) -> %08lX%08lX", index, static_cast<DWORD>(id >> 32), static_cast<DWORD>(id));
  return output;
}
int __fastcall Fake_Friends_GetFriendRelationship(Proxy*, void*, unsigned long long steamId) {
  const int instance = LocalInstanceFromSteamId(steamId);
  return instance >= 0 && instance != g_instance ? 3 : 0;  // k_EFriendRelationshipFriend / None
}
int __fastcall Fake_Friends_GetFriendPersonaState(Proxy*, void*, unsigned long long steamId) {
  return LocalInstanceFromSteamId(steamId) >= 0 ? 1 : 0;  // k_EPersonaStateOnline / Offline
}
const char* __fastcall Fake_Friends_GetFriendPersonaName(Proxy*, void*, unsigned long long steamId) {
  const int instance = LocalInstanceFromSteamId(steamId);
  return instance >= 0 ? kLocalPlayerNames[instance] : "Local Player";
}

void LogMeshHandshakeHeader(bool receive, unsigned long long peer, const void* data, unsigned int size, int channel) {
  if (!g_meshListenerProbe || !data || size != 22 || channel != 5679) return;
  const int instance = LocalInstanceFromSteamId(peer);
  if (instance < 0) return;
  static volatile LONG counts[2][4]{};
  if (InterlockedIncrement(&counts[receive ? 1 : 0][instance]) > 4) return;
  const BYTE* bytes = static_cast<const BYTE*>(data);
  char hex[22 * 2 + 1]{};
  for (unsigned int index = 0; index < 22; index++)
    sprintf_s(hex + index * 2, sizeof(hex) - index * 2, "%02X", bytes[index]);
  Log("mesh handshake: direction=%s peer=%08lX%08lX channel=%d bytes=%u header=%s",
      receive ? "receive" : "send", static_cast<DWORD>(peer >> 32), static_cast<DWORD>(peer), channel, size, hex);
}

BOOL __fastcall Fake_Networking_SendP2PPacket(Proxy*, void*, unsigned long long remote, const void* data,
                                               unsigned int size, int sendType, int channel) {
  const bool sent = BusSend(remote, data, size, channel);
  if (sent) LogMeshHandshakeHeader(false, remote, data, size, channel);
  const LONG call = InterlockedIncrement(&g_busSendCalls);
  if (!sent || size != 14 || call <= 8 || call % 1024 == 0) {
    Log("synthetic P2P: SendP2PPacket call=%ld peer=%08lX%08lX bytes=%u type=%d channel=%d result=%d",
        call, static_cast<DWORD>(remote >> 32), static_cast<DWORD>(remote), size, sendType, channel, sent);
  }
  return sent ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_IsP2PPacketAvailable(Proxy*, void*, unsigned int* size, int channel) {
  const bool available = BusAvailable(size, channel);
  const LONG call = InterlockedIncrement(&g_busAvailableCalls);
  if (available && ((size && *size != 14) || call <= 8 || call % 1024 == 0)) {
    Log("synthetic P2P: IsP2PPacketAvailable call=%ld bytes=%u channel=%d",
        call, size ? *size : 0, channel);
  }
  return available ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_ReadP2PPacket(Proxy*, void*, void* data, unsigned int capacity, unsigned int* size,
                                              unsigned long long* remote, int channel) {
  const bool read = BusRead(data, capacity, size, remote, channel);
  if (read && size && remote) LogMeshHandshakeHeader(true, *remote, data, *size, channel);
  const LONG call = InterlockedIncrement(&g_busReadCalls);
  if (read && ((size && *size != 14) || call <= 8 || call % 1024 == 0)) {
    const unsigned long long source = remote ? *remote : 0;
    Log("synthetic P2P: ReadP2PPacket call=%ld peer=%08lX%08lX bytes=%u channel=%d",
        call, static_cast<DWORD>(source >> 32), static_cast<DWORD>(source), size ? *size : 0, channel);
  }
  return read ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_AcceptP2PSessionWithUser(Proxy*, void*, unsigned long long remote) {
  const bool accepted = LocalInstanceFromSteamId(remote) >= 0;
  Log("synthetic P2P: AcceptP2PSessionWithUser peer=%08lX%08lX result=%d",
      static_cast<DWORD>(remote >> 32), static_cast<DWORD>(remote), accepted);
  return accepted ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_CloseP2PSessionWithUser(Proxy*, void*, unsigned long long remote) {
  return LocalInstanceFromSteamId(remote) >= 0 ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_CloseP2PChannelWithUser(Proxy*, void*, unsigned long long remote, int) {
  return LocalInstanceFromSteamId(remote) >= 0 ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_GetP2PSessionState(Proxy*, void*, unsigned long long remote, void* state) {
  if (LocalInstanceFromSteamId(remote) < 0 || !state) return FALSE;
  memset(state, 0, 20);
  static_cast<BYTE*>(state)[0] = 1;  // m_bConnectionActive
  return TRUE;
}
BOOL __fastcall Fake_Networking_AllowP2PPacketRelay(Proxy*, void*, BOOL) { return TRUE; }

constexpr unsigned int kLocalListenSocketBase = 0xD2400000;
constexpr unsigned int kLocalPeerSocketBase = 0xD2500000;

unsigned int __fastcall Fake_Networking_CreateListenSocket(Proxy*, void*, int virtualPort, unsigned int ip,
                                                            unsigned int port, BOOL relay) {
  const unsigned int handle = kLocalListenSocketBase | static_cast<unsigned int>(g_instance + 1);
  Log("legacy socket: listen handle=%08X virtualPort=%d ip=%08X port=%u relay=%d", handle, virtualPort, ip, port, relay);
  return handle;
}
unsigned int __fastcall Fake_Networking_CreateP2PConnectionSocket(Proxy*, void*, unsigned long long remote,
                                                                  int virtualPort, int timeoutSeconds, BOOL relay) {
  const int target = LocalInstanceFromSteamId(remote);
  if (target < 0) return 0;
  const unsigned int handle = kLocalPeerSocketBase | static_cast<unsigned int>(target + 1);
  Log("legacy socket: connect handle=%08X peer=%08lX%08lX virtualPort=%d timeout=%d relay=%d", handle,
      static_cast<DWORD>(remote >> 32), static_cast<DWORD>(remote), virtualPort, timeoutSeconds, relay);
  return handle;
}
unsigned int __fastcall Fake_Networking_CreateConnectionSocket(Proxy*, void*, unsigned int ip,
                                                                unsigned int port, int timeoutSeconds) {
  Log("legacy socket: direct IP connection rejected ip=%08X port=%u timeout=%d", ip, port, timeoutSeconds);
  return 0;
}
BOOL __fastcall Fake_Networking_DestroySocket(Proxy*, void*, unsigned int socket, BOOL notify) {
  Log("legacy socket: destroy socket=%08X notify=%d", socket, notify);
  return (socket & 0xFFFF0000) == kLocalPeerSocketBase ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_DestroyListenSocket(Proxy*, void*, unsigned int socket, BOOL notify) {
  Log("legacy socket: destroy listen=%08X notify=%d", socket, notify);
  return (socket & 0xFFFF0000) == kLocalListenSocketBase ? TRUE : FALSE;
}
BOOL __fastcall Fake_Networking_SendDataOnSocket(Proxy*, void*, unsigned int socket, const void*, unsigned int size,
                                                  BOOL reliable) {
  Log("legacy socket: send pending routing socket=%08X bytes=%u reliable=%d", socket, size, reliable);
  return FALSE;
}
BOOL __fastcall Fake_Networking_IsDataAvailableOnSocket(Proxy*, void*, unsigned int, unsigned int* size) {
  if (size) *size = 0;
  return FALSE;
}
BOOL __fastcall Fake_Networking_RetrieveDataFromSocket(Proxy*, void*, unsigned int, void*, unsigned int,
                                                        unsigned int* size) {
  if (size) *size = 0;
  return FALSE;
}
BOOL __fastcall Fake_Networking_IsDataAvailable(Proxy*, void*, unsigned int, unsigned int* size,
                                                 unsigned int* socket) {
  if (size) *size = 0;
  if (socket) *socket = 0;
  return FALSE;
}
BOOL __fastcall Fake_Networking_RetrieveData(Proxy*, void*, unsigned int, void*, unsigned int,
                                              unsigned int* size, unsigned int* socket) {
  if (size) *size = 0;
  if (socket) *socket = 0;
  return FALSE;
}
BOOL __fastcall Fake_Networking_GetSocketInfo(Proxy*, void*, unsigned int socket, unsigned long long* remote,
                                               int* status, unsigned int* ip, unsigned short* port) {
  if ((socket & 0xFFFF0000) != kLocalPeerSocketBase) return FALSE;
  const int target = static_cast<int>((socket & 0xFFFF) - 1);
  if (remote) *remote = LocalSteamId(target);
  if (status) *status = 1;  // k_ESNetSocketStateConnected
  if (ip) *ip = 0;
  if (port) *port = 0;
  return TRUE;
}
BOOL __fastcall Fake_Networking_GetListenSocketInfo(Proxy*, void*, unsigned int socket, unsigned int* ip,
                                                     unsigned short* port) {
  if (ip) *ip = 0;
  if (port) *port = 0;
  return (socket & 0xFFFF0000) == kLocalListenSocketBase ? TRUE : FALSE;
}
int __fastcall Fake_Networking_GetSocketConnectionType(Proxy*, void*, unsigned int socket) {
  return (socket & 0xFFFF0000) == kLocalPeerSocketBase ? 1 : 0;  // UDP / not connected
}
int __fastcall Fake_Networking_GetMaxPacketSize(Proxy*, void*, unsigned int socket) {
  return (socket & 0xFFFF0000) == kLocalPeerSocketBase ? 1200 : 0;
}

constexpr unsigned long long kLocalLobbyId = 0x0184000070000001ULL;
constexpr unsigned long long kFakeCallPrefix = 0xC04F000000000000ULL;
enum class FakeCallType { None, LobbyList, LobbyCreated, LobbyEnter };
#pragma pack(push, 8)
struct LobbyMatchListResult { DWORD count; };
struct LobbyCreatedResult { DWORD result; DWORD padding; unsigned long long lobby; };
struct LobbyEnterResult {
  unsigned long long lobby;
  DWORD permissions;
  BYTE locked;
  BYTE padding[3];
  DWORD response;
  DWORD tailPadding;
};
#pragma pack(pop)
struct PendingCall {
  unsigned long long handle;
  FakeCallType type;
  BYTE payload[24];
  void* callback;
};
PendingCall g_pendingCalls[8];
volatile LONG g_nextFakeCall = 0;
char g_lobbyDataResult[128];

unsigned long long QueueCall(FakeCallType type, const void* payload, size_t size) {
  const unsigned long long handle = kFakeCallPrefix | static_cast<unsigned long long>(InterlockedIncrement(&g_nextFakeCall));
  EnterCriticalSection(&g_lock);
  for (auto& pending : g_pendingCalls) {
    if (pending.handle) continue;
    pending.handle = handle;
    pending.type = type;
    memcpy(pending.payload, payload, size);
    pending.callback = nullptr;
    break;
  }
  LeaveCriticalSection(&g_lock);
  return handle;
}
bool IsFakeCall(unsigned long long handle) { return (handle & 0xFFFF000000000000ULL) == kFakeCallPrefix; }

DWORD LiveLobbyMask() {
  if (!g_bus) return 0;
  DWORD mask = 0;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    for (int i = 0; i < 4; i++)
      if ((g_bus->lobbyMemberMask & (1u << i)) && ProcessAlive(g_bus->slots[i].pid)) mask |= 1u << i;
    ReleaseMutex(g_busMutex);
  }
  return mask;
}
int MaskMemberAt(DWORD mask, int index) {
  for (int i = 0; i < 4; i++) {
    if (!(mask & (1u << i))) continue;
    if (index-- == 0) return i;
  }
  return -1;
}

unsigned long long __fastcall Fake_Matchmaking_RequestLobbyList(Proxy*, void*) {
  const LobbyMatchListResult result = {g_bus && g_bus->lobbyActive ? 1u : 0u};
  static volatile LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  if (call <= 5 || call % 100 == 0)
    Log("loopback lobby: RequestLobbyList call=%ld active=%ld owner=%ld members=%08lX resultCount=%lu",
        call, g_bus ? g_bus->lobbyActive : 0, g_bus ? g_bus->lobbyOwner : -1,
        g_bus ? g_bus->lobbyMemberMask : 0, result.count);
  return QueueCall(FakeCallType::LobbyList, &result, sizeof(result));
}
void __fastcall Fake_Matchmaking_AddStringFilter(Proxy*, void*, const char* key, const char* value, int comparison) {
  Log("loopback lobby: string filter key='%s' value='%s' comparison=%d", key ? key : "<null>",
      value ? value : "<null>", comparison);
}
void __fastcall Fake_Matchmaking_AddNumericalFilter(Proxy*, void*, const char* key, int value, int comparison) {
  Log("loopback lobby: numerical filter key='%s' value=%d comparison=%d", key ? key : "<null>", value, comparison);
}
void __fastcall Fake_Matchmaking_AddNearValueFilter(Proxy*, void*, const char* key, int value) {
  Log("loopback lobby: near-value filter key='%s' value=%d", key ? key : "<null>", value);
}
void __fastcall Fake_Matchmaking_AddSlotsAvailableFilter(Proxy*, void*, int slots) {
  Log("loopback lobby: slots-available filter slots=%d", slots);
}
void __fastcall Fake_Matchmaking_AddDistanceFilter(Proxy*, void*, int distance) {
  Log("loopback lobby: distance filter distance=%d", distance);
}
void __fastcall Fake_Matchmaking_AddResultCountFilter(Proxy*, void*, int count) {
  Log("loopback lobby: result-count filter count=%d", count);
}
void __fastcall Fake_Matchmaking_AddCompatibleMembersFilter(Proxy*, void*, unsigned long long steamId) {
  Log("loopback lobby: compatible-members filter steamId=%08lX%08lX", static_cast<DWORD>(steamId >> 32),
      static_cast<DWORD>(steamId));
}
void* __fastcall Fake_Matchmaking_GetLobbyByIndex(Proxy*, void*, unsigned long long* output, int index) {
  *output = index == 0 && g_bus && g_bus->lobbyActive ? kLocalLobbyId : 0;
  static volatile LONG calls = 0;
  const LONG call = InterlockedIncrement(&calls);
  if (call <= 5 || call % 100 == 0)
    Log("loopback lobby: GetLobbyByIndex call=%ld index=%d -> %08lX%08lX", call, index,
        static_cast<DWORD>(*output >> 32), static_cast<DWORD>(*output));
  return output;
}
unsigned long long __fastcall Fake_Matchmaking_CreateLobby(Proxy*, void*, int, int maxMembers) {
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    g_bus->lobbyActive = 1;
    g_bus->lobbyOwner = g_instance;
    g_bus->lobbyMemberMask = 1u << g_instance;
    g_bus->lobbyLimit = maxMembers > 0 && maxMembers <= 4 ? maxMembers : 4;
    ReleaseMutex(g_busMutex);
  }
  const LobbyCreatedResult result = {1, 0, kLocalLobbyId};
  Log("loopback lobby: Player %d created lobby with limit %lu", g_instance + 1, g_bus ? g_bus->lobbyLimit : 4);
  return QueueCall(FakeCallType::LobbyCreated, &result, sizeof(result));
}
unsigned long long __fastcall Fake_Matchmaking_JoinLobby(Proxy*, void*, unsigned long long lobby) {
  // Frontend clients bypass DirectHostProbeThread and its connector hook. Set their
  // native capacity before delivering LobbyEnter, which can immediately call Connect.
  if (lobby == kLocalLobbyId && g_lobbyFrontendProbe && g_instance > 0 &&
      !InstallCampaignAdmissionCapacity(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)))) {
    Log("lobby frontend: campaign capacity policy unavailable; join cancelled");
    const LobbyEnterResult failed = {lobby, 0, 0, {0, 0, 0}, 2, 0};
    return QueueCall(FakeCallType::LobbyEnter, &failed, sizeof(failed));
  }
  if (lobby != kLocalLobbyId || !g_bus || !g_busMutex || !StartLobbyFrontendObserver() ||
      WaitForSingleObject(g_busMutex, 1000) != WAIT_OBJECT_0) {
    Log("loopback lobby: Player %d join failed; lobby/observer/membership unavailable", g_instance + 1);
    const LobbyEnterResult failed = {lobby, 0, 0, {0, 0, 0}, 2, 0};
    return QueueCall(FakeCallType::LobbyEnter, &failed, sizeof(failed));
  }
  g_bus->lobbyMemberMask |= 1u << g_instance;
  ReleaseMutex(g_busMutex);
  const LobbyEnterResult result = {lobby, 0xFFFFFFFFu, 0, {0, 0, 0}, 1u, 0};
  Log("loopback lobby: Player %d joined lobby", g_instance + 1);
  return QueueCall(FakeCallType::LobbyEnter, &result, sizeof(result));
}
void __fastcall Fake_Matchmaking_LeaveLobby(Proxy*, void*, unsigned long long lobby) {
  if (lobby == kLocalLobbyId && WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) {
    g_bus->lobbyMemberMask &= ~(1u << g_instance);
    if (!g_bus->lobbyMemberMask) g_bus->lobbyActive = 0;
    ReleaseMutex(g_busMutex);
  }
}
BOOL __fastcall Fake_Matchmaking_InviteUserToLobby(Proxy*, void*, unsigned long long lobby, unsigned long long user) {
  return lobby == kLocalLobbyId && LocalInstanceFromSteamId(user) >= 0;
}
int __fastcall Fake_Matchmaking_GetNumLobbyMembers(Proxy*, void*, unsigned long long lobby) {
  if (lobby != kLocalLobbyId) return 0;
  DWORD mask = LiveLobbyMask();
  int count = 0;
  for (; mask; mask &= mask - 1) count++;
  return count;
}
void* __fastcall Fake_Matchmaking_GetLobbyMemberByIndex(Proxy*, void*, unsigned long long* output,
                                                         unsigned long long lobby, int index) {
  const int member = lobby == kLocalLobbyId ? MaskMemberAt(LiveLobbyMask(), index) : -1;
  *output = member >= 0 ? LocalSteamId(member) : 0;
  return output;
}
const char* __fastcall Fake_Matchmaking_GetLobbyData(Proxy*, void*, unsigned long long lobby, const char* key) {
  g_lobbyDataResult[0] = 0;
  if (lobby != kLocalLobbyId || !key || WaitForSingleObject(g_busMutex, 1000) != WAIT_OBJECT_0) return g_lobbyDataResult;
  for (const auto& entry : g_bus->lobbyData)
    if (entry.key[0] && _stricmp(entry.key, key) == 0) { strcpy_s(g_lobbyDataResult, entry.value); break; }
  ReleaseMutex(g_busMutex);
  static char observedKeys[16][64]{};
  bool observed = false;
  for (const auto& observedKey : observedKeys)
    if (observedKey[0] && _stricmp(observedKey, key) == 0) { observed = true; break; }
  if (!observed) {
    for (auto& observedKey : observedKeys) {
      if (observedKey[0]) continue;
      strncpy_s(observedKey, key, _TRUNCATE);
      Log("loopback lobby: GetLobbyData first key='%s' value='%s'", key, g_lobbyDataResult);
      break;
    }
  }
  return g_lobbyDataResult;
}
BOOL __fastcall Fake_Matchmaking_SetLobbyData(Proxy*, void*, unsigned long long lobby, const char* key, const char* value) {
  if (lobby != kLocalLobbyId || !key || !value || WaitForSingleObject(g_busMutex, 1000) != WAIT_OBJECT_0) return FALSE;
  LocalLobbyData* target = nullptr;
  for (auto& entry : g_bus->lobbyData) {
    if (entry.key[0] && _stricmp(entry.key, key) == 0) { target = &entry; break; }
    if (!target && !entry.key[0]) target = &entry;
  }
  if (target) { strcpy_s(target->key, key); strcpy_s(target->value, value); }
  ReleaseMutex(g_busMutex);
  return target ? TRUE : FALSE;
}
BOOL __fastcall Fake_Matchmaking_SetLobbyMemberLimit(Proxy*, void*, unsigned long long lobby, int limit) {
  if (lobby != kLocalLobbyId || limit < 1 || limit > 4) return FALSE;
  if (WaitForSingleObject(g_busMutex, 1000) == WAIT_OBJECT_0) { g_bus->lobbyLimit = limit; ReleaseMutex(g_busMutex); }
  return TRUE;
}
int __fastcall Fake_Matchmaking_GetLobbyMemberLimit(Proxy*, void*, unsigned long long lobby) {
  return lobby == kLocalLobbyId && g_bus ? static_cast<int>(g_bus->lobbyLimit) : 0;
}
void* __fastcall Fake_Matchmaking_GetLobbyOwner(Proxy*, void*, unsigned long long* output, unsigned long long lobby) {
  *output = lobby == kLocalLobbyId && g_bus ? LocalSteamId(g_bus->lobbyOwner) : 0;
  return output;
}

// One proxy per real interface pointer (DR2 asks for each interface many times).
Proxy g_proxies[16];
int g_proxyCount = 0;

void* Wrap(void* real, int iface) {
  if (!real) return real;
  if (g_production && iface == kMatchmaking) InterlockedExchangePointer(&g_productionMatchmaking, real);
  if (g_production && iface == kFriends) InterlockedExchangePointer(&g_productionFriends, real);
  if (!g_trace && !g_harness &&
      !(g_production && (iface == kMatchmaking || (iface == kApps && g_baseGameDlcCompatibility)))) return real;
  EnterCriticalSection(&g_lock);
  for (int i = 0; i < g_proxyCount; i++) {
    if (g_proxies[i].real == real) {
      LeaveCriticalSection(&g_lock);
      return &g_proxies[i];
    }
  }
  void* result = real;
  if (g_proxyCount < _countof(g_proxies)) {
    g_proxies[g_proxyCount] = {g_thunkTables[iface], real, iface};
    result = &g_proxies[g_proxyCount++];
    Log("proxying %s interface %p", kInterfaceNames[iface], real);
  }
  LeaveCriticalSection(&g_lock);
  return result;
}

using Accessor_t = void*(__cdecl*)();
Accessor_t Real_Accessor[kInterfaceCount];
void* __cdecl Hook_SteamMatchmaking() { return Wrap(Real_Accessor[kMatchmaking](), kMatchmaking); }
void* __cdecl Hook_SteamNetworking() { return Wrap(Real_Accessor[kNetworking](), kNetworking); }
void* __cdecl Hook_SteamUser() { return Wrap(Real_Accessor[kUser](), kUser); }
void* __cdecl Hook_SteamFriends() { return Wrap(Real_Accessor[kFriends](), kFriends); }
void* __cdecl Hook_SteamUtils() { return Wrap(Real_Accessor[kUtils](), kUtils); }
void* __cdecl Hook_SteamApps() { return Wrap(Real_Accessor[kApps](), kApps); }

// ---- Steam callbacks ---------------------------------------------------------------------------------------------
using RegisterCallback_t = void(__cdecl*)(void*, int);
using RegisterCallResult_t = void(__cdecl*)(void*, unsigned long long);
using UnregisterCallResult_t = void(__cdecl*)(void*, unsigned long long);
using RunCallbacks_t = void(__cdecl*)();
RegisterCallback_t Real_RegisterCallback = nullptr;
RegisterCallResult_t Real_RegisterCallResult = nullptr;
UnregisterCallResult_t Real_UnregisterCallResult = nullptr;
RunCallbacks_t Real_RunCallbacks = nullptr;
constexpr int kP2PSessionRequestCallback = 1202;
void* g_p2pSessionRequestCallbacks[8] = {};
int g_numP2PSessionRequestCallbacks = 0;
DWORD g_p2pSessionRequestMask = 0;
void __cdecl Hook_RegisterCallback(void* callback, int id) {
  Log("SteamAPI_RegisterCallback(%p, id=%d)", callback, id);
  if (g_harness && id == kP2PSessionRequestCallback && callback &&
      g_numP2PSessionRequestCallbacks < static_cast<int>(sizeof(g_p2pSessionRequestCallbacks) /
                                                         sizeof(g_p2pSessionRequestCallbacks[0]))) {
    g_p2pSessionRequestCallbacks[g_numP2PSessionRequestCallbacks++] = callback;
    void** vtable = *reinterpret_cast<void***>(callback);
    Log("callbacks: captured P2PSessionRequest object=%p count=%d vtable=%p slots=%p,%p,%p",
        callback, g_numP2PSessionRequestCallbacks, vtable,
        vtable ? vtable[0] : nullptr, vtable ? vtable[1] : nullptr, vtable ? vtable[2] : nullptr);
  }
  Real_RegisterCallback(callback, id);
}
void __cdecl Hook_RegisterCallResult(void* callback, unsigned long long call) {
  Log("SteamAPI_RegisterCallResult(%p, call=%llX)", callback, call);
  if (g_harness && IsFakeCall(call)) {
    EnterCriticalSection(&g_lock);
    for (auto& pending : g_pendingCalls)
      if (pending.handle == call) { pending.callback = callback; break; }
    LeaveCriticalSection(&g_lock);
    return;
  }
  Real_RegisterCallResult(callback, call);
}
void __cdecl Hook_UnregisterCallResult(void* callback, unsigned long long call) {
  if (g_harness && IsFakeCall(call)) {
    EnterCriticalSection(&g_lock);
    for (auto& pending : g_pendingCalls)
      if (pending.handle == call && pending.callback == callback) memset(&pending, 0, sizeof(pending));
    LeaveCriticalSection(&g_lock);
    return;
  }
  Real_UnregisterCallResult(callback, call);
}
void DispatchFakeCalls() {
  for (;;) {
    PendingCall ready = {};
    EnterCriticalSection(&g_lock);
    for (auto& pending : g_pendingCalls) {
      if (!pending.handle || !pending.callback) continue;
      ready = pending;
      memset(&pending, 0, sizeof(pending));
      break;
    }
    LeaveCriticalSection(&g_lock);
    if (!ready.handle) break;
    Log("callbacks: dispatch fake type=%d handle=%llX object=%p", static_cast<int>(ready.type), ready.handle, ready.callback);
    coop_steam::RunCallResult(ready.callback, ready.payload, false, ready.handle);
  }
}

void DispatchP2PSessionRequest() {
  if (g_numP2PSessionRequestCallbacks <= 0) return;

  const DWORD pendingMask = BusSourceMask(kGameP2PChannel) & ~g_p2pSessionRequestMask;
  for (int sourceInstance = 0; sourceInstance < 4; sourceInstance++) {
    const DWORD sourceBit = 1u << sourceInstance;
    if (!(pendingMask & sourceBit)) continue;
    const unsigned long long source = LocalSteamId(sourceInstance);
    g_p2pSessionRequestMask |= sourceBit;

    struct P2PSessionRequest {
      unsigned long long remote;
    } request = {source};
    Log("callbacks: dispatch P2PSessionRequest peer=%08lX%08lX objects=%d target=original",
        static_cast<DWORD>(source >> 32), static_cast<DWORD>(source),
        g_numP2PSessionRequestCallbacks);
    void* callback = g_p2pSessionRequestCallbacks[0];
    __try {
      // Vanilla slot 0 (008D25B0) is Run(payload, ioFailure, apiCall), ret 16.
      // Slot 1 (008D25E0) is Run(payload), ret 4 through the bound member.
      coop_steam::RunCallback(callback, &request);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      Log("callbacks: P2PSessionRequest object=%p fault=%08lX", callback, GetExceptionCode());
    }
    if (g_instance == 0) HarnessAcceptAdditionalPeer(source);
  }
}

bool TrySignalProductionFlowCommand(LONG memberCount) {
  LONG completed = InterlockedCompareExchange(&g_productionFlowSignalMembers, 0, 0);
  if (memberCount < completed) {
    InterlockedExchange(&g_productionFlowSignalMembers, memberCount < 2 ? 2 : memberCount);
    completed = InterlockedCompareExchange(&g_productionFlowSignalMembers, 0, 0);
  }

  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  BYTE* client = nullptr;
  BYTE* localNode = nullptr;
  BYTE* mesh = nullptr;
  LONG stage = -1;
  LONG localState = -1;
  LONG meshState = -1;
  bool ready = false;
  __try {
    BYTE* online = *reinterpret_cast<BYTE**>(base + (0x00E5F428 - 0x00400000));
    BYTE* p2p = online ? *reinterpret_cast<BYTE**>(online + 0xD8) : nullptr;
    client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
    localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
    mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
    stage = client ? *reinterpret_cast<LONG*>(client + 0x88) : -1;
    localState = localNode ? *reinterpret_cast<LONG*>(localNode + 0x0C) : -1;
    meshState = mesh ? *reinterpret_cast<LONG*>(mesh + 0x3C) : -1;
    ready = client && localNode && client[0x9D] == 0 && stage == 2 && localState == 3 && meshState == 3;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  if (!coop_matchmaking::ShouldSignalFlowForMemberCount(completed, memberCount, ready)) return false;

  BYTE* function = base + (0x00882040 - 0x00400000);
  const BYTE expected[] = {0x83, 0xEC, 0x50, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x5C, 0x83,
                           0xFF, 0x09, 0x8B, 0xF1};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    static volatile LONG signatureLogged = 0;
    if (InterlockedCompareExchange(&signatureLogged, 1, 0) == 0)
      Log("production transition: SignalFlowBasic signature mismatch");
    return false;
  }

  using SignalFlowBasicFn = void (__thiscall*)(void*, int, void*, void*);
  Log("production transition: signaling command=3 members=%ld completed=%ld client=%p stage=%ld localState=%ld meshState=%ld",
      memberCount, completed, client, stage, localState, meshState);
  __try {
    reinterpret_cast<SignalFlowBasicFn>(function)(client, 3, nullptr, nullptr);
    InterlockedExchange(&g_productionFlowSignalMembers, memberCount);
    Log("production transition: command=3 queued members=%ld", memberCount);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("production transition: command=3 raised exception code=%08lX", GetExceptionCode());
    return false;
  }
}

void __cdecl Hook_RunCallbacksImpl() {
  // Steam announces a new P2P sender before the title consumes that sender's setup traffic.
  // Discover all queued harness senders first so the real callback pump cannot drain the
  // evidence needed to synthesize P2PSessionRequest_t.
  if (g_harness) DispatchP2PSessionRequest();
  if (Real_RunCallbacks) Real_RunCallbacks();
  if (g_production) {
    static volatile LONG presencePublished = 0;
    void* friends = InterlockedCompareExchangePointer(&g_productionFriends, nullptr, nullptr);
    if (!friends && Real_Accessor[kFriends]) {
      friends = Real_Accessor[kFriends]();
      if (friends) InterlockedExchangePointer(&g_productionFriends, friends);
    }
    if (friends && !InterlockedCompareExchange(&presencePublished, 0, 0)) {
      using SetPresence_t = bool(__thiscall*)(void*, const char*, const char*);
      const bool published = reinterpret_cast<SetPresence_t>((*reinterpret_cast<void***>(friends))[
          coop_matchmaking::kFriendsSetRichPresenceSlot])(
          friends, coop_matchmaking::kProtocolKey, coop_matchmaking::kProtocolValue);
      if (published && !InterlockedExchange(&presencePublished, 1)) {
        Log("production matchmaking: published rich presence %s=%s", coop_matchmaking::kProtocolKey,
            coop_matchmaking::kProtocolValue);
      }
    }
    static volatile LONG memberTagPumps = 0;
    const LONG pump = InterlockedIncrement(&memberTagPumps);
    void* matchmaking = InterlockedCompareExchangePointer(&g_productionMatchmaking, nullptr, nullptr);
    const unsigned long long lobby = static_cast<unsigned long long>(InterlockedCompareExchange64(&g_productionLobby, 0, 0));
    if (matchmaking && lobby) {
      void** methods = *reinterpret_cast<void***>(matchmaking);
      using GetMemberCount_t = int(__thiscall*)(void*, unsigned long long);
      const LONG memberCount = reinterpret_cast<GetMemberCount_t>(methods[17])(matchmaking, lobby);
      TrySignalProductionFlowCommand(memberCount);
      if (pump <= 300 || pump % 600 == 0) {
        using SetMemberData_t = void(__thiscall*)(void*, unsigned long long, const char*, const char*);
        reinterpret_cast<SetMemberData_t>(methods[25])(
          matchmaking, lobby, coop_matchmaking::kProtocolKey, coop_matchmaking::kProtocolValue);
      }
    }
  }
  if (g_harness) {
    DispatchFakeCalls();
    if (g_instance == 0) {
      for (int instance = 1; instance < g_testInstances; instance++)
        HarnessAcceptAdditionalPeer(LocalSteamId(instance));
    }
  }
}

void __cdecl Hook_RunCallbacks() { Hook_RunCallbacksImpl(); }

struct LobbySelfTestCallback {
  void** vtable;
  FakeCallType expected;
  bool passed;
};
void __fastcall LobbySelfTestRun(LobbySelfTestCallback* self, void*, void* payload, bool failure,
                                 unsigned long long handle) {
  bool valid = !failure && IsFakeCall(handle);
  if (self->expected == FakeCallType::LobbyCreated) {
    auto* result = static_cast<LobbyCreatedResult*>(payload);
    valid = valid && result->result == 1 && result->lobby == kLocalLobbyId;
  } else if (self->expected == FakeCallType::LobbyEnter) {
    auto* result = static_cast<LobbyEnterResult*>(payload);
    valid = valid && result->response == 1 && result->lobby == kLocalLobbyId;
  } else if (self->expected == FakeCallType::LobbyList) {
    valid = valid && static_cast<LobbyMatchListResult*>(payload)->count == 1;
  }
  self->passed = valid;
}
void* g_lobbySelfTestVtable[] = {reinterpret_cast<void*>(LobbySelfTestRun)};

bool RunLobbySelfTestCall(FakeCallType type, unsigned long long handle) {
  LobbySelfTestCallback callback = {g_lobbySelfTestVtable, type, false};
  Hook_RegisterCallResult(&callback, handle);
  DispatchFakeCalls();
  return callback.passed;
}

DWORD WINAPI LobbySelfTestThread(LPVOID) {
  for (int wait = 0; wait < 80 && (!g_bus || LiveLobbyMask() == 0 && g_instance != 0); wait++) Sleep(100);
  while (g_bus) {
    int live = 0;
    for (const auto& slot : g_bus->slots) if (slot.pid && ProcessAlive(slot.pid)) live++;
    if (live == g_testInstances) break;
    Sleep(100);
  }
  if (!g_bus) return 0;

  bool resultPassed = false;
  if (g_instance == 0) {
    resultPassed = RunLobbySelfTestCall(FakeCallType::LobbyCreated,
        Fake_Matchmaking_CreateLobby(nullptr, nullptr, 3, 4));
    char hostId[32];
    _ui64toa_s(LocalSteamId(0), hostId, sizeof(hostId), 10);
    char hostIdHex[32];
    _ui64toa_s(LocalSteamId(0), hostIdHex, sizeof(hostIdHex), 16);
    const struct { const char* key; const char* value; } metadata[] = {
        {"HostId", hostIdHex}, {"AppId", "45740"}, {"Name", "Player 1"}, {"GameMode", "1"}, {"RankedMatch", "0"},
        {"SearchType", "0"}, {"SlotPrivateOpen", "0"}, {"SlotPublicOpen", "3"},
        {"SlotPrivateFilled", "0"}, {"SlotPublicFilled", "1"}, {"hostSteamID", hostId}};
    for (const auto& entry : metadata)
      Fake_Matchmaking_SetLobbyData(nullptr, nullptr, kLocalLobbyId, entry.key, entry.value);
    Log("loopback lobby: published DR2 story metadata HostId=%s hostSteamID=%s", hostIdHex, hostId);
  } else if (g_lobbyFrontendProbe) {
    // Validate discovery without pre-joining. The frontend must own the real JoinLobby transition.
    resultPassed = RunLobbySelfTestCall(FakeCallType::LobbyList,
        Fake_Matchmaking_RequestLobbyList(nullptr, nullptr));
  } else {
    for (int wait = 0; wait < 50 && !g_bus->lobbyActive; wait++) Sleep(100);
    resultPassed = RunLobbySelfTestCall(FakeCallType::LobbyEnter,
        Fake_Matchmaking_JoinLobby(nullptr, nullptr, kLocalLobbyId));
  }
  const bool listPassed = RunLobbySelfTestCall(FakeCallType::LobbyList,
      Fake_Matchmaking_RequestLobbyList(nullptr, nullptr));
  const int expectedMembers = g_lobbyFrontendProbe ? 1 : g_testInstances;
  for (int wait = 0; wait < 50 &&
       Fake_Matchmaking_GetNumLobbyMembers(nullptr, nullptr, kLocalLobbyId) < expectedMembers; wait++) Sleep(100);
  const int members = Fake_Matchmaking_GetNumLobbyMembers(nullptr, nullptr, kLocalLobbyId);
  Log("loopback lobby test: result=%s list=%s members=%d", resultPassed ? "valid" : "INVALID",
      listPassed ? "valid" : "INVALID", members);
  return 0;
}

// A directly-started DR2 process normally asks Steam to relaunch it and exits. Harness instances must remain in the
// process we created so their instance number, mutex namespace and eventual loopback identity stay attached.
using RestartAppIfNecessary_t = bool(__cdecl*)(unsigned int);
RestartAppIfNecessary_t Real_RestartAppIfNecessary = nullptr;
bool __cdecl Hook_RestartAppIfNecessary(unsigned int appId) {
  if (!g_harness && Real_RestartAppIfNecessary) return Real_RestartAppIfNecessary(appId);
  Log("SteamAPI_RestartAppIfNecessary(%u) bypassed for local harness", appId);
  return false;
}

// ---- Winsock ----------------------------------------------------------------------------------------------------
using socket_t = SOCKET(WSAAPI*)(int, int, int);
using bind_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using connect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
socket_t Real_socket;
bind_t Real_bind;
connect_t Real_connect;
sendto_t Real_sendto;
recvfrom_t Real_recvfrom;
LONG g_sendCount, g_recvCount;

void Address(const sockaddr* address, char* buffer, size_t size) {
  if (address && address->sa_family == AF_INET) {
    const auto* in = reinterpret_cast<const sockaddr_in*>(address);
    const BYTE* ip = reinterpret_cast<const BYTE*>(&in->sin_addr);
    sprintf_s(buffer, size, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(in->sin_port));
  } else {
    sprintf_s(buffer, size, "family=%d", address ? address->sa_family : -1);
  }
}
SOCKET WSAAPI Hook_socket(int af, int type, int protocol) {
  const SOCKET s = Real_socket(af, type, protocol);
  Log("socket(af=%d, type=%d, proto=%d) = %u", af, type, protocol, static_cast<unsigned>(s));
  return s;
}
int WSAAPI Hook_bind(SOCKET s, const sockaddr* address, int length) {
  const int result = Real_bind(s, address, length);
  char text[64];
  Address(address, text, sizeof(text));
  Log("bind(%u, %s) = %d", static_cast<unsigned>(s), text, result);
  return result;
}
int WSAAPI Hook_connect(SOCKET s, const sockaddr* address, int length) {
  const int result = Real_connect(s, address, length);
  char text[64];
  Address(address, text, sizeof(text));
  Log("connect(%u, %s) = %d", static_cast<unsigned>(s), text, result);
  return result;
}
int WSAAPI Hook_sendto(SOCKET s, const char* data, int length, int flags, const sockaddr* to, int toLength) {
  const int result = Real_sendto(s, data, length, flags, to, toLength);
  const LONG count = InterlockedIncrement(&g_sendCount);
  if (count <= 40 || count % 2000 == 0) {
    char text[64];
    Address(to, text, sizeof(text));
    Log("sendto(%u, %d bytes, %s) = %d #%ld", static_cast<unsigned>(s), length, text, result, count);
  }
  return result;
}
int WSAAPI Hook_recvfrom(SOCKET s, char* data, int length, int flags, sockaddr* from, int* fromLength) {
  const int result = Real_recvfrom(s, data, length, flags, from, fromLength);
  if (result > 0) {
    const LONG count = InterlockedIncrement(&g_recvCount);
    if (count <= 40 || count % 2000 == 0) {
      char text[64];
      Address(from, text, sizeof(text));
      Log("recvfrom(%u) = %d bytes from %s #%ld", static_cast<unsigned>(s), result, text, count);
    }
  }
  return result;
}

// ---- Quiet background harness -----------------------------------------------------------------------------------
// Harness copies must never steal focus, add taskbar windows or show modal crash/error boxes while the user works.
using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using SetForegroundWindow_t = BOOL(WINAPI*)(HWND);
using MessageBoxW_t = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
using MessageBoxA_t = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
ShowWindow_t Real_ShowWindow = ShowWindow;
SetForegroundWindow_t Real_SetForegroundWindow = SetForegroundWindow;
MessageBoxW_t Real_MessageBoxW = MessageBoxW;
MessageBoxA_t Real_MessageBoxA = MessageBoxA;
decltype(&SetCursorPos) Real_SetCursorPos = SetCursorPos;
decltype(&GetCursorPos) Real_GetCursorPos = GetCursorPos;
decltype(&ClipCursor) Real_ClipCursor = ClipCursor;
decltype(&ShowCursor) Real_ShowCursor = ShowCursor;
decltype(&SetCursor) Real_SetCursor = SetCursor;
HarnessCursor g_harnessCursor;

BOOL WINAPI Hook_SetCursorPos(int x, int y) {
  return g_privateInputProbe ? g_harnessCursor.SetPosition(x, y) : Real_SetCursorPos(x, y);
}
BOOL WINAPI Hook_GetCursorPos(LPPOINT point) {
  return g_privateInputProbe ? g_harnessCursor.GetPosition(point) : Real_GetCursorPos(point);
}
BOOL WINAPI Hook_ClipCursor(const RECT* rectangle) {
  return g_privateInputProbe ? TRUE : Real_ClipCursor(rectangle);
}
int WINAPI Hook_ShowCursor(BOOL visible) {
  return g_privateInputProbe ? g_harnessCursor.Show(visible) : Real_ShowCursor(visible);
}
HCURSOR WINAPI Hook_SetCursor(HCURSOR cursor) {
  return g_privateInputProbe ? g_harnessCursor.SetShape(cursor) : Real_SetCursor(cursor);
}

BOOL WINAPI Hook_ShowWindow(HWND window, int command) {
  if (g_harness && command != SW_HIDE) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, (style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
    SetWindowPos(window, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64,
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64, 640, 360,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    return Real_ShowWindow(window, SW_SHOWNOACTIVATE);
  }
  return Real_ShowWindow(window, command);
}
BOOL WINAPI Hook_SetForegroundWindow(HWND window) {
  if (g_harness && !g_isolatedDesktop) return TRUE;
  return Real_SetForegroundWindow(window);
}
int WINAPI Hook_MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type) {
  if (g_harness) {
    Log("suppressed MessageBoxW caption=\"%ls\" text=\"%ls\"", caption ? caption : L"", text ? text : L"");
    return IDOK;
  }
  return Real_MessageBoxW(owner, text, caption, type);
}
int WINAPI Hook_MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
  if (g_harness) {
    Log("suppressed MessageBoxA caption=\"%s\" text=\"%s\"", caption ? caption : "", text ? text : "");
    return IDOK;
  }
  return Real_MessageBoxA(owner, text, caption, type);
}

// DR2 requests IXAudio2 through CoCreateInstance. Muting the per-process mastering voice avoids touching Windows'
// persisted app-volume state. Bink has a separate DirectSound path, so its imported volume setter is forced to zero.
using CoCreateInstance_t = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
using CreateMasteringVoice_t = HRESULT(__stdcall*)(void*, void**, UINT32, UINT32, DWORD, UINT32, const void*);
using VoiceSetVolume_t = HRESULT(__stdcall*)(void*, float, UINT32);
using BinkSetVolume_t = void(__stdcall*)(void*, UINT32, int);
CoCreateInstance_t Real_CoCreateInstance = CoCreateInstance;
CreateMasteringVoice_t Real_CreateMasteringVoice = nullptr;
BinkSetVolume_t Real_BinkSetVolume = nullptr;

const GUID kIidXAudio2 = {0x8BCF1F58, 0x9FE7, 0x4583, {0x8A, 0xC6, 0xE2, 0xAD, 0xC4, 0x65, 0xC8, 0xBB}};

HRESULT __stdcall Hook_CreateMasteringVoice(void* xaudio, void** voice, UINT32 channels, UINT32 sampleRate,
                                            DWORD flags, UINT32 deviceIndex, const void* effectChain) {
  const HRESULT result = Real_CreateMasteringVoice(xaudio, voice, channels, sampleRate, flags, deviceIndex, effectChain);
  if (SUCCEEDED(result) && voice && *voice) {
    void** vtable = *reinterpret_cast<void***>(*voice);
    auto setVolume = reinterpret_cast<VoiceSetVolume_t>(vtable[12]);
    const HRESULT muteResult = setVolume(*voice, 0.0f, 0);
    Log("audio: IXAudio2 mastering voice muted result=%08lX", static_cast<DWORD>(muteResult));
  }
  return result;
}

HRESULT WINAPI Hook_CoCreateInstance(REFCLSID classId, LPUNKNOWN outer, DWORD context, REFIID interfaceId, LPVOID* object) {
  const HRESULT result = Real_CoCreateInstance(classId, outer, context, interfaceId, object);
  if (g_silent && SUCCEEDED(result) && object && *object && IsEqualGUID(interfaceId, kIidXAudio2)) {
    void** vtable = *reinterpret_cast<void***>(*object);
    if (vtable[10] != reinterpret_cast<void*>(Hook_CreateMasteringVoice)) {
      Real_CreateMasteringVoice = reinterpret_cast<CreateMasteringVoice_t>(vtable[10]);
      void* replacement = reinterpret_cast<void*>(Hook_CreateMasteringVoice);
      WriteSlot(&vtable[10], &replacement, sizeof(replacement));
      Log("audio: IXAudio2 mastering-voice hook installed");
    }
  }
  return result;
}

void __stdcall Hook_BinkSetVolume(void* bink, UINT32 track, int volume) {
  if (g_silent) volume = 0;
  Real_BinkSetVolume(bink, track, volume);
}

// ---- Single-instance mutex ----------------------------------------------------------------------------------------
using CreateEventA_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
using CreateSemaphoreA_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
CreateEventA_t Real_CreateEventA = CreateEventA;
CreateSemaphoreA_t Real_CreateSemaphoreA = CreateSemaphoreA;

HANDLE WINAPI Hook_CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial, LPCSTR name) {
  char renamed[128];
  if (!g_harness || !coop_sync::PrivateName(name, GetCurrentProcessId(), renamed))
    return Real_CreateEventA(attributes, manual, initial, name);
  HANDLE result = Real_CreateEventA(attributes, manual, initial, renamed);
  const DWORD error = GetLastError();
  Log("job isolation: event %s -> %s handle=%p existing=%d error=%lu", name, renamed, result,
      error == ERROR_ALREADY_EXISTS, error);
  SetLastError(error);
  return result;
}

HANDLE WINAPI Hook_CreateSemaphoreA(LPSECURITY_ATTRIBUTES attributes, LONG initial, LONG maximum, LPCSTR name) {
  char renamed[128];
  if (!g_harness || !coop_sync::PrivateName(name, GetCurrentProcessId(), renamed))
    return Real_CreateSemaphoreA(attributes, initial, maximum, name);
  HANDLE result = Real_CreateSemaphoreA(attributes, initial, maximum, renamed);
  const DWORD error = GetLastError();
  Log("job isolation: semaphore %s -> %s handle=%p existing=%d error=%lu", name, renamed, result,
      error == ERROR_ALREADY_EXISTS, error);
  SetLastError(error);
  return result;
}

using CreateMutexW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
CreateMutexW_t Real_CreateMutexW = CreateMutexW;
HANDLE WINAPI Hook_CreateMutexW(LPSECURITY_ATTRIBUTES attributes, BOOL owner, LPCWSTR name) {
  if (g_instance > 0 && name && wcscmp(name, L"DeadRising2") == 0) {
    wchar_t renamed[64];
    swprintf(renamed, 64, L"DeadRising2_coop%d", g_instance);
    Log("single-instance mutex renamed to %ls", renamed);
    return Real_CreateMutexW(attributes, owner, renamed);
  }
  return Real_CreateMutexW(attributes, owner, name);
}

// ---- Import patching (by name and by ordinal) ---------------------------------------------------------------------
struct Hook {
  const char* dll;
  const char* name;   // or nullptr to match by ordinal
  WORD ordinal;
  void* hook;
  void** real;
  bool traceOnly;     // installed only with tracing on
};

Hook g_hooks[] = {
    {"KERNEL32.dll", "CreateEventA", 0, reinterpret_cast<void*>(Hook_CreateEventA), reinterpret_cast<void**>(&Real_CreateEventA), false},
    {"KERNEL32.dll", "CreateSemaphoreA", 0, reinterpret_cast<void*>(Hook_CreateSemaphoreA), reinterpret_cast<void**>(&Real_CreateSemaphoreA), false},
    {"KERNEL32.dll", "CreateMutexW", 0, reinterpret_cast<void*>(Hook_CreateMutexW), reinterpret_cast<void**>(&Real_CreateMutexW), false},
    {"USER32.dll", "ShowWindow", 0, reinterpret_cast<void*>(Hook_ShowWindow), reinterpret_cast<void**>(&Real_ShowWindow), false},
    {"USER32.dll", "SetForegroundWindow", 0, reinterpret_cast<void*>(Hook_SetForegroundWindow), reinterpret_cast<void**>(&Real_SetForegroundWindow), false},
    {"USER32.dll", "SetCursorPos", 0, reinterpret_cast<void*>(Hook_SetCursorPos), reinterpret_cast<void**>(&Real_SetCursorPos), false},
    {"USER32.dll", "GetCursorPos", 0, reinterpret_cast<void*>(Hook_GetCursorPos), reinterpret_cast<void**>(&Real_GetCursorPos), false},
    {"USER32.dll", "ClipCursor", 0, reinterpret_cast<void*>(Hook_ClipCursor), reinterpret_cast<void**>(&Real_ClipCursor), false},
    {"USER32.dll", "ShowCursor", 0, reinterpret_cast<void*>(Hook_ShowCursor), reinterpret_cast<void**>(&Real_ShowCursor), false},
    {"USER32.dll", "SetCursor", 0, reinterpret_cast<void*>(Hook_SetCursor), reinterpret_cast<void**>(&Real_SetCursor), false},
    {"USER32.dll", "MessageBoxW", 0, reinterpret_cast<void*>(Hook_MessageBoxW), reinterpret_cast<void**>(&Real_MessageBoxW), false},
    {"USER32.dll", "MessageBoxA", 0, reinterpret_cast<void*>(Hook_MessageBoxA), reinterpret_cast<void**>(&Real_MessageBoxA), false},
    {"ole32.dll", "CoCreateInstance", 0, reinterpret_cast<void*>(Hook_CoCreateInstance), reinterpret_cast<void**>(&Real_CoCreateInstance), false},
    {"binkw32.dll", "_BinkSetVolume@12", 0, reinterpret_cast<void*>(Hook_BinkSetVolume), reinterpret_cast<void**>(&Real_BinkSetVolume), false},
    {"steam_api.dll", "SteamMatchmaking", 0, reinterpret_cast<void*>(Hook_SteamMatchmaking), reinterpret_cast<void**>(&Real_Accessor[kMatchmaking]), true},
    {"steam_api.dll", "SteamNetworking", 0, reinterpret_cast<void*>(Hook_SteamNetworking), reinterpret_cast<void**>(&Real_Accessor[kNetworking]), true},
    {"steam_api.dll", "SteamUser", 0, reinterpret_cast<void*>(Hook_SteamUser), reinterpret_cast<void**>(&Real_Accessor[kUser]), true},
    {"steam_api.dll", "SteamFriends", 0, reinterpret_cast<void*>(Hook_SteamFriends), reinterpret_cast<void**>(&Real_Accessor[kFriends]), true},
    {"steam_api.dll", "SteamUtils", 0, reinterpret_cast<void*>(Hook_SteamUtils), reinterpret_cast<void**>(&Real_Accessor[kUtils]), true},
    {"steam_api.dll", "SteamApps", 0, reinterpret_cast<void*>(Hook_SteamApps), reinterpret_cast<void**>(&Real_Accessor[kApps]), true},
    {"steam_api.dll", "SteamAPI_RegisterCallback", 0, reinterpret_cast<void*>(Hook_RegisterCallback), reinterpret_cast<void**>(&Real_RegisterCallback), true},
    {"steam_api.dll", "SteamAPI_RegisterCallResult", 0, reinterpret_cast<void*>(Hook_RegisterCallResult), reinterpret_cast<void**>(&Real_RegisterCallResult), true},
    {"steam_api.dll", "SteamAPI_UnregisterCallResult", 0, reinterpret_cast<void*>(Hook_UnregisterCallResult), reinterpret_cast<void**>(&Real_UnregisterCallResult), true},
    {"steam_api.dll", "SteamAPI_RunCallbacks", 0, reinterpret_cast<void*>(Hook_RunCallbacks), reinterpret_cast<void**>(&Real_RunCallbacks), true},
    {"steam_api.dll", "SteamAPI_RestartAppIfNecessary", 0, reinterpret_cast<void*>(Hook_RestartAppIfNecessary), reinterpret_cast<void**>(&Real_RestartAppIfNecessary), false},
    {"WS2_32.dll", nullptr, 23, reinterpret_cast<void*>(Hook_socket), reinterpret_cast<void**>(&Real_socket), true},
    {"WS2_32.dll", nullptr, 2, reinterpret_cast<void*>(Hook_bind), reinterpret_cast<void**>(&Real_bind), true},
    {"WS2_32.dll", nullptr, 4, reinterpret_cast<void*>(Hook_connect), reinterpret_cast<void**>(&Real_connect), true},
    {"WS2_32.dll", nullptr, 20, reinterpret_cast<void*>(Hook_sendto), reinterpret_cast<void**>(&Real_sendto), true},
    {"WS2_32.dll", nullptr, 17, reinterpret_cast<void*>(Hook_recvfrom), reinterpret_cast<void**>(&Real_recvfrom), true},
};

int PatchImports() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return 0;
  int hooked = 0;
  for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name; descriptor++) {
    const char* dllName = reinterpret_cast<char*>(base + descriptor->Name);
    if (!descriptor->OriginalFirstThunk) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
    for (; names->u1.AddressOfData; names++, slots++) {
      const bool byOrdinal = IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal);
      const char* importName = byOrdinal ? nullptr : reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
      for (auto& hook : g_hooks) {
        if (hook.traceOnly && !g_trace &&
            !(g_production && coop_matchmaking::IsRequiredProductionImport(hook.dll, hook.name))) continue;
        if (_stricmp(dllName, hook.dll) != 0) continue;
        if (hook.name ? (byOrdinal || strcmp(importName, hook.name) != 0) : (!byOrdinal || IMAGE_ORDINAL32(names->u1.Ordinal) != hook.ordinal)) continue;
        void* current = reinterpret_cast<void*>(static_cast<ULONG_PTR>(slots->u1.Function));
        if (current == hook.hook) {
          hooked++;
          continue;
        }
        *hook.real = current;
        const DWORD replacement = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(hook.hook));
        if (WriteSlot(&slots->u1.Function, &replacement, sizeof(replacement))) {
          hooked++;
          if (hook.name) Log("hooked import %s!%s", hook.dll, hook.name);
          else Log("hooked import %s!#%u", hook.dll, hook.ordinal);
        }
      }
    }
  }
  return hooked;
}

void LogBytes(const BYTE* address, size_t size) {
  char text[3 * 96 + 1] = {};
  const size_t shown = size < 96 ? size : 96;
  for (size_t i = 0; i < shown; i++) sprintf_s(text + i * 3, sizeof(text) - i * 3, "%02X ", address[i]);
  Log("engine probe bytes: %p: %s", address, text);
}

bool MatchMasked(const BYTE* address, const BYTE* pattern, const char* mask) {
  __try {
    for (size_t i = 0; mask[i]; i++)
      if (mask[i] == 'x' && address[i] != pattern[i]) return false;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void __fastcall Hook_AllocClientData(void* server, void*, unsigned int count) {
  const unsigned int effectiveCount =
      g_production && g_requestedPlayers == 4 && count == 2 ? 4 : count;
  Log("client table: AllocClientData server=%p requested=%u effective=%u", server, count, effectiveCount);
  g_allocClientData(server, effectiveCount);
  InterlockedExchangePointer(&g_clientDataServer, server);
  InterlockedExchange(&g_clientDataCount, static_cast<LONG>(effectiveCount));
  __try {
    Log("client table: allocated records=%p local-server=%p", *reinterpret_cast<void**>(static_cast<BYTE*>(server) + 0x54),
        *reinterpret_cast<void**>(static_cast<BYTE*>(server) + 0x58));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("client table: allocation completed but server fields were unreadable");
  }
}

DWORD WINAPI ClientDataMonitorThread(LPVOID) {
  BYTE previous[4 * 0x48] = {};
  LONG previousCount = -1;
  DWORD previousCapacity = 0xFFFFFFFF;
  void* previousServer = nullptr;
  for (;;) {
    Sleep(250);
    void* server = InterlockedCompareExchangePointer(&g_clientDataServer, nullptr, nullptr);
    LONG count = InterlockedCompareExchange(&g_clientDataCount, 0, 0);
    if (!server || count <= 0) continue;
    if (count > 4) count = 4;
    __try {
      BYTE* object = static_cast<BYTE*>(server);
      BYTE* records = *reinterpret_cast<BYTE**>(object + 0x54);
      BYTE* localServer = *reinterpret_cast<BYTE**>(object + 0x58);
      const DWORD capacity = localServer ? *reinterpret_cast<DWORD*>(localServer + 0x48) : 0;
      if (!records) continue;
      const size_t bytes = static_cast<size_t>(count) * 0x48;
      if (server == previousServer && count == previousCount && capacity == previousCapacity &&
          memcmp(previous, records, bytes) == 0) continue;
      Log("client table: changed server=%p allocated=%ld local-server-capacity=%lu", server, count, capacity);
      if (localServer) {
        const unsigned long long owner = *reinterpret_cast<unsigned long long*>(localServer + 0x38);
        Log("client table: local-server=%p vtable=%p owner=%08lX%08lX maxClients=%lu firstPeer=%p firstClient=%p",
            localServer, *reinterpret_cast<void***>(localServer), static_cast<DWORD>(owner >> 32),
            static_cast<DWORD>(owner), capacity, *reinterpret_cast<void**>(localServer + 0x54),
            *reinterpret_cast<void**>(localServer + 0x58));
      }
      for (LONG index = 0; index < count; index++) {
        BYTE* record = records + index * 0x48;
        const unsigned long long peerId = *reinterpret_cast<unsigned long long*>(record);
        if (record[0x0C] == 1 && record[0x0D] == 0) MarkNativePeerConfirmed(peerId);
        Log("client table: slot=%ld peer=%08lX%08lX index=%lu confirmed=%u leaving=%u ready=%lu private=%lu confirmations=%u,%u,%u,%u flow=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            index, static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId),
            *reinterpret_cast<DWORD*>(record + 0x08), record[0x0C], record[0x0D],
            *reinterpret_cast<DWORD*>(record + 0x3C), *reinterpret_cast<DWORD*>(record + 0x40),
            record[0x0E], record[0x0F], record[0x10], record[0x11], record[0x12], record[0x13],
            record[0x14], record[0x15], record[0x16], record[0x17], record[0x18], record[0x19],
            record[0x1A], record[0x1B]);
      }
      memset(previous, 0, sizeof(previous));
      memcpy(previous, records, bytes);
      previousServer = server;
      previousCount = count;
      previousCapacity = capacity;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      Log("client table: monitor read fault server=%p", server);
      InterlockedExchangePointer(&g_clientDataServer, nullptr);
      InterlockedExchange(&g_clientDataCount, 0);
    }
  }
  return 0;
}

bool InstallClientDataProbe(BYTE* base) {
  if (InterlockedCompareExchange(&g_clientDataProbeInstalled, 0, 0) == 1) return true;
  // One path rebuilds the table from platform members; cP2PServer::Start allocates the initial table
  // from BeginDirectP2PGame's requested player count. Tap both calls without replacing the allocator.
  BYTE* populationCall = base + (0x00874163 - 0x00400000);
  BYTE* startupCall = base + (0x0087DA3E - 0x00400000);
  BYTE* allocator = base + (0x00865300 - 0x00400000);
  const BYTE expectedPopulation[] = {0x8B, 0x48, 0x48, 0x51, 0x8B, 0xCD, 0xE8};
  const BYTE expectedStartup[] = {0x8B, 0x84, 0x24, 0x4C, 0x01, 0x00, 0x00, 0x50, 0x8B, 0xCE, 0xE8};
  if (memcmp(populationCall - 6, expectedPopulation, sizeof(expectedPopulation)) != 0 ||
      memcmp(startupCall - 10, expectedStartup, sizeof(expectedStartup)) != 0) {
    Log("client table: allocation call signatures mismatch; probe not installed");
    return false;
  }
  BYTE* calls[] = {populationCall, startupCall};
  for (BYTE* callSite : calls) {
    LONG existingRelative = 0;
    memcpy(&existingRelative, callSite + 1, sizeof(existingRelative));
    if (callSite[0] != 0xE8 || callSite + 5 + existingRelative != allocator) {
      Log("client table: allocation target mismatch call=%p actual=%p expected=%p; probe not installed",
          callSite, callSite + 5 + existingRelative, allocator);
      return false;
    }
  }
  g_allocClientData = reinterpret_cast<AllocClientDataFn>(allocator);
  for (BYTE* callSite : calls) {
    BYTE replacement[5] = {0xE8};
    const LONG relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_AllocClientData) - (callSite + 5));
    memcpy(replacement + 1, &relative, sizeof(relative));
    if (!WriteSlot(callSite, replacement, sizeof(replacement))) {
      Log("client table: failed to install allocation probe at %p", callSite);
      return false;
    }
  }
  Log("client table: allocation probes installed population=%p startup=%p allocator=%p hook=%p",
      populationCall, startupCall, allocator, &Hook_AllocClientData);
  InterlockedExchange(&g_clientDataProbeInstalled, 1);
  HANDLE monitor = CreateThread(nullptr, 0, ClientDataMonitorThread, nullptr, 0, nullptr);
  if (monitor) CloseHandle(monitor);
  return true;
}

void* __fastcall Hook_StarTopologyHost(void* topology, void*, void* ctorInfo) {
  __try {
    void* existing = *reinterpret_cast<void**>(static_cast<BYTE*>(topology) + 0x7C);
    if (existing) {
      Log("direct host: rejecting occupied topology=%p existing=%p vtable=%p",
          topology, existing, *reinterpret_cast<void**>(existing));
      return nullptr;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  void* result = g_starTopologyHost(topology, ctorInfo);
  Log("direct host: native local server constructed topology=%p server=%p vtable=%p",
      topology, result, result ? *reinterpret_cast<void**>(result) : nullptr);
  if (result) {
    void** table = *reinterpret_cast<void***>(result);
    if (table[16] == reinterpret_cast<void*>(0x00871190)) {
      g_localServerAccept = reinterpret_cast<LocalServerAcceptFn>(table[16]);
      void* replacement = &Hook_LocalServerAccept;
      WriteSlot(table + 16, &replacement, sizeof(replacement));
    }
  }
  return result;
}

bool InstallStarTopologyReuseProbe(BYTE* base) {
  BYTE* callSite = base + (0x0087D991 - 0x00400000);
  BYTE* original = base + (0x00862800 - 0x00400000);
  const BYTE expected[] = {0x89, 0x5C, 0x24, 0x2C, 0x89, 0x5C, 0x24, 0x30,
                           0x89, 0x5C, 0x24, 0x34, 0xE8};
  if (memcmp(callSite - (sizeof(expected) - 1), expected, sizeof(expected)) != 0 || callSite[0] != 0xE8) {
    Log("direct host: cStarTopology::Host call signature mismatch; reuse probe not installed");
    return false;
  }
  LONG target = 0;
  memcpy(&target, callSite + 1, sizeof(target));
  if (callSite + 5 + target != original) {
    Log("direct host: cStarTopology::Host target mismatch; reuse probe not installed");
    return false;
  }
  g_starTopologyHost = reinterpret_cast<StarTopologyHostFn>(original);
  BYTE replacement[5] = {0xE8};
  const LONG relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_StarTopologyHost) - (callSite + 5));
  memcpy(replacement + 1, &relative, sizeof(relative));
  const bool installed = WriteSlot(callSite, replacement, sizeof(replacement));
  Log("direct host: cStarTopology native-host trace %s", installed ? "installed" : "FAILED");
  return installed;
}

bool __fastcall Hook_P2PClientConnect(void* client, void*, const unsigned long long* address,
                                     unsigned short port, bool privateSlot) {
  if (g_instance == 0 && g_directP2PProbe) {
    // The synthetic first-party address cannot identify the host. Leave its
    // topology empty for InitAsServer instead of constructing a cRemoteServer.
    Log("direct host: skipped synthetic remote connector; reserving topology for native local server");
    return false;
  }
  unsigned long long originalAddress = 0;
  __try {
    if (address) originalAddress = *address;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  const unsigned long long peerId = LocalSteamId(g_instance == 0 ? 1 : 0);
  // The incomplete local first-party session supplies sentinel 0x10 here.
  // Remote clients connect to the harness host using the real native handshake.
  const unsigned long long* effectiveAddress = &peerId;
  Log("direct client: Connect call intercepted client=%p original=%08lX%08lX effective=%08lX%08lX port=%u private=%d",
      client, static_cast<DWORD>(originalAddress >> 32), static_cast<DWORD>(originalAddress),
      static_cast<DWORD>(*effectiveAddress >> 32), static_cast<DWORD>(*effectiveAddress), port, privateSlot);
  const bool result = g_p2pClientConnect(client, effectiveAddress, port, privateSlot);
  Log("direct client: Connect returned=%d mClient=%p", result,
      *reinterpret_cast<void**>(static_cast<BYTE*>(client) + 0x90));
  return result;
}

bool InstallDirectClientConnectProbe(BYTE* base) {
  BYTE* callSite = base + (0x00886613 - 0x00400000);
  BYTE* original = base + (0x0087C800 - 0x00400000);
  const BYTE expectedPrefix[] = {0xFF, 0xD0, 0x8B, 0xC8, 0xE8};
  LONG relative = 0;
  memcpy(&relative, callSite + 1, sizeof(relative));
  if (memcmp(callSite - 4, expectedPrefix, sizeof(expectedPrefix)) != 0 ||
      callSite + 5 + relative != original) {
    Log("direct client: Connect call signature mismatch; endpoint probe not installed");
    return false;
  }
  g_p2pClientConnect = reinterpret_cast<P2PClientConnectFn>(original);
  BYTE replacement[5] = {0xE8};
  relative = static_cast<LONG>(reinterpret_cast<BYTE*>(&Hook_P2PClientConnect) - (callSite + 5));
  memcpy(replacement + 1, &relative, sizeof(relative));
  const bool installed = WriteSlot(callSite, replacement, sizeof(replacement));
  Log("direct client: first Connect endpoint probe %s", installed ? "installed" : "FAILED");
  return installed;
}

void LogCampaignActorState(BYTE* p2p) {
  static unsigned long long lastSignature = ~0ULL;
  static DWORD observations = 0;
  __try {
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    BYTE* game = *reinterpret_cast<BYTE**>(base + (0x00DDC3F0 - 0x00400000));
    BYTE* scene = game ? *reinterpret_cast<BYTE**>(game + 0x2C) : nullptr;
    BYTE* actorManager = scene ? *reinterpret_cast<BYTE**>(scene + 0x94) : nullptr;
    BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
    BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
    const LONG localSlot = localNode ? *reinterpret_cast<LONG*>(localNode + 0xB8) : -1;
    const BYTE singlePlayer = client ? client[0x9D] : 0xFF;
    const LONG gameType = client ? *reinterpret_cast<LONG*>(client + 0xA8) : -1;
    BYTE* actors[4]{};
    unsigned long long signature = reinterpret_cast<ULONG_PTR>(scene) ^
        (static_cast<unsigned long long>(static_cast<DWORD>(localSlot)) << 32) ^
        (static_cast<unsigned long long>(singlePlayer) << 56);
    for (int slot = 0; slot < 4; slot++) {
      actors[slot] = actorManager ? *reinterpret_cast<BYTE**>(actorManager + 0x0C + slot * 4) : nullptr;
      signature ^= static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(actors[slot])) << (slot * 7);
      if (actors[slot]) signature ^= static_cast<unsigned long long>(actors[slot][0xD83C]) << (slot * 8);
    }
    observations++;
    if (signature == lastSignature && observations % 30 != 0) return;
    lastSignature = signature;
    Log("campaign actors: game=%p scene=%p manager=%p client=%p localNode=%p localSlot=%ld singlePlayer=%u gameType=%ld",
        game, scene, actorManager, client, localNode, localSlot, singlePlayer, gameType);
    for (int slot = 0; slot < 4; slot++) {
      if (actors[slot]) {
        void** vtable = *reinterpret_cast<void***>(actors[slot]);
        void* enableVirtual = vtable ? vtable[5] : nullptr;
        Log("campaign actor[%d]=%p user=%ld remoteEnabled=%u vtable=%p enableVirtual=%p", slot, actors[slot],
            *reinterpret_cast<LONG*>(actors[slot] + 0x3A0), actors[slot][0xD83C], vtable, enableVirtual);
      } else {
        Log("campaign actor[%d]=null", slot);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("campaign actors: read-only snapshot fault code=%08lX", GetExceptionCode());
  }
}

void TryActivateCampaignActors(BYTE* base, BYTE* p2p) {
  if (!g_actorActivationProbe || InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 0 ||
      !AllHarnessPeersConfirmed()) return;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  if (!client || !localNode || client[0x9D] == 0) return;
  BYTE* function = base + (0x00882C10 - 0x00400000);
  const BYTE expected[] = {0x51, 0x55, 0x8B, 0xE9, 0x80, 0xBD, 0x9D, 0x00, 0x00, 0x00, 0x00};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_campaignActivationState, 2, 0) == 0)
      Log("campaign activation: SinglePlayerToMultiPlayer signature mismatch; probe cancelled");
    return;
  }
  g_campaignActivationP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_campaignActivationState, 1, 0) != 0) return;
  Log("campaign activation: scheduled native conversion from worker thread=%lu client=%p localNode=%p",
      GetCurrentThreadId(), client, localNode);
}

bool CampaignActivationPendingInternal() {
  return g_actorActivationProbe && InterlockedCompareExchange(&g_campaignActivationState, 0, 0) == 1;
}

void ActivateCampaignActorsOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_campaignActivationState, 2, 1) != 1) return;
  BYTE* p2p = g_campaignActivationP2P;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  BYTE* function = base + (0x00882C10 - 0x00400000);
  Log("campaign activation: invoking native cP2PClient::SinglePlayerToMultiPlayer on game thread=%lu client=%p localNode=%p",
      GetCurrentThreadId(), client, localNode);
  if (!client || !localNode) {
    Log("campaign activation: game-thread dispatch lost client state");
    return;
  }
  using SinglePlayerToMultiPlayerFn = void (__thiscall*)(void*);
  __try {
    reinterpret_cast<SinglePlayerToMultiPlayerFn>(function)(client);
    Log("campaign activation: native conversion returned singlePlayer=%u", client[0x9D]);
    LogCampaignActorState(p2p);
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    Log("campaign activation: native conversion raised exception code=%08lX", GetExceptionCode());
  }
}

void LogConnectionMeshState(BYTE* base, const char* reason) {
  __try {
    BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
    if (!mesh) {
      Log("connection mesh: %s mesh=null", reason);
      return;
    }
    Log("connection mesh: %s mesh=%p state=%ld players=%ld localNetworkId=%ld topology=%p member=%p "
        "connectNext=%u hello=%ld hostDone=%u clientDone=%ld hostStart=%u",
        reason, mesh, *reinterpret_cast<LONG*>(mesh + 0x3C), *reinterpret_cast<LONG*>(mesh + 0x40),
        *reinterpret_cast<LONG*>(mesh + 0x44), *reinterpret_cast<void**>(mesh + 0x88),
        *reinterpret_cast<void**>(mesh + 0x90), mesh[0x94], *reinterpret_cast<LONG*>(mesh + 0x98),
        mesh[0x9C], *reinterpret_cast<LONG*>(mesh + 0xA0), mesh[0xA4]);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("connection mesh: %s snapshot fault=%08lX", reason, GetExceptionCode());
  }
}

void LogTopologyListenerState(BYTE* base, BYTE* p2p) {
  if (!g_meshListenerProbe) return;
  static DWORD lastSnapshot = 0;
  const DWORD now = GetTickCount();
  if (now - lastSnapshot < 1000) return;
  lastSnapshot = now;
  __try {
    BYTE* topologyManager = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x18) : nullptr;
    BYTE* listener = topologyManager ? *reinterpret_cast<BYTE**>(topologyManager + 0xBC) : nullptr;
    DWORD listenerVtable = listener ? *reinterpret_cast<DWORD*>(listener) : 0;
    Log("mesh listener: topology snapshot p2p=%p manager=%p listener=%p vtable=%08lX expected=%p",
        p2p, topologyManager, listener, listenerVtable, base + (0x00CBA244 - 0x00400000));
    if (listener && listenerVtable == reinterpret_cast<DWORD>(base + (0x00CBA244 - 0x00400000)))
      LogConnectionListenerSnapshot("topology periodic", listener);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("mesh listener: topology snapshot fault=%08lX", GetExceptionCode());
  }
}

void TryInitializeConnectionMesh(BYTE* base, BYTE* p2p) {
  LogTopologyListenerState(base, p2p);
  if (!g_hostStateTransferProbe ||
      InterlockedCompareExchange(&g_connectionMeshInitState, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 2 ||
      !AllHarnessPeersConfirmed()) return;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
  if (!client || !localNode || !mesh || client[0x9D] != 0 ||
      *reinterpret_cast<LONG*>(client + 0x88) != 2 ||
      *reinterpret_cast<LONG*>(localNode + 0x0C) != 3) return;

  const LONG meshState = *reinterpret_cast<LONG*>(mesh + 0x3C);
  if ((meshState == 2 && !g_meshListenerProbe) || meshState == 3) {
    if (InterlockedCompareExchange(&g_connectionMeshInitState, 2, 0) == 0)
      LogConnectionMeshState(base, "native lifecycle already started");
    return;
  }
  if (meshState != 1 && !(meshState == 2 && g_meshListenerProbe)) return;

  BYTE* function = base + (0x00887190 - 0x00400000);
  const BYTE expected[] = {0x83, 0xEC, 0x64, 0xA1, 0xB0, 0xB1, 0xD6, 0x00, 0x33, 0xC4};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_connectionMeshInitState, 3, 0) == 0)
      Log("connection mesh: cP2PClient::InitMesh signature mismatch; probe cancelled");
    return;
  }
  g_connectionMeshInitP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_connectionMeshInitState, 1, 0) != 0) return;
  LogConnectionMeshState(base, "scheduled native InitMesh");
}

bool ConnectionMeshInitPendingInternal() {
  return g_hostStateTransferProbe &&
      InterlockedCompareExchange(&g_connectionMeshInitState, 0, 0) == 1;
}

bool PrepareNativeMeshListener(BYTE* base) {
  if (!g_meshListenerProbe) return true;
  if (!g_canListen && !InstallJoinPolicyTrace(base)) return false;
  BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
  BYTE* manager = mesh ? *reinterpret_cast<BYTE**>(mesh + 0x34) : nullptr;
  BYTE* listener = manager ? *reinterpret_cast<BYTE**>(manager + 0xBC) : nullptr;
  if (!listener || *reinterpret_cast<BYTE**>(listener) != base + (0x00CBA244 - 0x00400000)) {
    Log("mesh listener: unsupported topology listener manager=%p listener=%p", manager, listener);
    return false;
  }
  LogConnectionListenerSnapshot("before mesh startup", listener);
  const LONG state = *reinterpret_cast<LONG*>(listener + 0x64);
  if (state == 1 || state == 2) return true;
  if (state != 0 || *reinterpret_cast<void**>(listener + 0x68) || listener[0x6C]) {
    Log("mesh listener: refusing to reinitialize existing listener=%p state=%ld", listener, state);
    return false;
  }
  BYTE* function = base + (0x008517E0 - 0x00400000);
  const BYTE expected[] = {0x83, 0xEC, 0x24, 0x56, 0x8B, 0xF1, 0x83, 0xBE, 0xBC, 0x00, 0x00, 0x00, 0x00};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    Log("mesh listener: cTopologyManager::Listen signature mismatch");
    return false;
  }
  // This entry constructs the listener connection before Listen(true); the raw listener method cannot do that.
  using ListenFn = bool (__thiscall*)(void*);
  const bool result = reinterpret_cast<ListenFn>(function)(manager);
  Log("mesh listener: native topology Listen returned=%d manager=%p thread=%lu", result, manager, GetCurrentThreadId());
  LogConnectionListenerSnapshot("after mesh startup", listener);
  return result;
}

void InitializeConnectionMeshOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_connectionMeshInitState, 2, 1) != 1) return;
  BYTE* p2p = g_connectionMeshInitP2P;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  Log("connection mesh: invoking native cP2PClient::InitMesh on game thread=%lu client=%p",
      GetCurrentThreadId(), client);
  if (!client) {
    Log("connection mesh: game-thread dispatch lost client state");
    return;
  }
  using InitMeshFn = void (__thiscall*)(void*);
  __try {
    if (!PrepareNativeMeshListener(base)) {
      InterlockedExchange(&g_connectionMeshInitState, 3);
      return;
    }
    BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
    if (mesh && *reinterpret_cast<LONG*>(mesh + 0x3C) == 1)
      reinterpret_cast<InitMeshFn>(base + (0x00887190 - 0x00400000))(client);
    Log("connection mesh: native InitMesh returned clientStarted=%u", client[0x251]);
    LogConnectionMeshState(base, "after native InitMesh");
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    InterlockedExchange(&g_connectionMeshInitState, 3);
    Log("connection mesh: native InitMesh raised exception code=%08lX", GetExceptionCode());
  }
}

bool ConnectionListenerRearmPendingInternal() {
  if (!g_meshListenerProbe) return false;
  const LONG state = InterlockedCompareExchange(&g_connectionListenerRearmState, 0, 0);
  return state == 1 || state == 2;
}

void RearmConnectionListenerOnCurrentThreadInternal() {
  LONG rearmState = InterlockedCompareExchange(&g_connectionListenerRearmState, 0, 0);
  if (rearmState != 1 && rearmState != 2) return;
  BYTE* listener = g_connectionListenerRearmObject;
  if (!listener) return;
  __try {
    LONG state = *reinterpret_cast<LONG*>(listener + 0x64);
    if (rearmState == 1) {
      BYTE* endpoint = *reinterpret_cast<BYTE**>(listener + 0x70);
      const LONG endpointState = endpoint ? *reinterpret_cast<LONG*>(endpoint + 0x98) : -1;
      if (state != 1 || !endpoint) {
        InterlockedExchange(&g_connectionListenerRearmState, 4);
        Log("mesh listener: lost pending connect listener=%p state=%ld endpoint=%p", listener, state, endpoint);
        return;
      }
      if (endpointState != 6 || endpoint[0x9C] == 0) return;
      Log("mesh listener: replaying native connect success listener=%p endpoint=%p thread=%lu",
          listener, endpoint, GetCurrentThreadId());
      g_connListenerConnectSuccess(listener, g_connectionListenerEvent);
      state = *reinterpret_cast<LONG*>(listener + 0x64);
      Log("mesh listener: replay returned state=%ld endpointState=%ld", state,
          *reinterpret_cast<LONG*>(endpoint + 0x98));
      if (state != 2) return;
      InterlockedExchange(&g_connectionListenerRearmState, 2);
      return;
    }
    if (state == 1 || state == 2) return;
    if (state != 0) {
      InterlockedExchange(&g_connectionListenerRearmState, 4);
      Log("mesh listener: cannot rearm after Pop listener=%p state=%ld", listener, state);
      return;
    }
    BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    BYTE* function = base + (0x00851A90 - 0x00400000);
    const BYTE expected[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x64, 0x04};
    if (memcmp(function, expected, sizeof(expected)) != 0) {
      InterlockedExchange(&g_connectionListenerRearmState, 4);
      Log("mesh listener: cConnListener2::Listen signature mismatch");
      return;
    }
    using ListenFn = bool (__thiscall*)(void*, bool);
    Log("mesh listener: rearming native listener=%p thread=%lu", listener, GetCurrentThreadId());
    const bool result = reinterpret_cast<ListenFn>(function)(listener, true);
    Log("mesh listener: native Listen returned=%d state=%ld connection=%p", result,
        *reinterpret_cast<LONG*>(listener + 0x64), *reinterpret_cast<void**>(listener + 0x68));
    InterlockedExchange(&g_connectionListenerRearmState, result ? 3 : 4);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    InterlockedExchange(&g_connectionListenerRearmState, 4);
    Log("mesh listener: native rearm raised exception code=%08lX", GetExceptionCode());
  }
}

void TryActivateCampaignActors(BYTE* base, BYTE* p2p);

void LogClientTransitionState(BYTE* p2p) {
  constexpr size_t kClientBytes = 0x404;
  static BYTE previous[kClientBytes]{};
  static BYTE* previousClient = nullptr;
  static bool havePrevious = false;
  BYTE snapshot[kClientBytes]{};

  __try {
    BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
    if (!client) {
      if (previousClient) Log("client transition: client destroyed old=%p", previousClient);
      previousClient = nullptr;
      havePrevious = false;
      return;
    }
    memcpy(snapshot, client, sizeof(snapshot));

    DWORD inviteState = 0;
    DWORD gameType = 0;
    DWORD localNode = 0;
    DWORD user = 0;
    memcpy(&inviteState, snapshot + 0x1C, sizeof(inviteState));
    memcpy(&localNode, snapshot + 0x90, sizeof(localNode));
    memcpy(&gameType, snapshot + 0xA8, sizeof(gameType));
    memcpy(&user, snapshot + 0x400, sizeof(user));

    if (!havePrevious || previousClient != client) {
      Log("client transition: initial client=%p invite=%lu localNode=%08lX singlePlayer=%u gameType=%lu user=%08lX",
          client, inviteState, localNode, snapshot[0x9D], gameType, user);
      for (size_t offset = 0; offset < sizeof(snapshot); offset += 16) {
        char bytes[16 * 3 + 1]{};
        const size_t remaining = sizeof(snapshot) - offset;
        const size_t count = remaining < 16 ? remaining : 16;
        for (size_t index = 0; index < count; index++)
          sprintf_s(bytes + index * 3, sizeof(bytes) - index * 3, "%02X ", snapshot[offset + index]);
        Log("client transition: initial +%03X %s", static_cast<unsigned>(offset), bytes);
      }
      memcpy(previous, snapshot, sizeof(previous));
      previousClient = client;
      havePrevious = true;
      return;
    }

    size_t changed = 0;
    size_t reported = 0;
    char changes[512]{};
    size_t used = 0;
    for (size_t offset = 0; offset < sizeof(snapshot); offset++) {
      if (snapshot[offset] == previous[offset]) continue;
      changed++;
      if (reported >= 64) continue;
      const int written = sprintf_s(changes + used, sizeof(changes) - used, "+%03X:%02X>%02X ",
                                    static_cast<unsigned>(offset), previous[offset], snapshot[offset]);
      if (written <= 0) break;
      used += static_cast<size_t>(written);
      reported++;
      if (reported % 8 == 0) {
        Log("client transition: delta %s", changes);
        changes[0] = '\0';
        used = 0;
      }
    }
    if (used) Log("client transition: delta %s", changes);
    if (changed) {
      Log("client transition: changed=%zu reported=%zu invite=%lu localNode=%08lX singlePlayer=%u gameType=%lu user=%08lX",
          changed, reported, inviteState, localNode, snapshot[0x9D], gameType, user);
      memcpy(previous, snapshot, sizeof(previous));
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("client transition: read-only snapshot fault code=%08lX", GetExceptionCode());
    havePrevious = false;
    previousClient = nullptr;
  }
}

void TryCompleteClientTransition(BYTE* base, BYTE* p2p) {
  if (g_nativeFlowProbe || !g_clientTransitionProbe || g_instance == 0 ||
      InterlockedCompareExchange(&g_clientTransitionState, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 2 ||
      !AllHarnessPeersConfirmed()) return;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  if (!client || !localNode || client[0x9D] != 0 || client[0x251] != 0) return;
  const LONG p2pStage = *reinterpret_cast<LONG*>(client + 0x88);
  const LONG localState = *reinterpret_cast<LONG*>(localNode + 0x0C);
  if (p2pStage != 2 || localState != 3) return;

  BYTE* function = base + (0x00889850 - 0x00400000);
  const BYTE expected[] = {0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57,
                           0x8B, 0xE9, 0x8B, 0x0D, 0xF0, 0xC3, 0xDD, 0x00, 0x6A, 0x01};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_clientTransitionState, 3, 0) == 0)
      Log("client transition: cP2PClient::StartGame signature mismatch; probe cancelled");
    return;
  }
  g_clientTransitionP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_clientTransitionState, 1, 0) != 0) return;
  Log("client transition: scheduled native StartGame from worker thread=%lu client=%p stage=%ld localState=%ld ready=%u started=%u jipType=%ld jipSize=%lu",
      GetCurrentThreadId(), client, p2pStage, localState, client[0x250], client[0x251],
      *reinterpret_cast<LONG*>(client + 0x3F4), *reinterpret_cast<DWORD*>(client + 0x3B4));
}

bool ClientTransitionPendingInternal() {
  return g_clientTransitionProbe && InterlockedCompareExchange(&g_clientTransitionState, 0, 0) == 1;
}

void CompleteClientTransitionOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_clientTransitionState, 2, 1) != 1) return;
  BYTE* p2p = g_clientTransitionP2P;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  Log("client transition: invoking native cP2PClient::StartGame on game thread=%lu client=%p localNode=%p",
      GetCurrentThreadId(), client, localNode);
  if (!client || !localNode) {
    Log("client transition: game-thread dispatch lost client state");
    return;
  }
  using StartGameFn = void (__thiscall*)(void*);
  __try {
    reinterpret_cast<StartGameFn>(base + (0x00889850 - 0x00400000))(client);
    Log("client transition: native StartGame returned ready=%u started=%u singlePlayer=%u",
        client[0x250], client[0x251], client[0x9D]);
    LogClientTransitionState(p2p);
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    Log("client transition: native StartGame raised exception code=%08lX", GetExceptionCode());
  }
}

void TryStartHostStateTransfer(BYTE* base, BYTE* p2p) {
  static ULONGLONG eligibleSince = 0;
  if (g_nativeFlowProbe || !g_hostStateTransferProbe || g_instance != 0 ||
      InterlockedCompareExchange(&g_hostStateTransferState, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 2 ||
      !AllHarnessPeersConfirmed()) {
    eligibleSince = 0;
    return;
  }
  BYTE* server = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x34) : nullptr;
  BYTE* localServer = server ? *reinterpret_cast<BYTE**>(server + 0x58) : nullptr;
  if (!server || !localServer || *reinterpret_cast<LONG*>(server + 0x13C) != 0) return;
  BYTE* records = *reinterpret_cast<BYTE**>(server + 0x54);
  const DWORD capacity = *reinterpret_cast<DWORD*>(localServer + 0x48);
  DWORD flowReady = 0;
  for (DWORD index = 0; records && index < capacity; index++) {
    BYTE* record = records + index * 0x48;
    if (record[0x0C] == 1 && record[0x0D] == 0 && record[0x15] == 1) flowReady++;
  }
  if (flowReady < static_cast<DWORD>(g_testInstances)) {
    eligibleSince = 0;
    return;
  }
  if (!eligibleSince) eligibleSince = GetTickCount64();
  if (GetTickCount64() - eligibleSince < 500) return;

  BYTE* function = base + (0x008744B0 - 0x00400000);
  const BYTE expected[] = {0x56, 0x8B, 0xF1, 0xE8, 0x98, 0x11, 0xFF, 0xFF};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_hostStateTransferState, 3, 0) == 0)
      Log("host state transfer: cP2PServer::StartCoopGameStateTransfer signature mismatch; probe cancelled");
    return;
  }
  g_hostStateTransferP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_hostStateTransferState, 1, 0) != 0) return;
  Log("host state transfer: scheduled after native flow consensus=%lu/%d from worker thread=%lu server=%p localServer=%p state=%ld active=%ld users=%ld cursor=%ld",
      flowReady, g_testInstances, GetCurrentThreadId(), server, localServer, *reinterpret_cast<LONG*>(server + 0x138),
      *reinterpret_cast<LONG*>(server + 0x13C), *reinterpret_cast<LONG*>(server + 0x140),
      *reinterpret_cast<LONG*>(server + 0x148));
}

void TrySignalClientFlowCommand(BYTE* base, BYTE* p2p) {
  if (!g_hostStateTransferProbe ||
      InterlockedCompareExchange(&g_clientFlowSignalState, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 2) return;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  if (!client || !localNode || client[0x9D] != 0) return;
  const LONG p2pStage = *reinterpret_cast<LONG*>(client + 0x88);
  const LONG localState = *reinterpret_cast<LONG*>(localNode + 0x0C);
  BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
  if (p2pStage != 2 || !mesh || *reinterpret_cast<LONG*>(mesh + 0x3C) != 3) return;
  if (!HandoffOnlineUpdateToNativeThread()) return;
  if (g_nativeFlowProbe) {
    static volatile LONG recorded = 0;
    if (InterlockedCompareExchange(&recorded, 1, 0) == 0)
      Log("native flow control: mesh connected; native online update owns loading; no harness READY/START/transfer/finalize signals");
    return;
  }

  BYTE* function = base + (0x00882040 - 0x00400000);
  const BYTE expected[] = {0x83, 0xEC, 0x50, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x5C, 0x83,
                           0xFF, 0x09, 0x8B, 0xF1};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_clientFlowSignalState, 3, 0) == 0)
      Log("client flow signal: cP2PClient::SignalFlowBasic signature mismatch; probe cancelled");
    return;
  }

  if (InterlockedCompareExchange(&g_clientFlowSignalState, 1, 0) != 0) return;
  using SignalFlowBasicFn = void (__thiscall*)(void*, int, void*, void*);
  Log("client flow signal: invoking native SignalFlowBasic(command=3) thread=%lu client=%p localNode=%p stage=%ld localState=%ld",
      GetCurrentThreadId(), client, localNode, p2pStage, localState);
  __try {
    reinterpret_cast<SignalFlowBasicFn>(function)(client, 3, nullptr, nullptr);
    InterlockedExchange(&g_clientFlowSignalState, 2);
    Log("client flow signal: native command=3 queued");
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    InterlockedExchange(&g_clientFlowSignalState, 3);
    Log("client flow signal: native command raised exception code=%08lX", GetExceptionCode());
  }
}

void TryRequestClientDataTransfer(BYTE* base, BYTE* p2p) {
  if (g_nativeFlowProbe || !g_hostStateTransferProbe || g_instance == 0 ||
      InterlockedCompareExchange(&g_clientDataTransferRequestState, 0, 0) != 0 ||
      InterlockedCompareExchange(&g_clientFlowSignalState, 0, 0) != 2 ||
      InterlockedCompareExchange(&g_campaignActivationState, 0, 0) != 2) return;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* localNode = client ? *reinterpret_cast<BYTE**>(client + 0x90) : nullptr;
  BYTE* mesh = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
  if (!client || !localNode || client[0x9D] != 0 ||
      *reinterpret_cast<LONG*>(client + 0x88) != 2 ||
      *reinterpret_cast<LONG*>(localNode + 0x0C) != 3 ||
      !mesh || *reinterpret_cast<LONG*>(mesh + 0x3C) != 3) return;

  BYTE* function = base + (0x008897D0 - 0x00400000);
  const BYTE expected[] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x50, 0x83,
                           0xE8, 0x00, 0x56, 0x8B, 0xF1, 0x74, 0x1F};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_clientDataTransferRequestState, 3, 0) == 0)
      Log("client data transfer: cP2PClient::SignalFlowDataTransfer signature mismatch; probe cancelled");
    return;
  }

  g_clientDataTransferRequestP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_clientDataTransferRequestState, 1, 0) != 0) return;
  Log("client data transfer: scheduled native START request from worker thread=%lu client=%p active=%u",
      GetCurrentThreadId(), client, client[0x97]);
}

bool ClientDataTransferRequestPendingInternal() {
  return g_hostStateTransferProbe &&
      InterlockedCompareExchange(&g_clientDataTransferRequestState, 0, 0) == 1;
}

void RequestClientDataTransferOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_clientDataTransferRequestState, 2, 1) != 1) return;
  BYTE* p2p = g_clientDataTransferRequestP2P;
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  Log("client data transfer: invoking native SignalFlowDataTransfer(START) on game thread=%lu client=%p active=%u",
      GetCurrentThreadId(), client, client ? client[0x97] : 0xFF);
  if (!client) {
    Log("client data transfer: game-thread dispatch lost client state");
    InterlockedExchange(&g_clientDataTransferRequestState, 3);
    return;
  }
  using SignalFlowDataTransferFn = void (__thiscall*)(void*, int, void*, void*);
  __try {
    reinterpret_cast<SignalFlowDataTransferFn>(base + (0x008897D0 - 0x00400000))(
        client, 0, nullptr, nullptr);
    Log("client data transfer: native START request queued active=%u descriptor=%ld",
        client[0x97], *reinterpret_cast<LONG*>(client + 0xAC));
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    InterlockedExchange(&g_clientDataTransferRequestState, 3);
    Log("client data transfer: native START request raised exception code=%08lX", GetExceptionCode());
  }
}

bool HostStateTransferPendingInternal() {
  return g_hostStateTransferProbe && InterlockedCompareExchange(&g_hostStateTransferState, 0, 0) == 1;
}

void StartHostStateTransferOnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_hostStateTransferState, 2, 1) != 1) return;
  BYTE* p2p = g_hostStateTransferP2P;
  BYTE* server = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x34) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  Log("host state transfer: invoking native cP2PServer::StartCoopGameStateTransfer after flow consensus on game thread=%lu server=%p",
      GetCurrentThreadId(), server);
  if (!server) {
    Log("host state transfer: game-thread dispatch lost server state");
    return;
  }
  using StartStateTransferFn = void (__thiscall*)(void*);
  __try {
    reinterpret_cast<StartStateTransferFn>(base + (0x008744B0 - 0x00400000))(server);
    BYTE* gateway = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
    Log("host state transfer: native start returned state=%ld active=%ld users=%ld cursor=%ld gateway=%p gatewayState=%ld",
        *reinterpret_cast<LONG*>(server + 0x138), *reinterpret_cast<LONG*>(server + 0x13C),
        *reinterpret_cast<LONG*>(server + 0x140), *reinterpret_cast<LONG*>(server + 0x148), gateway,
        gateway ? *reinterpret_cast<LONG*>(gateway + 0x3C) : -1);
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    Log("host state transfer: native start raised exception code=%08lX", GetExceptionCode());
  }
}

void TryFinalizeHostDataTransfer(BYTE* base, BYTE* p2p) {
  if (g_nativeFlowProbe || !g_hostStateTransferProbe || g_instance != 0 ||
      InterlockedCompareExchange(&g_hostStateTransferState, 0, 0) != 2 ||
      InterlockedCompareExchange(&g_hostFlow7FinalizeState, 0, 0) != 0) return;
  BYTE* server = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x34) : nullptr;
  BYTE* localServer = server ? *reinterpret_cast<BYTE**>(server + 0x58) : nullptr;
  if (!server || !localServer || *reinterpret_cast<LONG*>(server + 0x13C) != 0 ||
      *reinterpret_cast<LONG*>(server + 0x138) != 1) return;
  BYTE* records = *reinterpret_cast<BYTE**>(server + 0x54);
  const DWORD capacity = *reinterpret_cast<DWORD*>(localServer + 0x48);
  DWORD flowReady = 0;
  for (DWORD index = 0; records && index < capacity; index++) {
    BYTE* record = records + index * 0x48;
    if (record[0x0C] == 1 && record[0x0D] == 0 && record[0x19] == 1) flowReady++;
  }
  if (flowReady < static_cast<DWORD>(g_testInstances)) return;

  BYTE* function = base + (0x00875500 - 0x00400000);
  const BYTE expected[] = {0x81, 0xEC, 0x84, 0x02, 0x00, 0x00, 0xA1, 0xB0,
                           0xB1, 0xD6, 0x00, 0x33, 0xC4};
  if (memcmp(function, expected, sizeof(expected)) != 0) {
    if (InterlockedCompareExchange(&g_hostFlow7FinalizeState, 3, 0) == 0)
      Log("host data transfer: cP2PServer::UpdateFlowCommand signature mismatch; finalize cancelled");
    return;
  }
  g_hostFlow7FinalizeP2P = p2p;
  MemoryBarrier();
  if (InterlockedCompareExchange(&g_hostFlow7FinalizeState, 1, 0) != 0) return;
  Log("host data transfer: scheduled native UpdateFlowCommand(7) after transfer completion and flow consensus=%lu/%d",
      flowReady, g_testInstances);
}

bool HostFlow7FinalizePendingInternal() {
  return g_hostStateTransferProbe &&
      InterlockedCompareExchange(&g_hostFlow7FinalizeState, 0, 0) == 1;
}

void FinalizeHostFlow7OnCurrentThreadInternal() {
  if (InterlockedCompareExchange(&g_hostFlow7FinalizeState, 2, 1) != 1) return;
  BYTE* p2p = g_hostFlow7FinalizeP2P;
  BYTE* server = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x34) : nullptr;
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  Log("host data transfer: invoking native cP2PServer::UpdateFlowCommand(7) on game thread=%lu server=%p",
      GetCurrentThreadId(), server);
  if (!server) {
    Log("host data transfer: game-thread dispatch lost server state");
    InterlockedExchange(&g_hostFlow7FinalizeState, 3);
    return;
  }
  using UpdateFlowCommandFn = void (__thiscall*)(void*, int);
  __try {
    reinterpret_cast<UpdateFlowCommandFn>(base + (0x00875500 - 0x00400000))(server, 7);
    Log("host data transfer: native command-7 collection finalized");
  } __except (CaptureCampaignActivationException(GetExceptionInformation())) {
    InterlockedExchange(&g_hostFlow7FinalizeState, 3);
    Log("host data transfer: native command-7 finalize raised exception code=%08lX", GetExceptionCode());
  }
}

void LogProbeHealth(BYTE* p2p) {
  BYTE* topology = *reinterpret_cast<BYTE**>(p2p + 0x3C);
  BYTE* manager = topology ? *reinterpret_cast<BYTE**>(topology + 0x50) : nullptr;
  BYTE* connection = manager ? *reinterpret_cast<BYTE**>(manager + 0x84) : nullptr;
  if (connection) LogReliableLayer("periodic probe health", connection);
  if (g_instance == 0) {
    BYTE* server = *reinterpret_cast<BYTE**>(p2p + 0x34);
    BYTE* localServer = server ? *reinterpret_cast<BYTE**>(server + 0x58) : nullptr;
    if (server) {
      BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
      BYTE* gateway = *reinterpret_cast<BYTE**>(base + (0x00DDEA04 - 0x00400000));
      BYTE* online = *reinterpret_cast<BYTE**>(base + (0x00E5F428 - 0x00400000));
      BYTE* onlineTransport = online ? *reinterpret_cast<BYTE**>(online + 0xE0) : nullptr;
      BYTE* onlineManager = onlineTransport ? *reinterpret_cast<BYTE**>(onlineTransport + 0x84) : nullptr;
      BYTE* packetArena = onlineManager ? *reinterpret_cast<BYTE**>(onlineManager + 0x104) : nullptr;
      BYTE* sendBuffer = packetArena ? *reinterpret_cast<BYTE**>(packetArena + 0x8C) : nullptr;
      Log("host state transfer: health server=%p localServer=%p mode=%ld remoteClients=%ld state=%ld active=%ld users=%ld nextUser=%ld cursor=%ld gateway=%p gatewayState=%ld packetArena=%p sendBuffer=%p used=%lu capacity=%lu onlineOwner=%ld nativeUpdateThread=%ld nativeUpdateCalls=%ld",
          server, localServer, *reinterpret_cast<LONG*>(server + 0x44),
          *reinterpret_cast<LONG*>(server + 0x4C), *reinterpret_cast<LONG*>(server + 0x138),
          *reinterpret_cast<LONG*>(server + 0x13C), *reinterpret_cast<LONG*>(server + 0x140),
          *reinterpret_cast<LONG*>(server + 0x144), *reinterpret_cast<LONG*>(server + 0x148),
          gateway, gateway ? *reinterpret_cast<LONG*>(gateway + 0x3C) : -1, packetArena, sendBuffer,
          sendBuffer ? *reinterpret_cast<DWORD*>(sendBuffer + 0x270) : 0,
          sendBuffer ? *reinterpret_cast<DWORD*>(sendBuffer + 0x264) : 0,
          InterlockedCompareExchange(&g_onlineUpdateOwnership, 0, 0),
          InterlockedCompareExchange(&g_nativeOnlineUpdateThread, 0, 0),
          InterlockedCompareExchange(&g_nativeOnlineUpdateCalls, 0, 0));
    }
  }
  LogClientTransitionState(p2p);
  LogCampaignActorState(p2p);
}

DWORD WINAPI LobbyFrontendObserverThread(LPVOID) {
  const DWORD selfBit = 1u << g_instance;
  for (int wait = 0; wait < 1200 && (!g_bus || !(g_bus->lobbyMemberMask & selfBit)); wait++) Sleep(250);
  if (!g_bus || !(g_bus->lobbyMemberMask & selfBit)) {
    Log("lobby frontend: native session observer timed out before lobby membership");
    InterlockedExchange(&g_lobbyFrontendObserverStarted, 0);
    return 0;
  }
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  BYTE* observedP2P = nullptr;
  for (int wait = 0; wait < 1200 && !observedP2P; wait++) {
    __try {
      BYTE* online = *reinterpret_cast<BYTE**>(base + (0x00E5F428 - 0x00400000));
      BYTE* p2p = online ? *reinterpret_cast<BYTE**>(online + 0xD8) : nullptr;
      BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
      BYTE* topology = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x3C) : nullptr;
      if (client && topology) observedP2P = p2p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (!observedP2P) Sleep(250);
  }
  if (!observedP2P) {
    Log("lobby frontend: native session observer timed out before P2P client creation");
    InterlockedExchange(&g_lobbyFrontendObserverStarted, 0);
    return 0;
  }
  Log("lobby frontend: native session observation started p2p=%p", observedP2P);
  for (;;) {
    __try {
      BYTE* online = *reinterpret_cast<BYTE**>(base + (0x00E5F428 - 0x00400000));
      BYTE* currentP2P = online ? *reinterpret_cast<BYTE**>(online + 0xD8) : nullptr;
      if (currentP2P != observedP2P) {
        Log("lobby frontend: P2P transition old=%p new=%p", observedP2P, currentP2P);
        observedP2P = currentP2P;
      }
      if (observedP2P) {
        TryActivateCampaignActors(base, observedP2P);
        TryInitializeConnectionMesh(base, observedP2P);
        TrySignalClientFlowCommand(base, observedP2P);
        TryRequestClientDataTransfer(base, observedP2P);
        TryCompleteClientTransition(base, observedP2P);
        LogProbeHealth(observedP2P);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      Log("lobby frontend: native session snapshot fault=%08lX", GetExceptionCode());
    }
    Sleep(1000);
  }
}

bool StartLobbyFrontendObserver() {
  if (!g_harness || !g_lobbyFrontendProbe || g_directP2PProbe || g_instance <= 0) return true;
  if (InterlockedCompareExchange(&g_lobbyFrontendObserverStarted, 1, 0)) return true;
  // Start at the real join request, not process boot: slow menu navigation must not consume its lifetime.
  HANDLE observer = CreateThread(nullptr, 0, LobbyFrontendObserverThread, nullptr, 0, nullptr);
  if (!observer) {
    Log("lobby frontend: observer creation failed error=%lu", GetLastError());
    InterlockedExchange(&g_lobbyFrontendObserverStarted, 0);
    return false;
  }
  CloseHandle(observer);
  Log("lobby frontend: observer started for actual join request");
  return true;
}

bool ConfigureHarnessCampaign(void* online, bool isHost) {
  BYTE* p2p = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(online) + 0xD8);
  BYTE* client = p2p ? *reinterpret_cast<BYTE**>(p2p + 0x38) : nullptr;
  BYTE* user = client ? *reinterpret_cast<BYTE**>(client + 0x400) : nullptr;
  void** table = user ? *reinterpret_cast<void***>(user) : nullptr;
  if (!table || table[33] != reinterpret_cast<void*>(0x0084E060)) {
    Log("direct p2p: matchmaking getter signature mismatch; probe cancelled");
    return false;
  }
  using GetMatchMakerFn = BYTE* (__thiscall*)(void*);
  BYTE* matchMaker = reinterpret_cast<GetMatchMakerFn>(table[33])(user);
  if (!matchMaker) return false;
  LONG* gameType = reinterpret_cast<LONG*>(matchMaker + 0x20);
  if (*gameType < 0 || *gameType > 2) return false;
  const LONG oldType = *gameType;
  // Vanilla: TIR=0, COOP=1, unset=2. OTR removed TIR and uses COOP=0.
  // Native slot policy at 0x00851191 / 0x008623BB distinguishes these values.
  *gameType = 1;
  matchMaker[0x1C] = isHost ? 1 : 0;
  Log("direct p2p: native matchmaking campaign type=%ld->%ld asHost=%d matchMaker=%p",
      oldType, *gameType, isHost, matchMaker);
  return true;
}

bool InstallCampaignAdmissionCapacity(BYTE* base) {
  if (InterlockedCompareExchange(&g_campaignAdmissionCapacityInstalled, 0, 0) == 1) return true;
  // These are campaign-only branches: TIR's existing four-slot branches remain
  // intact. Validate every instruction before changing any immediate operand.
  struct Patch { DWORD address; BYTE expected[3]; BYTE replacement[3]; };
  const Patch patches[] = {
      {0x0085119E, {0x83, 0xFF, 0x02}, {0x83, 0xFF, 0x04}},
      {0x008623CC, {0x8D, 0x42, 0x02}, {0x8D, 0x42, 0x04}},
      {0x008623F8, {0x8D, 0x41, 0x02}, {0x8D, 0x41, 0x04}},
  };
  // Connect sets mMaxLinks=1 for campaign and 3 for TIR. Use its existing
  // three-remote branch, preserving IsFull and duplicate-connection checks.
  BYTE* linkPolicy = base + (0x0087C8C7 - 0x00400000);
  // The actual compare uses EBX (loaded with 1 at 0087C851), not an immediate.
  const BYTE linkPrefix[] = {0x39, 0x58, 0x20, 0x75, 0x08, 0x89, 0x99, 0xF4, 0, 0, 0, 0xEB, 0x0D,
                             0x8B, 0x4E, 0x30, 0xC7, 0x81, 0xF4, 0, 0, 0, 3, 0, 0, 0};
  if (memcmp(linkPolicy, linkPrefix, sizeof(linkPrefix)) != 0) {
    Log("direct campaign: link-capacity signature mismatch; probe cancelled");
    return false;
  }
  for (const auto& patch : patches) {
    if (memcmp(base + patch.address - 0x00400000, patch.expected, sizeof(patch.expected)) != 0) {
      Log("direct campaign: admission signature mismatch address=%08lX", patch.address);
      return false;
    }
  }
  if (g_requestedPlayers == 2) {
    Log("direct campaign: verified stock two-member admission and one-remote-link limits; no capacity patches applied");
    InterlockedExchange(&g_campaignAdmissionCapacityInstalled, 1);
    return true;
  }
  if (g_requestedPlayers != 4) {
    Log("direct campaign: unsupported requested player count=%d; probe cancelled", g_requestedPlayers);
    return false;
  }
  for (const auto& patch : patches) {
    if (!WriteSlot(base + patch.address - 0x00400000, patch.replacement, sizeof(patch.replacement))) return false;
  }
  const BYTE useThreeRemoteLinks = 0xEB;
  if (!WriteSlot(linkPolicy + 3, &useThreeRemoteLinks, sizeof(useThreeRemoteLinks))) return false;
  Log("direct campaign: native campaign admission capacity 2->4 at 0085119E,008623CC,008623F8");
  Log("direct campaign: native campaign remote-link limit 1->3 at 0087C8CA");
  InterlockedExchange(&g_campaignAdmissionCapacityInstalled, 1);
  return true;
}

bool InstallJoinPolicyTrace(BYTE* base) {
  if (InterlockedCompareExchange(&g_joinPolicyInstalled, 0, 0) == 1) return true;
  void** canListenSlot = reinterpret_cast<void**>(base + (0x00CB83C0 - 0x00400000));
  if (*canListenSlot == &Hook_CanListen) {
    InterlockedExchange(&g_joinPolicyInstalled, 1);
    return true;
  }
  if (*canListenSlot != base + (0x00863D40 - 0x00400000)) {
    Log("join policy: CanListen signature mismatch; probe cancelled");
    return false;
  }
  g_canListen = reinterpret_cast<CanListenFn>(*canListenSlot);
  void* replacement = &Hook_CanListen;
  if (!WriteSlot(canListenSlot, &replacement, sizeof(replacement))) return false;
  InterlockedExchange(&g_joinPolicyInstalled, 1);
  return true;
}

bool InstallProductionLocalServerTrace(BYTE* base) {
  if (InterlockedCompareExchange(&g_productionLocalServerTraceInstalled, 0, 0) == 1) return true;
  void** acceptSlot = reinterpret_cast<void**>(base + (0x00CB9F68 - 0x00400000));
  void* original = base + (0x00871190 - 0x00400000);
  if (*acceptSlot == &Hook_LocalServerAccept) {
    InterlockedExchange(&g_productionLocalServerTraceInstalled, 1);
    return true;
  }
  if (*acceptSlot != original) {
    Log("production admission: local-server Accept signature mismatch; serialized joins unavailable");
    return false;
  }
  g_localServerAccept = reinterpret_cast<LocalServerAcceptFn>(original);
  void* replacement = &Hook_LocalServerAccept;
  if (!WriteSlot(acceptSlot, &replacement, sizeof(replacement))) return false;
  struct AdmissionCallHook {
    DWORD address;
    DWORD target;
    LocalServerAdmissionCheckFn* original;
    void* replacement;
  };
  AdmissionCallHook checks[] = {
      {0x0087120E, 0x008511C0, &g_localServerCapacityCheck,
       reinterpret_cast<void*>(&Hook_LocalServerCapacityCheck)},
      {0x00871221, 0x008622C0, &g_localServerSessionCheck,
       reinterpret_cast<void*>(&Hook_LocalServerSessionCheck)},
  };
  for (const auto& check : checks) {
    BYTE* call = base + (check.address - 0x00400000);
    LONG relative = 0;
    memcpy(&relative, call + 1, sizeof(relative));
    if (call[0] != 0xE8 || call + 5 + relative != base + (check.target - 0x00400000)) {
      Log("production admission: helper signature mismatch call=%08lX", check.address);
      return false;
    }
    *check.original = reinterpret_cast<LocalServerAdmissionCheckFn>(
        base + (check.target - 0x00400000));
    BYTE patched[5] = {0xE8};
    relative = static_cast<LONG>(static_cast<BYTE*>(check.replacement) - (call + 5));
    memcpy(patched + 1, &relative, sizeof(relative));
    if (!WriteSlot(call, patched, sizeof(patched))) return false;
  }
  Log("production admission: local-server Accept hook installed slot=%p original=%p", acceptSlot, original);
  InterlockedExchange(&g_productionLocalServerTraceInstalled, 1);
  return true;
}

bool InstallProductionEngineCapacity(BYTE* base) {
  if (InterlockedCompareExchange(&g_productionEngineCapacityInstalled, 0, 0) == 1) return true;
  if (!g_production || g_requestedPlayers != 4) return false;

  auto* onlinePlayers = reinterpret_cast<LONG*>(base + (0x00DDCAB0 - 0x00400000));
  auto* healthBarsOverride = reinterpret_cast<BYTE*>(base + (0x00DDCBA5 - 0x00400000));
  const DWORD hostStatusGate = reinterpret_cast<DWORD>(base + (0x00DDCC2F - 0x00400000));
  const DWORD onlinePlayersAddress = reinterpret_cast<DWORD>(onlinePlayers);
  const DWORD healthBarsOverrideAddress = reinterpret_cast<DWORD>(healthBarsOverride);

  const BYTE completionReader[] = {
      0x80, 0x3D, 0, 0, 0, 0, 0, 0x74, 0x10, 0x8B, 0x0D, 0, 0, 0, 0,
      0x85, 0xC9, 0x7E, 0x06, 0x3B, 0xCA, 0x74, 0x02, 0x32, 0xC0};
  BYTE expectedCompletion[sizeof(completionReader)];
  memcpy(expectedCompletion, completionReader, sizeof(expectedCompletion));
  memcpy(expectedCompletion + 2, &hostStatusGate, sizeof(hostStatusGate));
  memcpy(expectedCompletion + 11, &onlinePlayersAddress, sizeof(onlinePlayersAddress));
  BYTE* flowReader = base + (0x00854120 - 0x00400000);
  BYTE* syncReader = base + (0x008541E0 - 0x00400000);
  const BYTE bypass[9] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  const auto completionReady = [&](BYTE* reader) {
    return memcmp(reader, expectedCompletion, sizeof(expectedCompletion)) == 0 ||
        memcmp(reader, bypass, sizeof(bypass)) == 0;
  };

  const BYTE hudReader[] = {0x80, 0x3D, 0, 0, 0, 0, 0, 0x74, 0x10, 0x5E, 0xB8, 0x03, 0, 0, 0};
  BYTE expectedHud[sizeof(hudReader)];
  memcpy(expectedHud, hudReader, sizeof(expectedHud));
  memcpy(expectedHud + 2, &healthBarsOverrideAddress, sizeof(healthBarsOverrideAddress));
  BYTE* hud = base + (0x005C24C2 - 0x00400000);

  const LONG currentPlayers = *onlinePlayers;
  if (!completionReady(flowReader) || !completionReady(syncReader) ||
      memcmp(hud, expectedHud, sizeof(expectedHud)) != 0 || currentPlayers < 0 || currentPlayers > 4) {
    Log("production engine: four-player signatures unavailable count=%ld flow=%d sync=%d hud=%d",
        currentPlayers, completionReady(flowReader), completionReady(syncReader),
        memcmp(hud, expectedHud, sizeof(expectedHud)) == 0);
    return false;
  }

  if (memcmp(flowReader, bypass, sizeof(bypass)) != 0 &&
      !WriteSlot(flowReader, bypass, sizeof(bypass))) return false;
  if (memcmp(syncReader, bypass, sizeof(bypass)) != 0 &&
      !WriteSlot(syncReader, bypass, sizeof(bypass))) return false;
  // Retail serializes campaign joins. Start with host + first remote and raise this target in
  // Hook_CanListen as each additional distinct remote link is admitted.
  const LONG requested = 2;
  const BYTE enabled = 1;
  if (!WriteSlot(onlinePlayers, &requested, sizeof(requested)) ||
      !WriteSlot(healthBarsOverride, &enabled, sizeof(enabled))) return false;

  Log("production engine: mOnlineNumPlayers %ld->2 dynamic; client collection is four-wide; partner HUD slots enabled",
      currentPlayers);
  InterlockedExchange(&g_productionEngineCapacityInstalled, 1);
  return true;
}

DWORD WINAPI ProductionPatchThread(LPVOID) {
  BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  // The debug-override registrar initializes these globals during frontend startup.
  // Apply the production values afterward so registration cannot restore retail defaults.
  Sleep(3000);
  for (int wait = 0; wait < 1200; wait++) {
    if (InstallCampaignAdmissionCapacity(base) && InstallJoinPolicyTrace(base) &&
        InstallProductionLocalServerTrace(base) && InstallClientDataProbe(base) &&
        InstallProductionEngineCapacity(base)) {
      Log("production co-op: four-player campaign admission, collection, and HUD capacity installed");
      return 0;
    }
    Sleep(100);
  }
  Log("production co-op: native signatures never became available; admission remains disabled");
  return 0;
}

DWORD WINAPI DirectHostProbeThread(LPVOID) {
  // This opt-in harness route mirrors cP2PClient::BeginMatchmaking's debug branch. Instance zero
  // invokes the proven host side; later instances invoke the matching native client side after the
  // local host has had time to allocate its four records.
  const bool isHost = g_instance == 0;
  Sleep(isHost ? 20000 : 24000);
  MarkDirectProbeReady();
  int ready = ReadyBusInstanceCount();
  for (int wait = 0; ready < g_testInstances && wait < 120; wait++) {
    Sleep(250);
    ready = ReadyBusInstanceCount();
  }
  Log("direct p2p: probe-ready synchronization ready=%d expected=%d", ready, g_testInstances);
  if (!isHost && g_sequentialJoins) {
    bool priorConfirmed = PriorHarnessPeersConfirmed();
    for (int wait = 0; !priorConfirmed && wait < 450; wait++) {
      Sleep(100);
      priorConfirmed = PriorHarnessPeersConfirmed();
    }
    Log("direct client: sequential admission instance=%d priorNativeConfirmed=%d", g_instance, priorConfirmed);
    if (!priorConfirmed) {
      Log("direct client: sequential join barrier failed; probe cancelled");
      return 0;
    }
  }
  if (isHost) {
    int live = LiveBusInstanceCount();
    for (int wait = 0; live < g_testInstances && wait < 80; wait++) {
      Sleep(250);
      live = LiveBusInstanceCount();
    }
    Log("direct host: roster synchronization live=%d expected=%d", live, g_testInstances);
  }
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  if (!InstallSyntheticOnlineCleanupGuard(base)) {
    Log("direct p2p: synthetic online cleanup guard unavailable after unpack; probe cancelled");
    return 0;
  }
  // Baseline native packet ordering while validating server construction.
  BYTE* beginDirect = base + (0x00886450 - 0x00400000);
  const BYTE expected[] = {0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0xA1};
  if (memcmp(beginDirect, expected, sizeof(expected)) != 0) {
    Log("direct host: BeginDirectP2PGame signature mismatch; probe cancelled");
    return 0;
  }
  void* online = nullptr;
  __try {
    online = *reinterpret_cast<void**>(base + (0x00E5F428 - 0x00400000));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct host: cOnline singleton pointer unreadable; probe cancelled");
    return 0;
  }
  if (!online) {
    Log("direct host: cOnline singleton is null after startup delay; probe cancelled");
    return 0;
  }
  void** onlineVtable = *reinterpret_cast<void***>(online);
  if (onlineVtable[3] != base + (0x00880970 - 0x00400000)) {
    Log("direct p2p: online update signature mismatch; probe cancelled");
    return 0;
  }
  g_onlineUpdate = reinterpret_cast<OnlineUpdateFn>(onlineVtable[3]);
  g_probeUpdateThread = GetCurrentThreadId();
  void* updateReplacement = &Hook_OnlineUpdate;
  if (!WriteSlot(onlineVtable + 3, &updateReplacement, sizeof(updateReplacement))) return 0;
  Log("direct p2p: serialized online update ownerThread=%lu", g_probeUpdateThread);
  if (!ConfigureHarnessCampaign(online, isHost)) return 0;
  if (!InstallCampaignAdmissionCapacity(base)) return 0;
  if (isHost && !InstallJoinPolicyTrace(base)) return 0;
  // cP2PClient::Connect copies this vanilla global into cLink::tInitInfo::mBypassFirstParty.
  // The local harness has no real Steam matchmaking session, so use the engine's own direct-link
  // route instead of manufacturing a first-party session around the synthetic peers.
  BYTE* bypassFirstParty = base + (0x00DDCC2F - 0x00400000);
  const BYTE oldBypassFirstParty = *bypassFirstParty;
  const BYTE enabled = 1;
  if (!WriteSlot(bypassFirstParty, &enabled, sizeof(enabled))) {
    Log("direct p2p: failed to enable native bypass-first-party flag");
    return 0;
  }
  Log("direct p2p: native bypass-first-party flag %u -> %u", oldBypassFirstParty, *bypassFirstParty);
  if (!InstallDirectClientConnectProbe(base) || !InstallLinkGetOrCreateTrace(base) ||
      !InstallPeerOwnerCreateTrace(base) || !InstallLinkInitTrace(base)) return 0;
  if (isHost) {
    BYTE* hostWait = base + (0x00886620 - 0x00400000);
    const BYTE expectedHostWait[] = {0x80, 0xBC, 0x24, 0x18, 0x01, 0x00, 0x00, 0x00,
                                     0x0F, 0x84, 0xBE, 0x00, 0x00, 0x00};
    const BYTE skipHostWait[] = {0xE9, 0xC7, 0x00, 0x00, 0x00, 0x90, 0x90,
                                 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (memcmp(hostWait, expectedHostWait, sizeof(expectedHostWait)) != 0 ||
        !WriteSlot(hostWait, skipHostWait, sizeof(skipHostWait))) {
      Log("direct p2p: host-readiness wait signature mismatch; probe cancelled");
      return 0;
    }
    Log("direct p2p: foreground-dependent host-readiness wait bypassed for probe");
  }
  Log("direct p2p: invoking BeginDirectP2PGame online=%p gameType=COOP(1) host=%d", online, isHost);
  using BeginDirectP2PGameFn = void (__thiscall*)(void*, int, bool);
  __try {
    reinterpret_cast<BeginDirectP2PGameFn>(beginDirect)(online, 1, isHost);
    Log("direct p2p: BeginDirectP2PGame returned host=%d", isHost);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("direct p2p: BeginDirectP2PGame raised exception host=%d code=%08lX", isHost, GetExceptionCode());
  }
  __try {
    BYTE* p2p = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(online) + 0xD8);
    void* server = p2p ? *reinterpret_cast<void**>(p2p + 0x34) : nullptr;
    Log("direct p2p: post-begin host=%d p2p=%p server=%p client=%p topology=%p", isHost, p2p, server,
        p2p ? *reinterpret_cast<void**>(p2p + 0x38) : nullptr,
        p2p ? *reinterpret_cast<void**>(p2p + 0x3C) : nullptr);
    if (!isHost && p2p) {
      BYTE* client = *reinterpret_cast<BYTE**>(p2p + 0x38);
      BYTE* topology = *reinterpret_cast<BYTE**>(p2p + 0x3C);
      void* remoteServer = topology ? *reinterpret_cast<void**>(topology + 0x7C) : nullptr;
      Log("direct client: post-connect mClient=%p remoteServer=%p",
          client ? *reinterpret_cast<void**>(client + 0xB4) : nullptr,
          remoteServer);
      if (remoteServer) InstallRemoteServerTeardownTrace(remoteServer);
      if (client) {
        // BeginDirectP2PGame uses cOnline's virtual slot 3 while waiting. Pump that same outer update
        // so topology, link, connection, traffic and P2P-client managers all advance together.
        void** vtable = *reinterpret_cast<void***>(online);
        using OnlineUpdateFn = void (__thiscall*)(void*, float);
        auto update = reinterpret_cast<OnlineUpdateFn>(vtable[3]);
        void* tracedLink = g_directProbeLink;
        LONG linkState = ReadLinkState(tracedLink);
        LONG linkConnectionState = ReadLinkConnectionState(tracedLink);
        void* linkEndpoint = ReadLinkEndpoint(tracedLink);
        LONG linkNodeCount = ReadLinkNodeCount(tracedLink);
        if (tracedLink) LogLinkState("baseline", tracedLink);
        Log("direct client: pumping cOnline update online=%p vtable=%p function=%p", online, vtable, update);
        for (int tick = 0; ; tick++) {
          update(online, 1.0f / 60.0f);
          TryActivateCampaignActors(base, p2p);
          TryInitializeConnectionMesh(base, p2p);
          TrySignalClientFlowCommand(base, p2p);
          TryRequestClientDataTransfer(base, p2p);
          if (tick % 60 == 0) LogProbeHealth(p2p);
          if (InterlockedCompareExchange(&g_syntheticTransportDetached, 0, 0)) {
            PumpDirectProbeConnection(g_directProbeLink, tick);
          }
          void* currentRemote = topology ? *reinterpret_cast<void**>(topology + 0x7C) : nullptr;
          if (currentRemote != remoteServer) {
            Log("direct client: remoteServer transition tick=%d old=%p new=%p", tick, remoteServer, currentRemote);
            remoteServer = currentRemote;
            if (remoteServer) InstallRemoteServerTeardownTrace(remoteServer);
          }
          if (currentRemote) {
            void* currentLink = g_directProbeLink;
            const LONG currentState = ReadLinkState(currentLink);
            const LONG currentConnectionState = ReadLinkConnectionState(currentLink);
            void* currentEndpoint = ReadLinkEndpoint(currentLink);
            const LONG currentNodeCount = ReadLinkNodeCount(currentLink);
            if (currentLink != tracedLink || currentState != linkState ||
                currentConnectionState != linkConnectionState || currentEndpoint != linkEndpoint ||
                currentNodeCount != linkNodeCount) {
              Log("direct client: link transition tick=%d oldLink=%p oldState=%ld oldConnectionState=%ld "
                  "oldEndpoint=%p oldNodes=%ld newLink=%p newState=%ld newConnectionState=%ld "
                  "newEndpoint=%p newNodes=%ld",
                  tick, tracedLink, linkState, linkConnectionState, linkEndpoint, linkNodeCount,
                  currentLink, currentState, currentConnectionState, currentEndpoint, currentNodeCount);
              tracedLink = currentLink;
              linkState = currentState;
              linkConnectionState = currentConnectionState;
              linkEndpoint = currentEndpoint;
              linkNodeCount = currentNodeCount;
              if (tracedLink) LogLinkState("transition", tracedLink);
            }
          }
          Sleep(16);
        }
        Log("direct client: update pump complete mClient=%p remoteServer=%p",
            *reinterpret_cast<void**>(client + 0xB4),
            topology ? *reinterpret_cast<void**>(topology + 0x7C) : nullptr);
      }
    }
    if (isHost && p2p && !server) {
      if (!InstallStarTopologyReuseProbe(base)) return 0;
      __declspec(align(16)) BYTE hardwareMessage[0xE8] = {};
      using HwMessageCtorFn = void* (__thiscall*)(void*);
      using HwMessageDtorFn = void (__thiscall*)(void*);
      using InitAsServerFn = bool (__thiscall*)(void*, int, const unsigned long long*, const long long*, int,
                                                const void*);
      reinterpret_cast<HwMessageCtorFn>(base + (0x0084B830 - 0x00400000))(hardwareMessage);
      const unsigned long long peerId = LocalSteamId(g_instance);
      const auto* matchId = reinterpret_cast<const long long*>(base + (0x00CB6BB8 - 0x00400000));
      Log("direct host: first-party session incomplete; invoking native cP2P::InitAsServer count=%d peer=%08lX%08lX",
          g_requestedPlayers, static_cast<DWORD>(peerId >> 32), static_cast<DWORD>(peerId));
      const bool initialized = reinterpret_cast<InitAsServerFn>(base + (0x0087E320 - 0x00400000))(
          p2p, g_requestedPlayers, &peerId, matchId, 1, hardwareMessage);
      server = *reinterpret_cast<void**>(p2p + 0x34);
      Log("direct host: InitAsServer result=%d server=%p", initialized, server);
      const bool transportReady = initialized && server &&
          (g_stockTransport || InstallFourEndpointReliableLayerProbe(base));
      *reinterpret_cast<DWORD*>(hardwareMessage) = reinterpret_cast<DWORD>(base + (0x00CABF90 - 0x00400000));
      reinterpret_cast<HwMessageDtorFn>(base + (0x00A4DB40 - 0x00400000))(hardwareMessage);
      BYTE* hostClient = *reinterpret_cast<BYTE**>(p2p + 0x38);
      BYTE* user = hostClient ? *reinterpret_cast<BYTE**>(hostClient + 0x400) : nullptr;
      if (user) {
        void** userTable = *reinterpret_cast<void***>(user);
        using GetSessionInfoFn = BYTE* (__thiscall*)(void*);
        BYTE* info = reinterpret_cast<GetSessionInfoFn>(userTable[33])(user);
        Log("direct host: session policy user=%p getter=%p info=%p type=%ld privateSlots=%ld",
            user, userTable[33], info, info ? *reinterpret_cast<LONG*>(info + 0x20) : -1,
            info ? *reinterpret_cast<LONG*>(info + 0x30) : -1);
      }
      const bool localClientJoined = transportReady && hostClient &&
          g_p2pClientConnect(hostClient, &peerId, 0, false);
      Log("direct host: native local-client join result=%d client=%p localNode=%p",
          localClientJoined, hostClient,
          hostClient ? *reinterpret_cast<void**>(hostClient + 0x90) : nullptr);
      if (transportReady) {
        void** vtable = *reinterpret_cast<void***>(online);
        using OnlineUpdateFn = void (__thiscall*)(void*, float);
        auto update = reinterpret_cast<OnlineUpdateFn>(vtable[3]);
        Log("direct host: pumping cOnline with native membership online=%p function=%p", online, update);
        for (int tick = 0; ; tick++) {
          update(online, 1.0f / 60.0f);
          TryActivateCampaignActors(base, p2p);
          TryInitializeConnectionMesh(base, p2p);
          TrySignalClientFlowCommand(base, p2p);
          TryRequestClientDataTransfer(base, p2p);
          TryStartHostStateTransfer(base, p2p);
          TryFinalizeHostDataTransfer(base, p2p);
          if (tick % 60 == 0) LogProbeHealth(p2p);
          if (InterlockedCompareExchange(&g_syntheticTransportDetached, 0, 0)) {
            PumpDirectProbeConnection(g_directProbeLink, tick);
          }
          if (tick < 4 || tick % 60 == 0) Log("direct host: roster update tick=%d", tick);
          Sleep(16);
        }
        Log("direct host: native roster update pump complete");
      }
    }
  } __except (CaptureNativeHandoffException(GetExceptionInformation())) {
    Log("direct p2p: native handoff raised exception host=%d code=%08lX", isHost, GetExceptionCode());
  }
  return 0;
}

void ProbeFourPlayerEngine() {
  // These probes only observe the unpacked vanilla image. They identify the retail equivalents of
  // OTR's cDebugOverride::mOnlineNumPlayers registration and cOnlineAI::GetNumActivePlayers.
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  InstallConnectionListenerRearmProbe(base);
  if (g_lobbyFrontendProbe && !g_lobbyDataParse) InstallLobbyCandidateTrace(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  constexpr DWORD kOnlineNumPlayersRva = 0x00CA9FBC - 0x00400000;
  constexpr DWORD kNumHealthBarsRva = 0x00CAA3E8 - 0x00400000;
  constexpr DWORD kOverrideHealthBarsRva = 0x00CA8BF0 - 0x00400000;
  const BYTE* setting = base + kOnlineNumPlayersRva;
  __try {
    if (strcmp(reinterpret_cast<const char*>(setting), "online_num_players") != 0) {
      Log("engine probe: online_num_players string not mapped at expected RVA");
      return;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("engine probe: online_num_players string unreadable");
    return;
  }

  const DWORD settingAddress = reinterpret_cast<DWORD>(setting);
  const DWORD numHealthBarsAddress = reinterpret_cast<DWORD>(base + kNumHealthBarsRva);
  const DWORD overrideHealthBarsAddress = reinterpret_cast<DWORD>(base + kOverrideHealthBarsRva);
  const BYTE getterPattern[] = {
      0x56, 0xBE, 1, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x74, 0,
      0x8B, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x8B, 0x10, 0x8B, 0xC8,
      0x8B, 0x42, 0x74, 0xFF, 0xD0, 0x85, 0xC0, 0x74, 0, 0x8B, 0xC8, 0x5E, 0xE9};
  const char getterMask[] = "xxxxxxx????xxx?xx????x????xxxxxxxxxxxx?xxxx";
  const BYTE attributesPattern[] = {
      0x53, 0x55, 0x56, 0x57, 0x6A, 0x02, 0x6A, 0x00, 0x6A, 0x19, 0x68,
      0, 0, 0, 0, 0x8B, 0xE9, 0xE8};
  const char attributesMask[] = "xxxxxxxxxxx????xxx";
  const BYTE coopUserPattern[] = {
      0x56, 0x57, 0x8B, 0xF1, 0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x74, 0x30,
      0x8B, 0x46, 0x08, 0x33, 0xC9, 0x39, 0x88, 0x90, 0, 0, 0, 0x0F, 0x94, 0xC1};
  const char coopUserMask[] = "xxxxx????xxx?xxxxxxx????xxx";
  const BYTE addHumanPattern[] = {0x8D, 0x4E, 0x0C, 0x83, 0xCF, 0xFF, 0x8B, 0x11, 0x85, 0xD2, 0x74, 0};
  const char addHumanMask[] = "xxxxxxxxxxx?";
  const BYTE getHumanPattern[] = {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x44, 0x81, 0, 0xC2, 0x04, 0x00};
  const char getHumanMask[] = "xxxxxxx?xxx";
  int settingRefs = 0;
  int getterMatches = 0;
  int attributesMatches = 0;
  int coopUserMatches = 0;
  int addHumanMatches = 0;
  int getHumanMatches = 0;
  DWORD onlineStorage = 0;
  DWORD numHealthBarsStorage = 0;
  DWORD overrideHealthBarsStorage = 0;
  auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; index++, section++) {
    if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    BYTE* begin = base + section->VirtualAddress;
    const size_t size = section->Misc.VirtualSize;
    for (size_t offset = 0; offset + sizeof(getterPattern) <= size; offset++) {
      BYTE* cursor = begin + offset;
      DWORD immediate = 0;
      memcpy(&immediate, cursor, sizeof(immediate));
      if (immediate == settingAddress) {
        settingRefs++;
        Log("engine probe: online_num_players ref #%d at VA=%08lX", settingRefs,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)));
        LogBytes(cursor >= begin + 12 ? cursor - 12 : cursor, 48);
        // The registrar returns this setting's value in EAX; vanilla stores it immediately before
        // registering the following setting: call; push next string; mov ecx,esi; mov [absolute],eax.
        if (offset >= 1 && cursor[-1] == 0x68 && offset + 23 < size && cursor[4] == 0x8B &&
            cursor[5] == 0xCE && cursor[6] == 0xE8 && cursor[11] == 0x68 &&
            cursor[16] == 0x8B && cursor[17] == 0xCE && cursor[18] == 0xA3) {
          DWORD storage = 0;
          memcpy(&storage, cursor + 19, sizeof(storage));
          onlineStorage = storage;
          LONG current = *reinterpret_cast<volatile LONG*>(storage);
          Log("engine probe: inferred mOnlineNumPlayers storage VA=%08lX current=%ld", storage, current);
          if (g_requestedPlayers >= 1 && g_requestedPlayers <= 4) {
            const LONG requested = g_requestedPlayers;
            if (WriteSlot(reinterpret_cast<void*>(storage), &requested, sizeof(requested)))
              Log("engine override: mOnlineNumPlayers %ld -> %ld", current, requested);
            else
              Log("engine override: FAILED to write mOnlineNumPlayers");
          }
        }
      }
      if (immediate == numHealthBarsAddress && offset >= 1 && cursor[-1] == 0x68 && offset + 23 < size &&
          cursor[4] == 0x8B && cursor[5] == 0xCE && cursor[6] == 0xE8 && cursor[11] == 0x68 &&
          cursor[16] == 0x8B && cursor[17] == 0xCE && cursor[18] == 0xA3) {
        memcpy(&numHealthBarsStorage, cursor + 19, sizeof(numHealthBarsStorage));
        const LONG current = *reinterpret_cast<volatile LONG*>(numHealthBarsStorage);
        Log("engine probe: inferred mNumPlayerHealthBars storage VA=%08lX current=%ld", numHealthBarsStorage, current);
        if (g_requestedPlayers >= 1 && g_requestedPlayers <= 4) {
          const LONG requested = g_requestedPlayers;
          WriteSlot(reinterpret_cast<void*>(numHealthBarsStorage), &requested, sizeof(requested));
          Log("engine override: mNumPlayerHealthBars %ld -> %ld", current, requested);
        }
      }
      if (immediate == overrideHealthBarsAddress && offset >= 1 && cursor[-1] == 0x68 && offset + 28 < size &&
          cursor[4] == 0x8B && cursor[5] == 0xCE && cursor[6] == 0xE8 && cursor[11] == 0x68 &&
          cursor[16] == 0x8B && cursor[17] == 0xCE && cursor[18] == 0xA2) {
        memcpy(&overrideHealthBarsStorage, cursor + 19, sizeof(overrideHealthBarsStorage));
        const BYTE current = *reinterpret_cast<volatile BYTE*>(overrideHealthBarsStorage);
        Log("engine probe: inferred override_player_health_bars storage VA=%08lX current=%u",
            overrideHealthBarsStorage, current);
        if (g_requestedPlayers >= 1 && g_requestedPlayers <= 4) {
          const BYTE enabled = 1;
          WriteSlot(reinterpret_cast<void*>(overrideHealthBarsStorage), &enabled, sizeof(enabled));
          Log("engine override: override_player_health_bars %u -> 1", current);
        }
      }
      if (MatchMasked(cursor, getterPattern, getterMask)) {
        getterMatches++;
        Log("engine probe: GetNumActivePlayers-shaped function #%d at VA=%08lX", getterMatches,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)));
        LogBytes(cursor, 48);
      }
      if (MatchMasked(cursor, attributesPattern, attributesMask)) {
        DWORD bytes = 0;
        memcpy(&bytes, cursor + 11, sizeof(bytes));
        attributesMatches++;
        Log("engine probe: CreatePlayerAttributes-shaped function #%d at VA=%08lX allocation=%lu",
            attributesMatches, static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)), bytes);
        LogBytes(cursor, 96);
      }
      if (MatchMasked(cursor, coopUserPattern, coopUserMask)) {
        coopUserMatches++;
        Log("engine probe: GetCoopUserPlayer-shaped function #%d at VA=%08lX", coopUserMatches,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)));
        LogBytes(cursor, 64);
      }
      if (MatchMasked(cursor, addHumanPattern, addHumanMask)) {
        addHumanMatches++;
        BYTE* start = offset >= 24 ? cursor - 24 : cursor;
        Log("engine probe: AddHumanActor slot-scan #%d at VA=%08lX", addHumanMatches,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)));
        LogBytes(start, 96);
      }
      if (MatchMasked(cursor, getHumanPattern, getHumanMask)) {
        getHumanMatches++;
        Log("engine probe: GetHumanActor accessor #%d at VA=%08lX array-offset=%u", getHumanMatches,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(cursor)), cursor[7]);
        LogBytes(offset >= 24 ? cursor - 24 : cursor, 80);
      }
    }
  }
  if (onlineStorage && !numHealthBarsStorage) {
    numHealthBarsStorage = onlineStorage - 0xC0;  // same cDebugOverride static-field delta as OTR
    Log("engine probe: layout candidate mNumPlayerHealthBars VA=%08lX current=%ld", numHealthBarsStorage,
        *reinterpret_cast<volatile LONG*>(numHealthBarsStorage));
  }
  if (onlineStorage && !overrideHealthBarsStorage) {
    overrideHealthBarsStorage = onlineStorage + 0xF5;  // same cDebugOverride static-field delta as OTR
    Log("engine probe: layout candidate override_player_health_bars VA=%08lX current=%u", overrideHealthBarsStorage,
        *reinterpret_cast<volatile BYTE*>(overrideHealthBarsStorage));
  }
  if (onlineStorage && g_requestedPlayers == 4) {
    // Both cP2PServer::ClientCollectionComplete overloads consult mOnlineNumPlayers only while the
    // online_display_host_status master debug gate is enabled. Bypass only those two guards; enabling the
    // global byte would also activate dozens of unrelated debug-display paths.
    const DWORD hostStatusGate = reinterpret_cast<DWORD>(base + (0x00DDCC2F - 0x00400000));
    const BYTE completionReader[] = {
        0x80, 0x3D, 0, 0, 0, 0, 0, 0x74, 0x10, 0x8B, 0x0D, 0, 0, 0, 0,
        0x85, 0xC9, 0x7E, 0x06, 0x3B, 0xCA, 0x74, 0x02, 0x32, 0xC0};
    BYTE expected[sizeof(completionReader)];
    memcpy(expected, completionReader, sizeof(expected));
    memcpy(expected + 2, &hostStatusGate, sizeof(hostStatusGate));
    memcpy(expected + 11, &onlineStorage, sizeof(onlineStorage));
    BYTE* flowReader = base + (0x00854120 - 0x00400000);
    BYTE* syncReader = base + (0x008541E0 - 0x00400000);
    if (memcmp(flowReader, expected, sizeof(expected)) == 0 &&
        memcmp(syncReader, expected, sizeof(expected)) == 0) {
      const BYTE bypass[9] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
      if (WriteSlot(flowReader, bypass, sizeof(bypass)) && WriteSlot(syncReader, bypass, sizeof(bypass)))
        Log("engine override: mOnlineNumPlayers enabled in both client-collection completion paths");
      else
        Log("engine override: FAILED to patch mOnlineNumPlayers completion guards");
    } else {
      Log("engine override: client-collection completion signatures mismatch; count gate left unchanged");
    }
  }
  if (g_directP2PProbe) InstallClientDataProbe(base);
  if (overrideHealthBarsStorage && g_requestedPlayers == 4) {
    // Vanilla's TIR-capable HUD reader is: cmp byte ptr [override],0; je ...; mov eax,3.
    const BYTE hudReader[] = {0x80, 0x3D, 0, 0, 0, 0, 0, 0x74, 0x10, 0x5E, 0xB8, 0x03, 0, 0, 0};
    BYTE expected[sizeof(hudReader)];
    memcpy(expected, hudReader, sizeof(expected));
    memcpy(expected + 2, &overrideHealthBarsStorage, sizeof(overrideHealthBarsStorage));
    BYTE* reader = base + (0x005C24C2 - 0x00400000);
    if (memcmp(reader, expected, sizeof(expected)) == 0) {
      const BYTE enabled = 1;
      const BYTE current = *reinterpret_cast<volatile BYTE*>(overrideHealthBarsStorage);
      WriteSlot(reinterpret_cast<void*>(overrideHealthBarsStorage), &enabled, sizeof(enabled));
      Log("engine override: override_player_health_bars %u -> 1 (three partner HUD slots)", current);
    } else {
      Log("engine override: HUD reader signature mismatch; override left unchanged");
    }
  }
  const DWORD globals[] = {onlineStorage, numHealthBarsStorage, overrideHealthBarsStorage};
  const char* globalNames[] = {"mOnlineNumPlayers", "mNumPlayerHealthBars", "override_player_health_bars"};
  for (int global = 0; global < 3; global++) {
    if (!globals[global]) continue;
    int refs = 0;
    section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; index++, section++) {
      if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
      BYTE* begin = base + section->VirtualAddress;
      const size_t size = section->Misc.VirtualSize;
      for (size_t offset = 0; offset + sizeof(DWORD) <= size; offset++) {
        DWORD immediate = 0;
        memcpy(&immediate, begin + offset, sizeof(immediate));
        if (immediate != globals[global]) continue;
        refs++;
        Log("engine probe: %s code ref #%d at VA=%08lX", globalNames[global], refs,
            static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(begin + offset)));
        LogBytes(offset >= 12 ? begin + offset - 12 : begin + offset, 48);
      }
    }
  }
  Log("engine probe complete: setting refs=%d getter matches=%d attributes=%d coop-user=%d add-human=%d get-human=%d",
      settingRefs, getterMatches, attributesMatches, coopUserMatches, addHumanMatches, getHumanMatches);

  if (g_instance == 0 && nt->OptionalHeader.SizeOfImage > 0 && nt->OptionalHeader.SizeOfImage < 0x10000000) {
    wchar_t outputRoot[MAX_PATH], absoluteRoot[MAX_PATH];
    const DWORD rootLength = GetEnvironmentVariableW(L"DR2_COOP_DUMP_ROOT", outputRoot, MAX_PATH);
    wchar_t path[MAX_PATH];
    const DWORD absoluteLength = rootLength && rootLength < MAX_PATH ?
        GetFullPathNameW(outputRoot, MAX_PATH, absoluteRoot, nullptr) : 0;
    if (!absoluteLength || absoluteLength + wcslen(L"\\coop_unpacked_image.bin") + 1 > MAX_PATH ||
        _wcsicmp(outputRoot, absoluteRoot) != 0 ||
        swprintf_s(path, MAX_PATH, L"%s\\coop_unpacked_image.bin", absoluteRoot) < 0) {
      Log("engine probe: unpacked image skipped; explicit absolute DR2_COOP_DUMP_ROOT required");
      return;
    }
    FILE* dump = nullptr;
    if (_wfopen_s(&dump, path, L"wb") == 0 && dump) {
      BYTE* zeros = static_cast<BYTE*>(calloc(1, nt->OptionalHeader.SizeOfImage));
      if (zeros) {
        memcpy(zeros, base, nt->OptionalHeader.SizeOfHeaders);
        auto* dumpSection = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; index++, dumpSection++) {
          const size_t size = dumpSection->Misc.VirtualSize;
          if (dumpSection->VirtualAddress + size > nt->OptionalHeader.SizeOfImage) continue;
          __try {
            memcpy(zeros + dumpSection->VirtualAddress, base + dumpSection->VirtualAddress, size);
          } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("engine probe: skipped unreadable section %.8s", dumpSection->Name);
          }
        }
        fwrite(zeros, 1, nt->OptionalHeader.SizeOfImage, dump);
        Log("engine probe: dumped unpacked mapped image size=%lu path=%ls", nt->OptionalHeader.SizeOfImage, path);
        free(zeros);
      }
      fclose(dump);
    } else {
      Log("engine probe: failed to create unpacked image dump at %ls", path);
    }
  }
}

DWORD WINAPI EngineProbeThread(LPVOID) {
  // SteamStub has completed code setup by the time the title renderer initializes. Keep this probe
  // independent of one exact compiler encoding and log the known IsValidUser site for provenance.
  BYTE* marker = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)) + (0x007A1B20 - 0x00400000);
  Sleep(3000);
  Log("engine probe: IsValidUser site snapshot");
  LogBytes(marker, 24);
  ProbeFourPlayerEngine();
  InstallClothingCapacityProbe(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)));
  InstallNativeTransitionTrace(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)));
  InstallNfsOwnershipProbe(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)));
  InstallLoaderWaitTrace(reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)));
  return 0;
}

DWORD WINAPI PatchThread(LPVOID) {
  // The loader may still be resolving imports during DllMain; keep re-checking the IAT for the first minute.
  for (int i = 0; i < 600; i++) {
    PatchImports();
    Sleep(100);
  }
  return 0;
}

int ArgumentInt(const wchar_t* commandLine, const wchar_t* prefix, int fallback) {
  const wchar_t* found = wcsstr(commandLine, prefix);
  return found ? _wtoi(found + wcslen(prefix)) : fallback;
}

}  // namespace

// Called from the runtime's DllMain after its own configuration is loaded.
void Initialize(const wchar_t* root) {
  wcsncpy_s(g_root, root, _TRUNCATE);
  wchar_t ini[MAX_PATH];
  swprintf(ini, MAX_PATH, L"%scase_zero_campaign.ini", root);
  const wchar_t* commandLine = GetCommandLineW();
  g_trace = GetPrivateProfileIntW(L"Coop", L"Trace", 0, ini) != 0 || wcsstr(commandLine, L"-cooptrace") != nullptr;
  g_harness = wcsstr(commandLine, L"-coopinstance=") != nullptr;
  wchar_t productionIni[MAX_PATH];
  swprintf(productionIni, MAX_PATH, L"%sfour_player_coop.ini", root);
  g_production = !g_harness && GetFileAttributesW(productionIni) != INVALID_FILE_ATTRIBUTES &&
      GetPrivateProfileIntW(L"FourPlayerCoop", L"Enabled", 1, productionIni) != 0;
  g_baseGameDlcCompatibility = g_production &&
      GetPrivateProfileIntW(L"FourPlayerCoop", L"BaseGameDlcCompatibility", 1, productionIni) != 0;
  g_silent = g_harness && wcsstr(commandLine, L"-coopsilent") != nullptr;
  g_isolatedDesktop = g_harness && wcsstr(commandLine, L"-coopdesktop") != nullptr;
  g_instance = ArgumentInt(commandLine, L"-coopinstance=", 0);
  g_requestedPlayers = ArgumentInt(commandLine, L"-coopplayers=", g_production ? 4 : 0);
  g_testInstances = ArgumentInt(commandLine, L"-cooptestinstances=", 1);
  if (g_instance < 0 || g_instance > 3) g_instance = 0;
  if (g_testInstances < 1 || g_testInstances > 4) g_testInstances = 1;
  g_directP2PProbe = g_harness &&
      ((g_instance == 0 && wcsstr(commandLine, L"-coopdirecthost") != nullptr) ||
       (g_instance > 0 && wcsstr(commandLine, L"-coopdirectclient") != nullptr));
  g_stockTransport = g_directP2PProbe && wcsstr(commandLine, L"-coopstocktransport") != nullptr;
  g_sequentialJoins = g_directP2PProbe && wcsstr(commandLine, L"-coopsequentialjoins") != nullptr;
  g_actorActivationProbe = g_harness && wcsstr(commandLine, L"-coopactoractivate") != nullptr;
  g_clientTransitionProbe = g_harness && wcsstr(commandLine, L"-coopclienttransition") != nullptr;
  g_hostStateTransferProbe = g_harness && wcsstr(commandLine, L"-coophoststatetransfer") != nullptr;
  g_nativeFlowProbe = g_harness && g_hostStateTransferProbe && wcsstr(commandLine, L"-coopnativeflowprobe") != nullptr;
  g_nfsOwnershipProbe = g_nativeFlowProbe && g_requestedPlayers == 4 &&
      wcsstr(commandLine, L"-coopnfsownershipprobe") != nullptr;
  g_jipBroadcastQueueProbe = g_nativeFlowProbe && g_requestedPlayers == 4 &&
      wcsstr(commandLine, L"-coopjipbroadcastqueue") != nullptr;
  g_pauseAcknowledgementProbe = g_nativeFlowProbe && g_requestedPlayers == 4 &&
      wcsstr(commandLine, L"-cooppauseackprobe") != nullptr;
  g_loaderWaitProbe = g_harness && wcsstr(commandLine, L"-cooploaderwaitprobe") != nullptr;
  g_privateInputProbe = g_harness && g_silent && wcsstr(commandLine, L"-coopprivatemouse") != nullptr;
  g_meshListenerProbe = g_harness && wcsstr(commandLine, L"-coopmeshlistenerprobe") != nullptr;
  g_clothingCapacityProbe = g_harness && wcsstr(commandLine, L"-coopclothingcapacityprobe") != nullptr;
  g_clothingVariantsProbe = g_clothingCapacityProbe && g_requestedPlayers == 4 &&
      wcsstr(commandLine, L"-coopclothingvariants") != nullptr;
  g_lobbyFrontendProbe = g_harness && wcsstr(commandLine, L"-cooplobbyfrontend") != nullptr;
  g_bridgeFourthPeer = g_directP2PProbe &&
      wcsstr(commandLine, L"-coopbridgefourth") != nullptr;
  if (!g_trace && !g_harness && !g_production) return;

  InitializeCriticalSection(&g_lock);
  if (g_harness) SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  for (int iface = 0; iface < kInterfaceCount; iface++)
    for (int slot = 0; slot < kSlots; slot++) g_thunkTables[iface][slot] = reinterpret_cast<void*>(kThunks[slot]);
  if (g_harness) {
    g_thunkTables[kMatchmaking][4] = reinterpret_cast<void*>(Fake_Matchmaking_RequestLobbyList);
    g_thunkTables[kMatchmaking][5] = reinterpret_cast<void*>(Fake_Matchmaking_AddStringFilter);
    g_thunkTables[kMatchmaking][6] = reinterpret_cast<void*>(Fake_Matchmaking_AddNumericalFilter);
    g_thunkTables[kMatchmaking][7] = reinterpret_cast<void*>(Fake_Matchmaking_AddNearValueFilter);
    g_thunkTables[kMatchmaking][8] = reinterpret_cast<void*>(Fake_Matchmaking_AddSlotsAvailableFilter);
    g_thunkTables[kMatchmaking][9] = reinterpret_cast<void*>(Fake_Matchmaking_AddDistanceFilter);
    g_thunkTables[kMatchmaking][10] = reinterpret_cast<void*>(Fake_Matchmaking_AddResultCountFilter);
    g_thunkTables[kMatchmaking][11] = reinterpret_cast<void*>(Fake_Matchmaking_AddCompatibleMembersFilter);
    g_thunkTables[kMatchmaking][12] = reinterpret_cast<void*>(Fake_Matchmaking_GetLobbyByIndex);
    g_thunkTables[kMatchmaking][13] = reinterpret_cast<void*>(Fake_Matchmaking_CreateLobby);
    g_thunkTables[kMatchmaking][14] = reinterpret_cast<void*>(Fake_Matchmaking_JoinLobby);
    g_thunkTables[kMatchmaking][15] = reinterpret_cast<void*>(Fake_Matchmaking_LeaveLobby);
    g_thunkTables[kMatchmaking][16] = reinterpret_cast<void*>(Fake_Matchmaking_InviteUserToLobby);
    g_thunkTables[kMatchmaking][17] = reinterpret_cast<void*>(Fake_Matchmaking_GetNumLobbyMembers);
    g_thunkTables[kMatchmaking][18] = reinterpret_cast<void*>(Fake_Matchmaking_GetLobbyMemberByIndex);
    g_thunkTables[kMatchmaking][19] = reinterpret_cast<void*>(Fake_Matchmaking_GetLobbyData);
    g_thunkTables[kMatchmaking][20] = reinterpret_cast<void*>(Fake_Matchmaking_SetLobbyData);
    g_thunkTables[kMatchmaking][31] = reinterpret_cast<void*>(Fake_Matchmaking_SetLobbyMemberLimit);
    g_thunkTables[kMatchmaking][32] = reinterpret_cast<void*>(Fake_Matchmaking_GetLobbyMemberLimit);
    g_thunkTables[kMatchmaking][35] = reinterpret_cast<void*>(Fake_Matchmaking_GetLobbyOwner);
    g_thunkTables[kNetworking][0] = reinterpret_cast<void*>(Fake_Networking_SendP2PPacket);
    g_thunkTables[kNetworking][1] = reinterpret_cast<void*>(Fake_Networking_IsP2PPacketAvailable);
    g_thunkTables[kNetworking][2] = reinterpret_cast<void*>(Fake_Networking_ReadP2PPacket);
    g_thunkTables[kNetworking][3] = reinterpret_cast<void*>(Fake_Networking_AcceptP2PSessionWithUser);
    g_thunkTables[kNetworking][4] = reinterpret_cast<void*>(Fake_Networking_CloseP2PSessionWithUser);
    g_thunkTables[kNetworking][5] = reinterpret_cast<void*>(Fake_Networking_CloseP2PChannelWithUser);
    g_thunkTables[kNetworking][6] = reinterpret_cast<void*>(Fake_Networking_GetP2PSessionState);
    g_thunkTables[kNetworking][7] = reinterpret_cast<void*>(Fake_Networking_AllowP2PPacketRelay);
    g_thunkTables[kNetworking][8] = reinterpret_cast<void*>(Fake_Networking_CreateListenSocket);
    g_thunkTables[kNetworking][9] = reinterpret_cast<void*>(Fake_Networking_CreateP2PConnectionSocket);
    g_thunkTables[kNetworking][10] = reinterpret_cast<void*>(Fake_Networking_CreateConnectionSocket);
    g_thunkTables[kNetworking][11] = reinterpret_cast<void*>(Fake_Networking_DestroySocket);
    g_thunkTables[kNetworking][12] = reinterpret_cast<void*>(Fake_Networking_DestroyListenSocket);
    g_thunkTables[kNetworking][13] = reinterpret_cast<void*>(Fake_Networking_SendDataOnSocket);
    g_thunkTables[kNetworking][14] = reinterpret_cast<void*>(Fake_Networking_IsDataAvailableOnSocket);
    g_thunkTables[kNetworking][15] = reinterpret_cast<void*>(Fake_Networking_RetrieveDataFromSocket);
    g_thunkTables[kNetworking][16] = reinterpret_cast<void*>(Fake_Networking_IsDataAvailable);
    g_thunkTables[kNetworking][17] = reinterpret_cast<void*>(Fake_Networking_RetrieveData);
    g_thunkTables[kNetworking][18] = reinterpret_cast<void*>(Fake_Networking_GetSocketInfo);
    g_thunkTables[kNetworking][19] = reinterpret_cast<void*>(Fake_Networking_GetListenSocketInfo);
    g_thunkTables[kNetworking][20] = reinterpret_cast<void*>(Fake_Networking_GetSocketConnectionType);
    g_thunkTables[kNetworking][21] = reinterpret_cast<void*>(Fake_Networking_GetMaxPacketSize);
    g_thunkTables[kUser][1] = reinterpret_cast<void*>(Fake_User_BLoggedOn);
    g_thunkTables[kUser][2] = reinterpret_cast<void*>(Fake_User_GetSteamID);
    g_thunkTables[kFriends][0] = reinterpret_cast<void*>(Fake_Friends_GetPersonaName);
    g_thunkTables[kFriends][3] = reinterpret_cast<void*>(Fake_Friends_GetFriendCount);
    g_thunkTables[kFriends][4] = reinterpret_cast<void*>(Fake_Friends_GetFriendByIndex);
    g_thunkTables[kFriends][5] = reinterpret_cast<void*>(Fake_Friends_GetFriendRelationship);
    g_thunkTables[kFriends][6] = reinterpret_cast<void*>(Fake_Friends_GetFriendPersonaState);
    g_thunkTables[kFriends][7] = reinterpret_cast<void*>(Fake_Friends_GetFriendPersonaName);
  } else if (g_production) {
    g_thunkTables[kMatchmaking][4] = reinterpret_cast<void*>(Mod_Matchmaking_RequestLobbyList);
    g_thunkTables[kMatchmaking][13] = reinterpret_cast<void*>(Mod_Matchmaking_CreateLobby);
    g_thunkTables[kMatchmaking][14] = reinterpret_cast<void*>(Mod_Matchmaking_JoinLobby);
    g_thunkTables[kMatchmaking][20] = reinterpret_cast<void*>(Mod_Matchmaking_SetLobbyData);
    g_thunkTables[kMatchmaking][31] = reinterpret_cast<void*>(Mod_Matchmaking_SetLobbyMemberLimit);
    g_thunkTables[kMatchmaking][33] = reinterpret_cast<void*>(Mod_Matchmaking_SetLobbyType);
    g_thunkTables[kFriends][7] = reinterpret_cast<void*>(Mod_Friends_GetFriendPersonaName);
    if (g_baseGameDlcCompatibility)
      g_thunkTables[kApps][coop_matchmaking::kAppsBIsDlcInstalledSlot] =
          reinterpret_cast<void*>(Mod_Apps_BIsDlcInstalled);
    if (g_baseGameDlcCompatibility)
      g_thunkTables[kApps][coop_matchmaking::kAppsBGetDlcDataByIndexSlot] =
          reinterpret_cast<void*>(Mod_Apps_BGetDLCDataByIndex);
  }
  wchar_t logPath[MAX_PATH];
  if (g_instance > 0) swprintf(logPath, MAX_PATH, L"%scoop_net.%d.log", root, g_instance);
  else swprintf(logPath, MAX_PATH, L"%scoop_net.log", root);
  g_log = _wfsopen(logPath, L"w", _SH_DENYWR);
  Log("co-op runtime loaded: trace=%d harness=%d production=%d silent=%d instance=%d requestedPlayers=%d pid=%lu",
      g_trace, g_harness, g_production, g_silent, g_instance, g_requestedPlayers, GetCurrentProcessId());
  if (g_directP2PProbe) Log("direct transport: stockLayout=%d", g_stockTransport);
  if (g_harness) StartLocalBus();
  PatchImports();
  if (g_production) {
    HANDLE productionPatches = CreateThread(nullptr, 0, ProductionPatchThread, nullptr, 0, nullptr);
    if (productionPatches) CloseHandle(productionPatches);
  }
    if (g_harness) {
      if (g_directP2PProbe && !g_stockTransport) {
        BYTE* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
      const bool managerReady = InstallFiveEndpointManagerProbe(base);
      const bool reliableReady = InstallFourEndpointReliableLayerProbe(base);
      if (!managerReady || !reliableReady) {
        HANDLE endpointCapacity = CreateThread(nullptr, 0, EarlyEndpointManagerPatchThread, nullptr, 0, nullptr);
        if (endpointCapacity) CloseHandle(endpointCapacity);
      }
    }
    HANDLE lobbyTest = CreateThread(nullptr, 0, LobbySelfTestThread, nullptr, 0, nullptr);
    if (lobbyTest) CloseHandle(lobbyTest);
    HANDLE engineProbe = CreateThread(nullptr, 0, EngineProbeThread, nullptr, 0, nullptr);
    if (engineProbe) CloseHandle(engineProbe);
    if (g_directP2PProbe) {
      HANDLE directP2P = CreateThread(nullptr, 0, DirectHostProbeThread, nullptr, 0, nullptr);
      if (directP2P) CloseHandle(directP2P);
    }
  }
  HANDLE thread = CreateThread(nullptr, 0, PatchThread, nullptr, 0, nullptr);
  if (thread) CloseHandle(thread);
}

bool IsHarness() { return g_harness; }
bool CampaignActivationPending() { return CampaignActivationPendingInternal(); }
void ActivateCampaignActorsOnCurrentThread() { ActivateCampaignActorsOnCurrentThreadInternal(); }
bool ConnectionMeshInitPending() { return ConnectionMeshInitPendingInternal(); }
void InitializeConnectionMeshOnCurrentThread() { InitializeConnectionMeshOnCurrentThreadInternal(); }
bool ConnectionListenerRearmPending() { return ConnectionListenerRearmPendingInternal(); }
void RearmConnectionListenerOnCurrentThread() { RearmConnectionListenerOnCurrentThreadInternal(); }
bool ClientTransitionPending() { return ClientTransitionPendingInternal(); }
void CompleteClientTransitionOnCurrentThread() { CompleteClientTransitionOnCurrentThreadInternal(); }
bool ClientDataTransferRequestPending() { return ClientDataTransferRequestPendingInternal(); }
void RequestClientDataTransferOnCurrentThread() { RequestClientDataTransferOnCurrentThreadInternal(); }
bool HostStateTransferPending() { return HostStateTransferPendingInternal(); }
void StartHostStateTransferOnCurrentThread() { StartHostStateTransferOnCurrentThreadInternal(); }
bool HostFlow7FinalizePending() { return HostFlow7FinalizePendingInternal(); }
void FinalizeHostFlow7OnCurrentThread() { FinalizeHostFlow7OnCurrentThreadInternal(); }
bool ClothingVariantsPending() { return ClothingVariantsPendingInternal(); }
void ApplyClothingVariantsOnCurrentThread() { ApplyClothingVariantsOnCurrentThreadInternal(); }
int Instance() { return g_instance; }
void TraceInput(int code, bool down) { Log("script input: DIK=%d %s", code, down ? "down" : "up"); }

}  // namespace coop

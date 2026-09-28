// Used by build.ps1 when the 4-player co-op project (4-player-coop/runtime/coop_net.cpp) is absent.
namespace coop {
void Initialize(const wchar_t*) {}
bool IsHarness() { return false; }
bool CampaignActivationPending() { return false; }
void ActivateCampaignActorsOnCurrentThread() {}
bool ClientTransitionPending() { return false; }
void CompleteClientTransitionOnCurrentThread() {}
bool HostStateTransferPending() { return false; }
void StartHostStateTransferOnCurrentThread() {}
int Instance() { return 0; }
void TraceInput(int, bool) {}
}

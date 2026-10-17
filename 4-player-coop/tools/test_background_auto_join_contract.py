from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "runtime" / "coop_net.cpp").read_text(encoding="utf-8")
PROXY_SOURCE = (ROOT.parent / "tools" / "case-zero-runtime" / "cz_runtime.cpp").read_text(encoding="utf-8")
PACKAGE_CONFIG = (ROOT / "package" / "four_player_coop.ini").read_text(encoding="utf-8")


def test_background_auto_join_is_disabled_by_default():
    assert 'L"BackgroundAutoJoin", 0' in SOURCE
    assert 'L"AutoJoinSingleCompatibleFallback", 0' in SOURCE
    active_lines = [line.strip() for line in PACKAGE_CONFIG.splitlines()
                    if line.strip() and not line.lstrip().startswith(";")]
    assert "BackgroundAutoJoin=1" not in active_lines


def test_background_auto_join_uses_protocol_filtered_lobby_discovery():
    assert "TickProductionBackgroundAutoJoin" in SOURCE
    assert "coop_matchmaking::kProtocolKey, coop_matchmaking::kProtocolValue" in SOURCE
    assert "kLobbyMatchListCallback = 510" in SOURCE
    assert "IsCallCompleted_t" in SOURCE
    assert "GetCallResult_t" in SOURCE
    assert "if (!utils && Real_Accessor[kUtils])" in SOURCE
    assert 'GetProcAddress(steamApi, "SteamUtils")' in SOURCE


def test_background_auto_join_targets_an_explicit_persona():
    assert 'L"AutoJoinPersona"' in SOURCE
    assert "_stricmp(persona, g_autoJoinPersona) == 0" in SOURCE


def test_uncached_owner_fallback_requires_one_protocol_compatible_lobby():
    assert "g_autoJoinSingleCompatibleFallback && compatibleCount == 1" in SOURCE
    assert 'matchmaking, lobby, "hostSteamID"' in SOURCE
    assert 'matchmaking, lobby, "HostId"' in SOURCE
    assert "_strtoui64(hostSteamId, nullptr, 10)" in SOURCE


def test_background_auto_join_preserves_dr2_native_frontend_transition():
    assert "kGameLobbyJoinRequestedCallback = 333" in SOURCE
    assert "g_gameLobbyJoinRequestedCallbacks" in SOURCE
    assert "coop_steam::RunCallback(g_gameLobbyJoinRequestedCallbacks[0], &request)" in SOURCE


def test_background_auto_join_can_drive_frontend_without_focus():
    assert "bool BackgroundAutoJoinEnabled()" in SOURCE
    assert 'L"%sfour_player_auto_input.txt"' in PROXY_SOURCE
    assert "coop::BackgroundAutoJoinEnabled()" in PROXY_SOURCE
    assert "ScriptInputThread" in PROXY_SOURCE


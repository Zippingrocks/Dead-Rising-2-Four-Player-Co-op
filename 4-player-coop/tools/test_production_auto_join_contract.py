from pathlib import Path


SCRIPT = Path(__file__).with_name("run-production-auto-join.ps1").read_text(encoding="utf-8-sig")


def test_requires_explicit_opt_in_and_exact_owned_process():
    assert "BackgroundAutoJoin=1 is required" in SCRIPT
    assert "Exactly one matching DR2 process is required" in SCRIPT
    assert "StartTime.ToUniversalTime().Ticks" in SCRIPT
    assert "The owned DR2 process exited or changed identity" in SCRIPT


def test_uses_private_file_input_without_foreground_control():
    assert "four_player_auto_input.txt" in SCRIPT
    assert "[IO.File]::WriteAllText($inputPath" in SCRIPT
    for forbidden in ("SendInput", "SetForegroundWindow", "AppActivate", "Start-Process", "Stop-Process"):
        assert forbidden not in SCRIPT


def test_cleanup_and_native_join_are_mandatory():
    assert "finally" in SCRIPT
    assert "Remove-Item -LiteralPath $inputPath" in SCRIPT
    assert "production matchmaking: accepted lobby=" in SCRIPT
    assert "No native JoinLobby acceptance appeared" in SCRIPT

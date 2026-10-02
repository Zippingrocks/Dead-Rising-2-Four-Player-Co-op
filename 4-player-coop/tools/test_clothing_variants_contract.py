import pathlib
import unittest


ROOT = pathlib.Path(__file__).parents[2]
RUNTIME = ROOT / "4-player-coop" / "runtime" / "coop_net.cpp"
PROXY = ROOT / "tools" / "case-zero-runtime" / "cz_runtime.cpp"
HARNESS = ROOT / "4-player-coop" / "tools" / "test-four-instances.ps1"


class ClothingVariantsContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = RUNTIME.read_text(encoding="utf-8-sig")
        cls.proxy = PROXY.read_text(encoding="utf-8-sig")
        cls.harness = HARNESS.read_text(encoding="utf-8-sig")

    def test_feature_is_explicit_and_requires_four_player_capacity(self):
        self.assertIn("-coopclothingvariants", self.runtime)
        self.assertIn("g_clothingCapacityProbe && g_requestedPlayers == 4", self.runtime)
        self.assertIn("[switch]$ClothingVariantsProbe", self.harness)
        self.assertIn("if ($ClothingVariantsProbe) { $arguments += ' -coopclothingvariants' }", self.harness)

    def test_only_players_three_and_four_are_actor_local_targets(self):
        self.assertIn("Every client fixes its local view of actor slot 4", self.runtime)
        self.assertIn("{2, 5u, 0}, {2, 5u, 3}, {2, 5u, 4}, {2, 5u, 5}, {2, 5u, 6}", self.runtime)
        self.assertIn("{3, 151u, 3}", self.runtime)
        self.assertIn("setClothingInfo(manager, actors[target.actor], target.part", self.runtime)
        self.assertNotRegex(self.runtime, r"\{[01],\s*\d+u,\s*\d+\}")
        self.assertNotIn("TriggerSpawnOutfitPartFn", self.runtime)
        self.assertNotIn("trigger(manager", self.runtime)

    def test_native_call_is_guarded_and_waits_for_idle_clothing_manager(self):
        self.assertIn("clothing + 0x08) != 2", self.runtime)
        self.assertIn("clothing + 0x3DC4) != 4", self.runtime)
        self.assertIn("clothing + 0x3DEC) != 0", self.runtime)
        self.assertIn("clothing + 0x3CD8) != 0", self.runtime)
        self.assertIn("now - g_clothingVariantCandidateSince < 1000", self.runtime)
        self.assertIn("reinterpret_cast<BYTE*>(0x0041FEF0)", self.runtime)
        self.assertIn("reinterpret_cast<BYTE*>(0x0051C9D0)", self.runtime)
        self.assertIn("actor-local clothing signatures mismatch", self.runtime)
        self.assertNotIn("reinterpret_cast<BYTE*>(0x00469E10)", self.runtime)
        self.assertNotIn("reinterpret_cast<BYTE*>(0x00469DC0)", self.runtime)

    def test_native_call_is_dispatched_on_game_window_thread(self):
        self.assertIn("bool ClothingVariantsPending();", self.proxy)
        self.assertIn("coop::ClothingVariantsPending()", self.proxy)
        self.assertIn("coop::ApplyClothingVariantsOnCurrentThread();", self.proxy)
        dispatcher = self.proxy.index("if (message == kHarnessActivateCampaign)")
        apply_call = self.proxy.index("coop::ApplyClothingVariantsOnCurrentThread();")
        self.assertGreater(apply_call, dispatcher)

    def test_only_player_four_gets_one_post_admission_reapply(self):
        self.assertIn("if (applyCount != 1", self.runtime)
        self.assertIn("now - g_clothingVariantFirstAppliedAt) < 60000", self.runtime)
        self.assertIn("scheduling post-admission P4 jacket reapply", self.runtime)
        self.assertIn("InterlockedIncrement(&g_clothingVariantApplyCount)", self.runtime)

if __name__ == "__main__":
    unittest.main()

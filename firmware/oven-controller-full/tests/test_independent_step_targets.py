import copy
import unittest
from test_telegram_tool import preview


class IndependentStepTargetsTest(unittest.TestCase):
    def test_all_adjacent_types_and_independent_targets_save_and_reload(self):
        device = preview.DemoDevice("full", speed=1)
        for first in ("ramp", "hold", "cooldown"):
            for second in ("ramp", "hold", "cooldown"):
                for target in (50, 100, 150):
                    with self.subTest(first=first, second=second, target=target):
                        programs = [dict(id="free-steps", name="Target indipendenti", steps=[
                            dict(type=first, target=100, rate=1, duration=10),
                            dict(type=second, target=target, rate=1, duration=10)])]
                        saved = device.request("POST", "recipes", body=dict(recipes=programs))
                        self.assertEqual(saved["recipes"], programs)
                        self.assertEqual(device.request("GET", "recipes")["recipes"], programs)

    def test_invalid_individual_values_still_rejected_without_changing_saved_programs(self):
        device = preview.DemoDevice("full", speed=1)
        original = copy.deepcopy(device.recipes)
        for step in (dict(type="hold", target=401, duration=10), dict(type="hold", target=80, duration=0),
                     dict(type="ramp", target=80, rate=0), dict(type="unknown", target=80, rate=1)):
            with self.assertRaises(preview.ApiError):
                device.request("POST", "recipes", body=dict(recipes=[dict(id="invalid", name="Valori errati", steps=[step])]))
            self.assertEqual(device.recipes, original)


if __name__ == "__main__": unittest.main()

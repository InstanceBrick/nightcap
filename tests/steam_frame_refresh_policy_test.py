import ast
import math
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / 'tools/steam_frame/run-nightcap-arm64.py'
module = ast.parse(source.read_text())
functions = [node for node in module.body if isinstance(node, ast.FunctionDef)
             and node.name in ('steam_profile_display_hz', 'steam_profile_frame_cap')]
scope = {'math': math}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), 'exec'), scope)
rate = scope['steam_profile_display_hz']
cap = scope['steam_profile_frame_cap']
APP = 'steam.app.123'


class RefreshPolicy(unittest.TestCase):
    def test_app_refresh_wins_over_startup_and_global_rates(self):
        self.assertEqual(rate(90, {'steamvr': {'preferredRefreshRate': 96},
                                   APP: {'preferredRefreshRate': 72}}, APP), 72)

    def test_automatic_app_refresh_follows_global(self):
        self.assertEqual(rate(72, {'steamvr': {'preferredRefreshRate': 90},
                                   APP: {'preferredRefreshRate': 0}}, APP), 90)

    def test_missing_configuration_keeps_the_observed_rate(self):
        for settings in (None, [], {}, {APP: []}):
            self.assertEqual(rate(80, settings, APP), 80)

    def test_rejects_invalid_rate_values(self):
        for value in (True, False, '72', -1, 0, 29, 241, float('nan'), float('inf')):
            self.assertEqual(rate(90, {APP: {'preferredRefreshRate': value}}, APP), 90)

    def test_fractional_rate_rounds_the_scheduler_up(self):
        settings = {APP: {'preferredRefreshRate': 89.91, 'framesToThrottle': 1}}
        self.assertEqual(cap(rate(72, settings, APP), settings, APP), '45')

    def test_72_hz_without_reprojection_does_not_keep_the_old_45_cap(self):
        settings = {APP: {'preferredRefreshRate': 72, 'motionSmoothingOverride': 2}}
        self.assertEqual(cap(rate(90, settings, APP), settings, APP), '72')

    def test_explicit_half_rate_is_preserved(self):
        settings = {APP: {'preferredRefreshRate': 72, 'framesToThrottle': 1}}
        self.assertEqual(cap(rate(90, settings, APP), settings, APP), '36')

    def test_forced_smoothing_keeps_its_half_rate_minimum(self):
        settings = {APP: {'preferredRefreshRate': 90, 'motionSmoothingOverride': 3,
                          'framesToThrottle': 0}}
        self.assertEqual(cap(rate(72, settings, APP), settings, APP), '45')


if __name__ == '__main__':
    unittest.main()

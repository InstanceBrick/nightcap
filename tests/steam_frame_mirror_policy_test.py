import ast
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / 'tools/steam_frame/run-nightcap-arm64.py'
module = ast.parse(source.read_text())
functions = [node for node in module.body if isinstance(node, ast.FunctionDef)
             and node.name in ('steam_profile_desktop_mirror',
                               'desktop_mirror_environment')]
scope = {}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), 'exec'), scope)
mirror_environment = scope['desktop_mirror_environment']


class DesktopMirrorPolicy(unittest.TestCase):
    def test_frame_defaults_to_hidden_mirror_off(self):
        self.assertEqual(mirror_environment({}, []), {
            'MOCKTAIL_WIN_WIDTH': '1920', 'MOCKTAIL_WIN_HEIGHT': '1080',
            'MOCKTAIL_WINDOW_STATE_PERSISTENCE': '0',
            'MOCKTAIL_VR_DESKTOP_MIRROR': '0',
            'MOCKTAIL_VR_HIDE_DESKTOP_WINDOW': '1',
            'MOCKTAIL_SHOW_WINDOW_ON_FIRST_SWAP': '0',
        })

    def test_saved_mirror_on_shows_the_window(self):
        settings = {'nightcap': {'desktopMirror': 1, 'aaMode': 3,
                                'gazeRadiusPercent': 8}}
        self.assertEqual(mirror_environment(settings, []), {
            'MOCKTAIL_WIN_WIDTH': '1920', 'MOCKTAIL_WIN_HEIGHT': '1080',
            'MOCKTAIL_WINDOW_STATE_PERSISTENCE': '0',
            'MOCKTAIL_VR_DESKTOP_MIRROR': '1',
            'MOCKTAIL_VR_HIDE_DESKTOP_WINDOW': '0',
            'MOCKTAIL_SHOW_WINDOW_ON_FIRST_SWAP': '1',
        })
        self.assertEqual(settings['nightcap'], {
            'desktopMirror': 1, 'aaMode': 3, 'gazeRadiusPercent': 8})

    def test_json_integer_and_float_one_both_enable_the_mirror(self):
        for value in (1, 1.0):
            self.assertEqual(mirror_environment({'nightcap': {'desktopMirror': value}}, [])[
                'MOCKTAIL_VR_DESKTOP_MIRROR'], '1')

    def test_invalid_saved_values_cannot_accidentally_enable_mirror(self):
        for settings in (None, [], {'nightcap': None}, {'nightcap': []},
                         *({'nightcap': {'desktopMirror': value}}
                           for value in (0, -1, 2, True, False, '1', '0', None, [], [1], {}))):
            with self.subTest(settings=settings):
                self.assertEqual(mirror_environment(settings, [])[
                    'MOCKTAIL_VR_DESKTOP_MIRROR'], '0')

    def test_desktop_launch_remains_visible_even_with_mirror_off(self):
        for arguments in (['--no-vr'], ['--vr', '--no-vr']):
            environment = mirror_environment({}, arguments)
            self.assertEqual(environment['MOCKTAIL_VR_HIDE_DESKTOP_WINDOW'], '0')
            self.assertEqual(environment['MOCKTAIL_SHOW_WINDOW_ON_FIRST_SWAP'], '1')

    def test_both_mirror_modes_keep_the_same_logical_viewport(self):
        for value in (0, 1):
            environment = mirror_environment({'nightcap': {'desktopMirror': value}}, [])
            self.assertEqual(environment['MOCKTAIL_WIN_WIDTH'], '1920')
            self.assertEqual(environment['MOCKTAIL_WIN_HEIGHT'], '1080')
            self.assertEqual(environment['MOCKTAIL_WINDOW_STATE_PERSISTENCE'], '0')

    def test_last_explicit_vr_flag_wins(self):
        environment = mirror_environment({}, ['--no-vr', '--vr'])
        self.assertEqual(environment['MOCKTAIL_VR_HIDE_DESKTOP_WINDOW'], '1')


if __name__ == '__main__':
    unittest.main()

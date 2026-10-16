import ast
import io
import json
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / 'tools/steam_frame/run-nightcap-arm64.py'
module = ast.parse(source.read_text())
functions = [node for node in module.body if isinstance(node, ast.FunctionDef)
             and node.name in ('steam_profile_scene_samples', 'read_steam_profile')]
scope = {'json': json}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(source), 'exec'), scope)
samples = scope['steam_profile_scene_samples']
read_profile = scope['read_steam_profile']


class ProfilePath:
    def __init__(self, contents):
        self.contents = contents
        self.read_size = None

    def open(self, mode):
        assert mode == 'rb'
        owner = self

        class File(io.BytesIO):
            def read(self, size=-1):
                owner.read_size = size
                return super().read(size)

        return File(self.contents)


class NativeScenePolicy(unittest.TestCase):
    def test_saved_multisample_and_upscaler_choices_cannot_enable_either_path(self):
        for mode in (0, 1, 2, 3):
            for aa in range(9):
                self.assertEqual(samples({'nightcap': {'aaMode': aa, 'upscaleMode': mode}}), 1)

    def test_missing_and_malformed_profiles_still_use_single_sample_scene(self):
        for profile in (None, [], {}, {'nightcap': []}, {'nightcap': None},
                        {'nightcap': {'aaMode': '5', 'upscaleMode': '3'}}):
            self.assertEqual(samples(profile), 1)

    def test_profile_reader_is_bounded_and_preserves_the_saved_profile(self):
        expected = {'nightcap': {'aaMode': 5}, 'steamvr': {'preferredRefreshRate': 72}}
        path = ProfilePath(json.dumps(expected).encode())
        self.assertEqual(read_profile(path), expected)
        self.assertEqual(path.read_size, 262145)
        self.assertEqual(json.loads(path.contents), expected)

    def test_oversized_and_malformed_profiles_use_single_sample_default(self):
        for contents in (b'x' * 262145, b'{', b'[]', b'null', b''):
            path = ProfilePath(contents)
            self.assertEqual(read_profile(path), {})
            self.assertEqual(samples(read_profile(path)), 1)


if __name__ == '__main__':
    unittest.main()

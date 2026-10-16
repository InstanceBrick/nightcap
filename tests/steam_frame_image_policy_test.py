import ast
from pathlib import Path
import unittest

script = Path(__file__).resolve().parents[1] / 'packaging/steam-frame/verify_image.sh'
embedded = script.read_text().split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
module = ast.parse(embedded)
functions = [node for node in module.body if isinstance(node, ast.FunctionDef)
             and node.name == 'verify_native_cmake_settings']
scope = {}
exec(compile(ast.Module(body=functions, type_ignores=[]), str(script), 'exec'), scope)
verify = scope['verify_native_cmake_settings']


class NativeImagePolicy(unittest.TestCase):
    def test_accepts_real_cmake_string_vr_and_boolean_flags(self):
        verify("// Native Frame build\nMOCKTAIL_ENABLE_VR:STRING=ON\n"
               "MOCKTAIL_ENABLE_FSR3:BOOL=OFF\nMOCKTAIL_VR_DIAGNOSTICS:BOOL=OFF\n")

    def test_accepts_equivalent_configured_values_across_cache_types(self):
        verify("MOCKTAIL_ENABLE_VR:BOOL=ON\nMOCKTAIL_ENABLE_FSR3:UNINITIALIZED=OFF\n"
               "MOCKTAIL_VR_DIAGNOSTICS:BOOL=OFF\n")

    def test_rejects_incorrect_build_values(self):
        settings = {'MOCKTAIL_ENABLE_VR': 'ON', 'MOCKTAIL_ENABLE_FSR3': 'OFF',
                    'MOCKTAIL_VR_DIAGNOSTICS': 'OFF'}
        for name, invalid in [('MOCKTAIL_ENABLE_VR', 'OFF'),
                              ('MOCKTAIL_ENABLE_VR', 'AUTO'),
                              ('MOCKTAIL_ENABLE_FSR3', 'ON'),
                              ('MOCKTAIL_VR_DIAGNOSTICS', 'ON')]:
            with self.subTest(name=name, invalid=invalid):
                changed = dict(settings, **{name: invalid})
                with self.assertRaises(SystemExit):
                    verify('\n'.join(f'{key}:STRING={value}' for key, value in changed.items()))

    def test_rejects_missing_or_commented_out_build_settings(self):
        settings = {'MOCKTAIL_ENABLE_VR': 'ON', 'MOCKTAIL_ENABLE_FSR3': 'OFF',
                    'MOCKTAIL_VR_DIAGNOSTICS': 'OFF'}
        for omitted in settings:
            with self.subTest(omitted=omitted):
                lines = [f'{key}:STRING={value}' for key, value in settings.items() if key != omitted]
                lines.append(f'//{omitted}:STRING={settings[omitted]}')
                with self.assertRaises(SystemExit):
                    verify('\n'.join(lines))


if __name__ == '__main__':
    unittest.main()

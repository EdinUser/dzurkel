"""Ensure the native gate rejects the displacement/offscreen failures it guards."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gnome_native', Path(__file__).parents[1] / 'integration/GnomeNativeTest.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class RenderedPlacementAssertions(unittest.TestCase):
    def test_different_client_sizes_keep_the_same_rendered_center(self):
        center = native.assert_centered([440, 235, 840, 635], None, [1280, 800])
        self.assertEqual(native.assert_centered([232, 182, 1048, 688], center, [1280, 800]), center)

    def test_in_bounds_but_displaced_adaptive_window_fails(self):
        with self.assertRaisesRegex(AssertionError, 'displaced rendered center'):
            native.assert_centered([400, 182, 1216, 688], [640, 435], [1280, 800])

    def test_wrong_initial_placement_is_not_used_as_a_successful_baseline(self):
        with self.assertRaisesRegex(AssertionError, 'Initial preview'):
            native.assert_centered([20, 50, 420, 450], None, [1280, 800])

    def test_empty_or_offscreen_capture_bounds_fail(self):
        for bounds in ([-1, 200, 399, 600], [0, 200, 1281, 600], [0, 0, 400, 801], [400, 200, 400, 600]):
            with self.subTest(bounds=bounds), self.assertRaisesRegex(AssertionError, 'outside framebuffer'):
                native.assert_contained(bounds, [1280, 800])


if __name__ == '__main__':
    unittest.main()

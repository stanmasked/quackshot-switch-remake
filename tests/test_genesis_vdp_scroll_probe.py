import ctypes as C
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import genesis_vdp_scroll_probe as vdp


class StubHook:
    def __call__(self, line, out, count):
        if count < 6 or line > 1023:
            return 0
        for i,x in enumerate((3,32,0xF000,255,0x07FF,0x0002)):
            out[i] = x
        return 1


class FakeCore:
    quackshot_research_vdp_hscroll = StubHook()


class GenesisVDPHScrollTests(unittest.TestCase):
    def test_signed_11bit_boundaries(self):
        self.assertEqual(vdp.signed_11bit(0), 0)
        self.assertEqual(vdp.signed_11bit(0x3ff), 1023)
        self.assertEqual(vdp.signed_11bit(0x400), -1024)
        self.assertEqual(vdp.signed_11bit(0x7ff), -1)
        self.assertEqual(vdp.signed_11bit(0x87ff), -1)

    def test_binding_requires_hook(self):
        with self.assertRaisesRegex(RuntimeError, "lacks"):
            vdp.bind_vdp_probe(object())

    def test_stub_numeric_snapshot(self):
        bound = vdp.bind_vdp_probe(FakeCore())
        out = vdp.vdp_number_snapshot(bound, 192)
        self.assertEqual(out["hscroll_vram_base_address"], 0xF000)
        self.assertEqual(out["hscroll_scanline_mask"], 255)
        self.assertEqual(out["vdp_reg11"], 3)
        self.assertEqual(out["plane_A_HScroll_11bit_signed"], -1)
        self.assertEqual(out["plane_B_HScroll_11bit_signed"], 2)
        self.assertEqual(out["plane_A_highbits"], 0)

    def test_hardware_hook_rejects_invalid_frame_line(self):
        h = vdp.bind_vdp_probe(FakeCore())
        with self.assertRaisesRegex(RuntimeError, "unavailable"):
            vdp.vdp_number_snapshot(h, 1024)

    def test_four_independent_hardware_lines(self):
        self.assertEqual(vdp.SCANLINES, (0,64,128,192))
        self.assertEqual(len(vdp.KEYFRAMES), len(set(vdp.KEYFRAMES)))
        self.assertIn(2124, vdp.KEYFRAMES)
        self.assertIn(2375, vdp.KEYFRAMES)

    def test_core_provenance_research_only(self):
        hook = Path(__file__).resolve().parents[1] / "tools/genesis_vdp_hscroll_export.c"
        content = hook.read_text()
        self.assertIn("quackshot_research_vdp_hscroll",content)
        self.assertNotIn("fwrite",content)
        self.assertNotIn("memcpy",content)


if __name__=="__main__":
    unittest.main()

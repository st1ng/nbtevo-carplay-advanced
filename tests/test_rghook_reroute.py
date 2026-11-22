"""The start marker after a reroute in librghook.so (src/rghook.c, cur_view; issue #9, BMW F25 X3: "the first direction after a
reroute feels missed"). After a reroute the iPhone's current maneuver list (RouteGuidanceUpdate 0x5201, field 13) is [0, 1]: the
start marker (type 11, 0 m) and the first real turn, and only later [1, 2]; the HUD drew the start marker in between. The first turn
is published instead, with its step length as the distance in the iPhone's units; test switch keep_start_marker: as before.
Built with test_rghook_lanes.py's harness."""
import os
import struct
import subprocess
import shutil
import tempfile
import unittest
from pathlib import Path

try:
    from test_rghook_lanes import HARNESS, ROOT, tlv          # python3 -m unittest discover -s tests
except ImportError:
    from tests.test_rghook_lanes import HARNESS, ROOT, tlv    # python3 -m unittest tests.test_rghook_reroute


def maneuver(idx, mtype, step, road=b'Main Street'):
    return (tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, struct.pack('>H', idx)) + tlv(3, bytes([mtype])) + tlv(4, road + b'\0')
            + tlv(5, struct.pack('>I', step)))


def update(current, dist, dist_s, units=2):
    return (tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, b'\x01') + tlv(10, struct.pack('>I', dist)) + tlv(11, dist_s + b'\0')
            + tlv(12, bytes([units])) + tlv(13, b''.join(struct.pack('>H', i) for i in current)))


class RgHookReroute(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang'):
            raise unittest.SkipTest('clang not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='rghook-rr-')
        d = Path(cls.tmp.name)
        (d / 'h.c').write_text(HARNESS)
        cls.exe = d / 'h'
        subprocess.run(['clang', '-w', '-I', str(ROOT / 'src'), '-o', str(cls.exe), str(d / 'h.c')], check=True)
        cls.cfg = d / 'cfg'
        cls.cfg.mkdir()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_msgs(self, *msgs, keep=False):
        pps = Path(self.tmp.name) / 'routeguidance'
        if pps.exists():
            pps.unlink()
        sw = self.cfg / 'keep_start_marker'
        if keep:
            sw.write_text('')
        elif sw.exists():
            sw.unlink()
        args = [str(self.exe)]
        for mid, payload in msgs:
            args += ['%x' % mid, payload.hex()]
        subprocess.run(args, check=True, env=dict(os.environ, RGHOOK_PPS=str(pps), RGHOOK_CFG=str(self.cfg), RGHOOK_NO_WATCH='1'))
        return dict(l.split(':', 2)[0::2] for l in pps.read_text().splitlines() if l.count(':') >= 2)

    def reroute(self, units=2, keep=False, step=160):
        return self.run_msgs((0x5202, maneuver(0, 11, 0)), (0x5202, maneuver(1, 1, step, b'Oak Avenue')),
                             (0x5201, update([0, 1], 0, b'0 m', units)), keep=keep)

    def test_start_marker_then_first_turn(self):
        out = self.reroute()
        self.assertEqual(out['maneuver_index'], '1')
        self.assertEqual(out['maneuver_type'], '1')
        self.assertEqual(out['maneuver_road'], 'Oak Avenue')
        self.assertEqual(out['dist_to_maneuver'], '160')
        self.assertEqual(out['dist_to_maneuver_string'], '150 m')

    def test_units(self):
        self.assertEqual(self.reroute(units=4)['dist_to_maneuver_string'], '550 ft')
        self.assertEqual(self.reroute(units=1, step=2414)['dist_to_maneuver_string'], '1.5 mi')
        self.assertEqual(self.reroute(units=0, step=2450)['dist_to_maneuver_string'], '2.5 km')
        self.assertEqual(self.reroute(units=3, step=150)['dist_to_maneuver_string'], '150 yd')

    def test_no_step_length_keeps_the_iphones_distance(self):
        out = self.reroute(step=0)
        self.assertEqual(out['maneuver_index'], '1')
        self.assertEqual(out['dist_to_maneuver_string'], '0 m')

    def test_switch_keeps_the_start_marker(self):
        out = self.reroute(keep=True)
        self.assertEqual(out['maneuver_index'], '0')
        self.assertEqual(out['maneuver_type'], '11')

    def test_unchanged_cases(self):
        m0, m1 = (0x5202, maneuver(0, 11, 0)), (0x5202, maneuver(1, 1, 160))
        far = self.run_msgs(m0, m1, (0x5201, update([0, 1], 400, b'400 m')))         # driving to the route's start
        self.assertEqual(far['maneuver_index'], '0')
        alone = self.run_msgs(m0, (0x5201, update([0], 0, b'0 m')))                  # no next maneuver listed
        self.assertEqual(alone['maneuver_index'], '0')
        unsent = self.run_msgs(m0, (0x5201, update([0, 1], 0, b'0 m')))              # listed, not sent yet
        self.assertEqual(unsent['maneuver_index'], '0')
        turn = self.run_msgs(m0, m1, (0x5202, maneuver(2, 2, 300)), (0x5201, update([1, 2], 120, b'120 m')))
        self.assertEqual((turn['maneuver_index'], turn['dist_to_maneuver_string']), ('1', '120 m'))


if __name__ == '__main__':
    unittest.main()

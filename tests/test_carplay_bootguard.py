"""The bootloop guard in the CarPlayMenu plugin's boot.sh: failed boots in a row (boot_pending left over) -> safe mode at 3, the plugin
disabled at 3 more while in safe mode; a good boot (the plugin deleted boot_pending) resets the count. Runs the real boot.sh with ksh
on folders in a temp dir (CPM_DIR / CPM_PLUGIN_DIR / CPM_LOG); no switch files, so it makes no links."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

BOOT = Path(__file__).resolve().parents[1] / 'plugin/boot.sh'
KSH = shutil.which('ksh')


@unittest.skipUnless(KSH, 'no ksh')
class BootGuard(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.d = self.tmp / 'carplaymenu'
        self.pl = self.tmp / 'Plugins/CarPlayMenu'
        self.d.mkdir()
        self.pl.mkdir(parents=True)
        (self.pl / 'CarPlayMenu.manifest').write_text('m')
        self.log = self.tmp / 'log'

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def boot(self, good=False, hmi_running=False):
        """One boot: boot.sh, then (good) the plugin deleting boot_pending after its minute. hmi_running: a fake pidin that lists
        HmiMain3D (boot.sh run by the installer on a running unit)."""
        env = dict(os.environ, CPM_DIR=str(self.d), CPM_PLUGIN_DIR=str(self.pl), CPM_LOG=str(self.log))
        if hmi_running:
            bin_ = self.tmp / 'bin'
            bin_.mkdir(exist_ok=True)
            (bin_ / 'pidin').write_text('#!/bin/sh\necho "  12345   1 /opt/hmi/bin/HmiMain3D  10r RUNNING"\n')
            (bin_ / 'pidin').chmod(0o755)
            env['PATH'] = f'{bin_}:{env["PATH"]}'
        subprocess.run([KSH, str(BOOT)], env=env, check=True)
        if good:
            (self.d / 'boot_pending').unlink(missing_ok=True)

    def strikes(self):
        return int((self.d / 'boot_strikes').read_text())

    def safe(self):
        return (self.d / 'safe_mode').exists()

    def enabled(self):
        return (self.pl / 'CarPlayMenu.manifest').exists()

    def test_good_boots_never_trip(self):
        for _ in range(5):
            self.boot(good=True)
        self.assertEqual(self.strikes(), 0)
        self.assertFalse(self.safe())
        self.assertTrue(self.enabled())

    def test_one_failed_boot_changes_nothing(self):
        self.boot()                 # failed (boot_pending left)
        self.boot(good=True)
        self.assertEqual(self.strikes(), 1)
        self.boot(good=True)
        self.assertEqual(self.strikes(), 0)
        self.assertFalse(self.safe())
        self.assertTrue(self.enabled())

    def test_three_failed_boots_safe_mode(self):
        for _ in range(3):
            self.boot()
        self.assertFalse(self.safe())
        self.boot(good=True)        # sees the 3rd failure
        self.assertTrue(self.safe())
        self.assertTrue(self.enabled())
        self.assertIn('SAFE MODE', self.log.read_text())
        self.boot(good=True)        # safe mode is sticky across good boots
        self.assertTrue(self.safe())

    def test_failing_in_safe_mode_disables_plugin(self):
        (self.d / 'safe_mode').write_text('1')
        for _ in range(3):
            self.boot()
        self.assertTrue(self.enabled())
        self.boot()
        self.assertFalse(self.enabled())
        self.assertTrue((self.pl / 'CarPlayMenu.manifest.disabled').exists())
        self.assertFalse((self.d / 'boot_pending').exists())   # disabled plugin: nobody would delete it

    def test_restart_to_apply_is_not_a_failure(self):
        # the plugin's onRestart deletes boot_pending and safe_mode before rebooting
        (self.d / 'safe_mode').write_text('1')
        self.boot()
        (self.d / 'boot_pending').unlink()
        (self.d / 'safe_mode').unlink()
        self.boot(good=True)
        self.assertEqual(self.strikes(), 0)
        self.assertFalse(self.safe())

    def test_run_while_hmi_is_up_is_not_a_boot(self):
        self.boot(good=True)        # a good boot: count 0, boot_pending gone after the plugin's minute
        self.boot(hmi_running=True)  # the installer's relink
        self.assertFalse((self.d / 'boot_pending').exists())
        self.assertEqual(self.strikes(), 0)
        self.assertIn('not a boot', self.log.read_text())
        self.boot(good=True)
        self.assertEqual(self.strikes(), 0)

    def test_hud_only_never_guards(self):
        # the HUD-only menu (turn-by-turn-only install) may never be started (a car not coded for apps): no strikes, no safe mode
        (self.pl / 'hud_only').write_text('1')
        for _ in range(8):
            self.boot()
        self.assertFalse((self.d / 'boot_pending').exists())
        self.assertFalse(self.safe())
        self.assertTrue(self.enabled())

    def test_garbage_count(self):
        (self.d / 'boot_strikes').write_text('x\n')
        self.boot()
        self.boot(good=True)
        self.assertEqual(self.strikes(), 1)


if __name__ == '__main__':
    unittest.main()

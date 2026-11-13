"""carplay-advanced.sh (CarPlay Advanced Settings installer): filesystem-level tests, no head unit.

Only the absolute path constants of a temporary copy of the production script are redirected into a temporary directory. QNX-only
tools (uname, pidin, on, mount, df, sync, OnOffDSICommander) and ssh are simulated; cp/mv/dd/diff/grep/ksh are the host's own.
The ssh stand-in runs the remote command locally, as the head unit would (landing on hu-jacinto unless TEST_LAND says otherwise).
Every test runs under /bin/sh and under ksh (the head unit's shell is a ksh).
"""
import hashlib, os, shlex, shutil, struct, subprocess, tempfile, unittest, urllib.parse, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SOURCE = HERE / 'carplay-advanced.sh'
FILES = ('CarPlayMenu.manifest', 'CarPlayMenu.xml', 'CarPlayMenu_ALL_Texts.zip', 'libCarPlayMenu.so', 'librghook.so',
         'librghook-kombi.so', 'librghook-most.so', 'librghook-both.so', 'libcppinch.so', 'libcpreboot.so', 'libcpprobe.so', 'boot.sh',
         'restart.sh')
LIBS = ('libCarPlayMenu.so', 'librghook.so', 'librghook-kombi.so', 'librghook-most.so', 'librghook-both.so', 'libcppinch.so',
        'libcpreboot.so', 'libcpprobe.so', 'libCarPlayMenu-hud.so')
HMI_VERSION = b'NBTevo_ID5_1803_Release ID5_1803-510-1749MA Build 71'          # no trailing newline, as in the firmware
START_SCREEN = b'#!/bin/sh \n\n# Start QNX screen resource manager\nCONFIGFILE=${GRAPHICS_ROOT}/graphics.conf\nscreen -c $CONFIGFILE &\n'
MARK = b'CarPlayMenu boot hook'
# The supported-software table of the copy under test: fake HmiMain3D / libconn_carplay.so contents and their hashes
HMI_006, HMI_1703, CONN = b'HmiMain3D 006', b'HmiMain3D 1703', b'libconn_carplay'
sha = lambda b: hashlib.sha256(b).hexdigest()
TABLE = ['# software file sha256',
         'TEST_006 HmiMain3D %s' % sha(HMI_006), 'TEST_006 libconn_carplay.so %s' % sha(CONN),
         'TEST_1703 HmiMain3D %s' % sha(HMI_1703), 'TEST_1703 libconn_carplay.so %s' % sha(CONN)]
SHELLS = [s for s in ('/bin/sh', shutil.which('ksh')) if s]


def elf(tag=b'new', etype=3, machine=40):
    data = bytearray(64)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHI', data, 16, etype, machine, 1)
    struct.pack_into('<H', data, 40, 52)
    return bytes(data) + tag


class Base(object):
    SHELL = '/bin/sh'

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cpa-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root = r = Path(self.tmp.name)
        self.p = {
            'REMOTE_NODE': r / 'net/hu-omap',
            'FS_ROOT': r / 'fs',
            'HMI_VERSION_FILE': r / 'opt/hmi/ID5/data/ro/version.txt',
            'HMI_BIN': r / 'opt/hmi/bin/zipped/HmiMain3D',
            'HMI_BIN_PLAIN': r / 'opt/hmi/bin/HmiMain3D',
            'CONN_LIB': r / 'opt/conn/lib/carplay/devctrl/libconn_carplay.so',
            'PLUGIN': r / 'var/opt/hmi/data/rw/Plugins/CarPlayMenu',
            'SETTINGS': r / 'var/opt/hmi/data/rw/carplaymenu',
            'START_SCREEN': r / 'sysbin/start_screen.sh',
            'LAYOUT': r / 'opt/hmi/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw',
            'LAYOUT_ID5': r / 'opt/hmi/ID5/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw',
            'LAYOUT_COMMON': r / 'opt/hmi/data/ro/common/widget/generated/Spec_State_Merged.pbw',
            'LAYOUT_COMMON_ID5': r / 'opt/hmi/ID5/data/ro/common/widget/generated/Spec_State_Merged.pbw',
            'MMIPOD': r / 'opt/conn/bin/carplay/mm-ipod',
            'CONNECTIVITY': r / 'opt/conn/bin/Connectivity',
            'CARPLAY_INI': r / 'opt/conn/data/carplay/carplay.ini',
            'NBT_AAD_PLUGIN': r / 'opt/hmi/ID5/data/ro/Plugins/NbtAad',
            'BACKUPS': r / 'var/opt/carplay-advanced-backups',
            'LOCK': r / 'var/opt/carplay-advanced.lock',
            'STAGE': r / 'stage',
            'SHELL_TMP': r / 'tmp',
            'SHMEM': r / 'dev/shmem',
            'PROBE_HOST': r / 'stubs/probe-host',
        }
        for path in self.p.values():
            path.parent.mkdir(parents=True, exist_ok=True)
        (self.p['FS_ROOT'] / 'opt/hmi/ID5').mkdir(parents=True)
        (self.p['REMOTE_NODE'] / 'fs/sda0/opt/hmi/ID5').mkdir(parents=True)
        self.p['HMI_VERSION_FILE'].write_bytes(HMI_VERSION)
        self.p['HMI_BIN'].write_bytes(HMI_006)
        self.p['CONN_LIB'].write_bytes(CONN)
        self.p['START_SCREEN'].write_bytes(START_SCREEN)
        self.p['START_SCREEN'].chmod(0o755)
        self.p['LAYOUT'].write_bytes(b'PBW original layout')
        self.p['LAYOUT_COMMON_ID5'].write_bytes(b'PBW original common layout')
        self.p['MMIPOD'].write_bytes(elf(b'mm-ipod', etype=2))
        self.p['CONNECTIVITY'].write_bytes(elf(b'Connectivity' * 100, etype=2))
        self.p['CARPLAY_INI'].write_bytes(b'Option: 750_480\n[Screen]\nWidth=750\n')
        content = SOURCE.read_text()
        lines = content.splitlines()
        for key, value in self.p.items():
            matches = [n for n, line in enumerate(lines) if line.startswith(key + '=')]
            self.assertEqual(len(matches), 1, key)
            lines[matches[0]] = key + '=' + shlex.quote(str(value))
        (pw,) = [n for n, line in enumerate(lines) if line.startswith('SSH_PASSWORD=')]
        lines[pw] = "SSH_PASSWORD='pw & for tests'"
        start = lines.index('supported_table() {') + 2                   # after "cat <<'EOF'"
        end = lines.index('EOF', start)
        lines[start:end] = TABLE
        self.script = r / 'carplay-advanced.sh'
        self.script.write_text('\n'.join(lines) + '\n')
        self.bin = r / 'stubs'
        self.bin.mkdir(exist_ok=True)
        # libcpprobe.so's line, as /bin/cat with it preloaded prints it on the head unit (TEST_PROBE; unset = no answer)
        self.stub('probe-host', 'case "$LD_PRELOAD" in *libcpprobe.so) [ -n "${TEST_PROBE:-}" ] && echo "$TEST_PROBE" ;; esac\nexit 0')
        self.calls_file = r / "calls"
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'], TEST_CALLS=str(self.calls_file),
                        TEST_ROOT=str(r), TEST_SHELL=self.SHELL)
        self.stub('uname', 'if [ "$1" = -s ]; then echo "${TEST_SYSNAME:-QNX}"; else echo "${TEST_NODE:-hu-omap}"; fi')
        self.stub('mount', 'echo "mount $*" >> "$TEST_CALLS"\n'
                  '[ "${TEST_MOUNT_RC:-0}" = 0 ] || exit 1\n'
                  'chmod u+w "$TEST_ROOT/sysbin"')
        self.stub('df', 'if [ "$1" = -k ]; then echo "Filesystem 1024-blocks Used Available Capacity Mounted on"; '
                  'echo "/dev/hd0t177 100000 1000 ${TEST_DF_AVAIL:-500000} 1% /var"; '
                  'else echo "Filesystem 512-blocks Used Available Capacity Mounted on"; '
                  'echo "/dev/hd0t178 100 10 90 10% $TEST_ROOT/fs"; fi')
        self.stub('sync', 'echo sync >> "$TEST_CALLS"; exit "${TEST_SYNC_RC:-0}"')
        self.stub('nc', 'echo "nc $*" >> "$TEST_CALLS"; exit "${TEST_NC_RC:-0}"')                # the reachability probe
        self.stub('OnOffDSICommander', 'echo "appreset $*" >> "$TEST_CALLS"; sleep "${TEST_HMI_SLEEP:-0}"; exit "${TEST_HMI_RC:-0}"')
        self.stub('on', 'echo on >> "$TEST_CALLS"\n'
                  '[ "$1" = -f ] && { [ "$2" = "$TEST_ROOT/net/hu-omap" ] || [ "$2" = hu-omap ]; } || exit 90\n'
                  'shift 2; prog=$1; shift\n'        # -f node <program>
                  'TEST_NODE=${TEST_FORWARD_NODE:-hu-omap}; export TEST_NODE\n'
                  'case "$prog" in */sh|*/ksh) exec "$TEST_SHELL" "$@" ;; *) exec "$prog" "$@" ;; esac')
        # ssh: the master connection (-fN) and its exit (-O) only succeed; a command runs locally as on the head unit
        self.stub('ssh', 'cmd=; seen=0\n'
                  'while [ "$#" -gt 0 ]; do case "$1" in\n'
                  '  -o) shift 2 ;;\n'
                  '  -fN) echo ssh-master >> "$TEST_CALLS"\n'
                  '       if [ "$SSH_ASKPASS_REQUIRE" = force ]; then echo "askpass:$("$SSH_ASKPASS" Password:)" >> "$TEST_CALLS"; fi\n'
                  '       exit "${TEST_SSH_RC:-0}" ;;\n'
                  '  -O) exit 0 ;;\n'
                  '  *) if [ "$seen" = 0 ]; then echo "ssh-host $1" >> "$TEST_CALLS"; seen=1; else cmd=$1; fi; shift ;;\n'
                  'esac; done\n'
                  'cmd=$(printf "%s" "$cmd" | sed "s#/bin/ksh#$TEST_SHELL#g; s#/bin/sh #$TEST_SHELL #g; s#/bin/cat#cat#g")\n'
                  'TEST_SYSNAME=QNX; TEST_NODE=${TEST_LAND:-hu-jacinto}; export TEST_SYSNAME TEST_NODE\n'
                  'exec sh -c "$cmd"')
        self.stub('pidin', 'if [ "$1" = -p ]; then\n'
                  '  echo "pid euid"; echo "$2 ${TEST_EUID:-0}"; exit 0\n'
                  'fi\n'
                  'echo "pid Arguments"\n'
                  'if [ -f "$TEST_ROOT/processes" ]; then cat "$TEST_ROOT/processes"; fi')
        self.pl = self.payload()

    def stub(self, name, body):
        path = self.bin / name
        path.write_text('#!/bin/sh\n' + body + '\n')
        path.chmod(0o755)

    def payload(self, tag=b'new'):
        """The plugin files in plugin/ next to the script (the folder a computer uploads, and --payload on the head unit)."""
        dest = self.root / 'plugin'
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir()
        for lib in LIBS:
            (dest / lib).write_bytes(elf(tag + lib.encode()))
        (dest / 'CarPlayMenu.manifest').write_text('<plugin>\n\t<name>CarPlayMenu</name>\n</plugin>\n')
        (dest / 'CarPlayMenu.xml').write_text('<pluginApps><pluginApp applicationType="Radio"/></pluginApps>\n')
        (dest / 'CarPlayMenu-hud.xml').write_text('<pluginApps><pluginApp applicationType="Radio" hud="1"/></pluginApps>\n')
        with zipfile.ZipFile(dest / 'CarPlayMenu_ALL_Texts.zip', 'w') as z:
            z.writestr('db', b'texts')
        # boot.sh: records that it ran, and links like the real one for the switches that are on. The real boot.sh uses QNX "ln -sP"
        # (a path-manager link that overrides the original file in place); the host has no path-manager, so "ln -sf" stands in for it.
        (dest / 'boot.sh').write_text('#!/bin/ksh\necho "boot %s" >> "$TEST_CALLS"\n'
                                      'D=%s\n[ -f $D/hud_guidance ] && ln -sf $D/mm-ipod %s\nexit 0\n'
                                      % (tag.decode(), self.p['SETTINGS'], self.p['MMIPOD']))
        (dest / 'restart.sh').write_text('#!/bin/ksh\nexit 0\n')
        return dest

    def run_script(self, *args, rc=0, **env):
        e = dict(self.env, **{k: str(v) for k, v in env.items()})
        if args and args[0] == 'install' and '--payload' not in args and 'TEST_SYSNAME' not in env:
            args += ('--payload', str(self.pl))                 # on the head unit: the files are given
        if args and args[0] == 'install' and '--hmi' not in args and 'TEST_SYSNAME' not in env:
            args += ('--hmi', 'TEST_006')                       # ... and the software the computer found by hash
        res = subprocess.run([self.SHELL, str(self.script)] + list(args), env=e, capture_output=True, timeout=60)
        out = res.stdout.decode() + res.stderr.decode()
        if rc is not None:
            self.assertEqual(res.returncode, rc, out)
        return out

    def calls(self):
        return self.calls_file.read_text().splitlines() if self.calls_file.exists() else []

    def backups(self, action=None):
        b = self.p['BACKUPS']
        return sorted(d for d in b.iterdir() if d.name != 'originals' and (action is None or d.name.endswith('-' + action))) \
            if b.exists() else []

    def assert_plugin(self, tag=b'new'):
        src = self.pl
        for f in FILES:
            self.assertEqual((self.p['PLUGIN'] / f).read_bytes(), (src / f).read_bytes(), f)

    def hooked(self):
        return self.p['START_SCREEN'].read_bytes().count(MARK)


class InstallTest(Base):
    def test_refuses_non_root(self):
        out = self.run_script('install', rc=1, TEST_EUID=501)
        self.assertIn('Root SSH access could not be verified', out)

    def test_install_on_the_unit_needs_the_hash_check(self):
        out = self.run_script('install', '--hmi', '', rc=1)
        self.assertIn('needs a computer', out)
        self.assertIn("Unknown software 'ID5_1903'", self.run_script('install', '--hmi', 'ID5_1903', rc=1))
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)
        self.assertIn('supported software TEST_006', self.run_script('install', '--dry-run'))

    def test_refuses_symlinked_start_screen(self):
        real = self.root / 'elsewhere.sh'
        real.write_bytes(START_SCREEN)
        self.p['START_SCREEN'].unlink()
        self.p['START_SCREEN'].symlink_to(real)
        self.assertIn('it is a link', self.run_script('install', rc=1))

    def test_refuses_two_hook_lines(self):
        self.p['START_SCREEN'].write_bytes(START_SCREEN + b'x # ' + MARK + b'\ny # ' + MARK + b'\n')
        self.assertIn('2 boot-hook lines', self.run_script('install', rc=1))

    def test_refuses_bad_library(self):
        (self.pl / 'librghook.so').write_bytes(elf(machine=3))        # x86
        out = self.run_script('install', rc=1)
        self.assertIn('Not an ARM shared library', out)
        self.assertFalse(self.p['BACKUPS'].exists())
        self.assertEqual(list(self.pl.glob('check-*')), [])

    def test_refuses_bad_script_in_payload(self):
        (self.pl / 'boot.sh').write_text('if [ x ]; then\n')
        self.assertIn('boot.sh does not parse', self.run_script('install', rc=1))

    def test_install_hud_only_on_id6(self):
        # iDrive 6, turn-by-turn only: the menu plugin and HMI patches are skipped; only the mm-ipod route-guidance wrapper is built.
        out = self.run_script('install', '--kind', 'id6', '--mode', 'hud')
        self.assertIn('SUCCESS: install completed', out)
        self.assertIn('no reboot', out.lower())
        # the plugin dir: librghook, boot.sh and the HUD-only menu under the plugin's names, with the hud_only marker
        self.assertTrue((self.p['PLUGIN'] / 'librghook.so').exists())
        self.assertTrue((self.p['PLUGIN'] / 'boot.sh').exists())
        self.assertTrue((self.p['PLUGIN'] / 'CarPlayMenu.manifest').exists())
        self.assertEqual((self.p['PLUGIN'] / 'libCarPlayMenu.so').read_bytes(), elf(b'newlibCarPlayMenu-hud.so'))
        self.assertIn('hud="1"', (self.p['PLUGIN'] / 'CarPlayMenu.xml').read_text())
        self.assertFalse((self.p['PLUGIN'] / 'libCarPlayMenu-hud.so').exists())
        self.assertFalse((self.p['PLUGIN'] / 'libcppinch.so').exists())                # no HMI-side hooks
        self.assertTrue((self.p['PLUGIN'] / 'hud_only').exists())
        # the route-guidance wrapper: a copy of the original mm-ipod, the wrapper script and the switch
        self.assertEqual((self.p['SETTINGS'] / 'bin' / 'mm-ipod').read_bytes(), elf(b'mm-ipod', etype=2))
        wrap = (self.p['SETTINGS'] / 'mm-ipod').read_text()
        self.assertIn('librghook.so', wrap)
        self.assertIn('exec', wrap)
        self.assertTrue((self.p['SETTINGS'] / 'hud_guidance').exists())
        # boot hook added and the relink made mm-ipod a link to the wrapper
        self.assertEqual(self.hooked(), 1)
        self.assertTrue(self.p['MMIPOD'].is_symlink())

    def test_full_stack_on_id6_needs_no_hash(self):
        # the full plugin can still be forced on iDrive 6 (the plugin self-detects; no hash is required there).
        out = self.run_script('install', '--kind', 'id6', '--mode', 'full', '--hmi', '')
        self.assertIn('SUCCESS: install completed', out)
        self.assertTrue((self.p['PLUGIN'] / 'libCarPlayMenu.so').exists())
        self.assertTrue((self.p['PLUGIN'] / 'CarPlayMenu.manifest').exists())

    def test_install_needs_payload(self):
        self.assertIn('install needs the plugin files', self.run_script('install', '--payload', '', rc=1))
        (self.pl / 'restart.sh').unlink()
        self.assertIn('No complete plugin', self.run_script('install', rc=1))
        self.assertFalse(self.p['BACKUPS'].exists())

    def test_host_is_for_computers(self):
        self.assertIn('--host is for runs from a computer', self.run_script('status', '--host', 'root@x', rc=1))

    def test_refuses_without_space(self):
        out = self.run_script('install', rc=1, TEST_DF_AVAIL=100)
        self.assertIn('Not enough free space', out)
        self.assertFalse(self.p['BACKUPS'].exists())

    def test_refuses_stale_lock(self):
        self.p['LOCK'].mkdir()
        self.assertIn('stale lock', self.run_script('install', rc=1))
        self.assertTrue(self.p['LOCK'].exists())

    def test_dry_run_changes_nothing(self):
        out = self.run_script('install', '--dry-run')
        self.assertIn('DRY RUN: all checks passed', out)
        self.assertIn('add the boot-hook line', out)
        self.assertFalse(self.p['BACKUPS'].exists())
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertFalse(self.p['LOCK'].exists())
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)
        self.assertEqual(self.calls(), [])

    def test_fresh_install(self):
        out = self.run_script('install')
        self.assertIn('SUCCESS: install completed', out)
        self.assert_plugin()
        ss = self.p['START_SCREEN'].read_bytes().split(b'\n')
        self.assertEqual(ss[0], b'#!/bin/sh ')
        self.assertIn(MARK, ss[1])
        self.assertIn(str(self.p['PLUGIN']).encode() + b'/boot.sh', ss[1])
        self.assertEqual(b'\n'.join(ss[:1] + ss[2:]), START_SCREEN)
        orig = self.p['BACKUPS'] / 'originals'
        self.assertEqual((orig / 'start_screen.sh').read_bytes(), START_SCREEN)
        self.assertEqual((orig / 'Spec_State_Merged.pbw').read_bytes(), self.p['LAYOUT'].read_bytes())
        self.assertEqual((orig / 'Spec_State_Merged-common.pbw').read_bytes(), self.p['LAYOUT_COMMON_ID5'].read_bytes())
        self.assertEqual((orig / 'mm-ipod').read_bytes(), self.p['MMIPOD'].read_bytes())
        self.assertEqual((orig / 'Connectivity').read_bytes(), self.p['CONNECTIVITY'].read_bytes())
        self.assertEqual((orig / 'carplay.ini').read_bytes(), self.p['CARPLAY_INI'].read_bytes())
        (b,) = self.backups('install')
        self.assertTrue((b / 'COMMITTED').exists())
        self.assertEqual((b / 'start_screen.sh').read_bytes(), START_SCREEN)
        self.assertFalse((b / 'plugin').exists())
        self.assertIn('boot new', self.calls())
        self.assertFalse(self.p['LOCK'].exists())
        self.assertNotIn('mount', ' '.join(self.calls()))                           # the directory was writable
        self.assertNotIn('appreset', ' '.join(self.calls()))

    def test_remounts_read_only_system(self):
        (self.root / 'sysbin').chmod(0o555)
        self.addCleanup((self.root / 'sysbin').chmod, 0o755)
        self.run_script('install')
        self.assertIn('mount -o remount rw %s' % (self.root / 'fs'), self.calls())
        self.assertEqual(self.hooked(), 1)
        self.assertEqual(list((self.root / 'sysbin').glob('.start_screen*')), [])

    def test_read_only_system_and_failed_remount(self):
        (self.root / 'sysbin').chmod(0o555)
        self.addCleanup((self.root / 'sysbin').chmod, 0o755)
        old = self.p['MMIPOD']
        out = self.run_script('install', rc=1, TEST_MOUNT_RC=1)
        self.assertIn('Cannot remount', out)
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)
        self.assertTrue(old.exists())

    def test_update_keeps_settings_and_backs_up_old_plugin(self):
        self.run_script('install')
        settings = self.p['SETTINGS']
        settings.mkdir(exist_ok=True)
        (settings / 'fullscreen').write_text('1')
        (settings / 'hud_guidance').write_text('1')
        (settings / 'mm-ipod').write_text('#!/bin/ksh\nexec wrapper\n')
        (settings / 'bin').mkdir()
        (settings / 'bin/mm-ipod').write_bytes(self.p['MMIPOD'].read_bytes())
        old_plugin = {f: (self.p['PLUGIN'] / f).read_bytes() for f in FILES}
        self.pl = self.payload(tag=b'v2')
        self.run_script('install')
        self.assert_plugin()
        b = self.backups('install')[-1]
        for f in FILES:
            self.assertEqual((b / 'plugin' / f).read_bytes(), old_plugin[f], f)
        self.assertEqual((settings / 'fullscreen').read_text(), '1')                 # switches stay in place
        self.assertEqual((b / 'settings-copy/hud_guidance').read_text(), '1')
        self.assertFalse((b / 'settings-copy/bin').exists())
        self.assertEqual(self.hooked(), 1)                                            # not added twice
        self.assertIn('boot v2', self.calls())                                        # the new boot.sh relinked

    def test_existing_link_is_removed_to_reach_original_then_relinked(self):
        settings = self.p['SETTINGS']
        settings.mkdir(exist_ok=True)
        (settings / 'hud_guidance').write_text('1')
        (settings / 'mm-ipod').write_text('#!/bin/ksh\nexec wrapper\n')
        real = self.p['MMIPOD'].read_bytes()
        self.p['MMIPOD'].unlink()
        self.p['MMIPOD'].symlink_to(settings / 'mm-ipod')
        out = self.run_script('install')
        self.assertIn('Removed the earlier link', out)
        b = self.backups('install')[-1]
        self.assertIn(str(self.p['MMIPOD']), (b / 'unlinked').read_text())
        self.assertTrue(self.p['MMIPOD'].is_symlink())                                # made again by boot.sh
        self.assertNotEqual(real, b'')

    def test_firmware_change_after_first_backup_is_kept_too(self):
        self.run_script('install')
        self.p['CONNECTIVITY'].write_bytes(elf(b'updated Connectivity', etype=2))
        out = self.run_script('install')
        self.assertIn('differs from the kept original', out)
        b = self.backups('install')[-1]
        self.assertEqual((b / 'system/Connectivity').read_bytes(), self.p['CONNECTIVITY'].read_bytes())
        self.assertNotEqual((self.p['BACKUPS'] / 'originals/Connectivity').read_bytes(), self.p['CONNECTIVITY'].read_bytes())

    def test_hook_from_hook_tool_is_recognised(self):
        line = b'[ -f x/boot.sh ] && ksh x/boot.sh > /dev/null 2>&1   # ' + MARK + b' (tools/car/carplaymenu_hook.sh)'
        lines = START_SCREEN.split(b'\n')
        self.p['START_SCREEN'].write_bytes(b'\n'.join(lines[:1] + [line] + lines[1:]))
        out = self.run_script('install')
        self.assertIn('already had the hook', out)
        self.assertIn('replace the older boot-hook line', out)       # it only tried /var, which does not exist yet at boot
        self.assertEqual(self.hooked(), 1)
        ss = self.p['START_SCREEN'].read_bytes().split(b'\n')
        self.assertIn(b'CPM_BOOT=/fs/sda1/opt/hmi/data/ID5/Plugins/CarPlayMenu/boot.sh', ss[1])
        self.assertEqual(b'\n'.join(ss[:1] + ss[2:]), START_SCREEN)
        self.assertEqual((self.p['BACKUPS'] / 'originals/start_screen.sh').read_bytes(), START_SCREEN)
        self.assertNotIn('replace the older', self.run_script('install'))   # current line: left as it is

    def test_hook_line_runs_boot_sh(self):
        self.run_script('install')
        line = self.p['START_SCREEN'].read_bytes().split(b'\n')[1].decode()
        (self.p['PLUGIN'] / 'boot.sh').write_text('echo ran > %s\n' % (self.root / 'ran'))
        subprocess.run(['/bin/sh', '-c', line.replace('/bin/ksh', 'sh')], check=True)   # /fs/sda1 is absent here: the /var copy runs
        self.assertEqual((self.root / 'ran').read_text(), 'ran\n')

    def test_failure_after_switch_rolls_back(self):
        self.run_script('install')
        old_plugin = {f: (self.p['PLUGIN'] / f).read_bytes() for f in FILES}
        hooked = self.p['START_SCREEN'].read_bytes()
        self.p['START_SCREEN'].write_bytes(START_SCREEN)                              # as if someone removed the hook
        out = self.run_script('install', rc=1, TEST_SYNC_RC=1)
        self.assertIn('Previous files restored', out)
        for f in FILES:
            self.assertEqual((self.p['PLUGIN'] / f).read_bytes(), old_plugin[f], f)
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)
        b = self.backups('install')[-1]
        self.assertTrue((b / 'ROLLED_BACK').exists())
        self.assertFalse((b / 'COMMITTED').exists())
        self.assertFalse(self.p['LOCK'].exists())
        self.assertNotEqual(hooked, START_SCREEN)

    def test_fresh_install_failure_rolls_back_to_nothing(self):
        out = self.run_script('install', rc=1, TEST_SYNC_RC=1)
        self.assertIn('Previous files restored', out)
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)

    def test_restart_after_success(self):
        out = self.run_script('install', '--restart-hmi')
        self.assertIn('appreset', self.calls()[-1])
        self.assertIn('OnOffDSICommander appreset', out)
        self.assertFalse(self.p['LOCK'].exists())

    def test_restart_without_onoff_uses_plugin_reboot(self):
        (self.bin / 'OnOffDSICommander').unlink()                     # HMI ID5_1703-490 has none
        out = self.run_script('install', '--restart-hmi', rc=3)       # the host's cat ignores the preload: "did not happen"
        self.assertIn('libcpreboot.so', out)
        self.assertIn('reboot did not happen', out)
        self.assert_plugin()
        self.assertFalse(self.p['LOCK'].exists())

    def test_restart_action(self):
        self.assertIn('would be restarted', self.run_script('restart', '--dry-run'))
        self.assertNotIn('appreset', ' '.join(self.calls()))
        self.run_script('restart')
        self.assertIn('appreset', ' '.join(self.calls()))
        self.assertIn('goes with install or uninstall', self.run_script('status', '--restart-hmi', rc=1))

    def test_here_documents_without_tmp(self):
        """A unit without /tmp: ksh would fail on the first here-document ("can't create temporary file"), so TMPDIR becomes
        /dev/shmem; an existing /tmp or a TMPDIR that is set is left alone."""
        self.stub('pidin', 'echo "tmpdir ${TMPDIR:-unset}" >> "$TEST_CALLS"\n'
                  'if [ "$1" = -p ]; then echo "pid euid"; echo "$2 ${TEST_EUID:-0}"; exit 0; fi\necho "pid Arguments"')
        self.p['SHMEM'].mkdir()
        env = {k: v for k, v in self.env.items() if k != 'TMPDIR'}
        res = subprocess.run([self.SHELL, str(self.script), 'status'], env=env, capture_output=True, timeout=60)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn('tmpdir %s' % self.p['SHMEM'], self.calls())
        self.calls_file.unlink()
        self.p['SHELL_TMP'].mkdir()
        subprocess.run([self.SHELL, str(self.script), 'status'], env=env, capture_output=True, timeout=60, check=True)
        self.assertIn('tmpdir unset', self.calls())
        self.calls_file.unlink()
        self.run_script('status', TMPDIR='/somewhere')
        self.assertIn('tmpdir /somewhere', self.calls())

    def test_restart_failure_is_exit_3(self):
        self.run_script('install', '--restart-hmi', rc=3, TEST_HMI_RC=1)
        self.assert_plugin()

    def test_forwarded_from_jacinto(self):
        out = self.run_script('install', '--dry-run', TEST_NODE='hu-jacinto')
        self.assertIn('Entering hu-omap', out)
        self.assertIn('DRY RUN', out)
        self.assertEqual(self.calls()[0], 'on')

    def test_forwarding_loop_refused(self):
        out = self.run_script('install', TEST_NODE='hu-jacinto', TEST_FORWARD_NODE='hu-jacinto', rc=1)
        self.assertIn('forwarding loop', out)


class FromComputerTest(Base):
    """Runs on a computer: one SSH connection, upload to STAGE on hu-omap, the script runs itself there."""
    def computer(self, *args, rc=0, **env):
        return self.run_script(*args, rc=rc, TEST_SYSNAME='Linux', **env)

    def test_install_over_ssh(self):
        out = self.computer('install')
        self.assertIn('Uploaded plugin/', out)
        self.assertIn('Done: CarPlay Advanced Settings is installed', out)
        self.assert_plugin()
        self.assertEqual(self.hooked(), 1)
        self.assertFalse(self.p['STAGE'].exists())                                    # upload removed
        calls = self.calls()
        self.assertEqual(calls[0], 'nc -z -G 4 169.254.199.99 22')                        # reachable first
        self.assertEqual(calls[1:4], ['ssh-master', 'askpass:pw & for tests', 'ssh-host root@169.254.199.99'])
        self.assertEqual(calls.count('ssh-master'), 1)                                # one login
        self.assertIn('on', calls)                                                    # sshd on hu-jacinto: on -f hu-omap
        (b,) = self.backups('install')                                                # the run's backup, copied to BACKUP/
        local = self.root / 'BACKUP'
        self.assertIn('Backup copied to BACKUP/%s' % b.name, out)
        self.assertTrue((local / b.name / 'installer-output.log').is_file())             # the head unit's full output
        for f in b.rglob('*'):
            if f.is_file():
                self.assertEqual((local / b.name / f.relative_to(b)).read_bytes(), f.read_bytes(), f)
        self.assertEqual((local / 'originals/Connectivity').read_bytes(), self.p['CONNECTIVITY'].read_bytes())
        self.assertEqual((local / 'originals/start_screen.sh').read_bytes(), START_SCREEN)
        self.assertNotIn('appreset', ' '.join(calls))

    def test_supported_by_hash(self):
        self.p['HMI_VERSION_FILE'].write_bytes(b'NBTevo_ID5_1703_Release ID5_1703-490-1650ZD Build 139')
        self.p['HMI_BIN'].write_bytes(HMI_1703)
        out = self.computer('install')
        self.assertIn('Supported software: TEST_1703', out)
        self.assertIn('supported software TEST_1703, checked by hash', out)
        self.assertIn('Done: CarPlay Advanced Settings is installed', out)

    def test_plain_hmi_binary_when_there_is_no_zipped_one(self):
        self.p['HMI_BIN'].unlink()
        self.p['HMI_BIN_PLAIN'].write_bytes(HMI_006)
        self.assertIn('Supported software: TEST_006', self.computer('install', '--dry-run'))

    def test_unsupported_software_is_refused_with_an_issue_link(self):
        self.p['HMI_VERSION_FILE'].write_bytes(b'NBTevo_ID5_1903_Release ID5_1903-999 Build 12')
        self.p['HMI_BIN'].write_bytes(b'HmiMain3D other')
        out = self.computer('install', rc=1)
        self.assertIn('not in the supported list', out)
        self.assertIn(sha(b'HmiMain3D other'), out)
        self.assertIn('Nothing was installed', out)
        self.assertFalse(self.p['STAGE'].exists())                                    # nothing uploaded
        self.assertFalse(self.p['PLUGIN'].exists())
        (link,) = [l.strip() for l in out.splitlines() if l.strip().startswith('https://github.com/')]
        url = urllib.parse.urlsplit(link)
        self.assertEqual(url.path, '/st1ng/nbtevo-carplay-advanced/issues/new')
        q = urllib.parse.parse_qs(url.query)
        self.assertEqual(q['title'], ['Support for HMI NBTevo_ID5_1903_Release ID5_1903-999 Build 12'])
        self.assertIn('HmiMain3D sha256: %s' % sha(b'HmiMain3D other'), q['body'][0].splitlines())
        self.assertIn('libconn_carplay.so sha256: %s' % sha(CONN), q['body'][0].splitlines())
        # status reports it too, but still runs
        out = self.computer('status')
        self.assertIn('not in the supported list', out)
        self.assertIn('Plugin: not installed', out)

    def test_missing_library_is_unsupported(self):
        self.p['CONN_LIB'].unlink()
        out = self.computer('install', rc=1)
        self.assertIn('libconn_carplay.so  missing', out)

    def test_hmi_option_refused_from_a_computer(self):
        self.assertIn('--hmi is for runs on the head unit', self.computer('install', '--hmi', 'TEST_006', rc=1))

    def test_login_with_the_password(self):
        self.computer('status')
        self.assertIn('askpass:pw & for tests', self.calls())                         # ssh got it from the helper

    def test_login_without_a_password(self):
        self.script.write_text(self.script.read_text().replace("SSH_PASSWORD='pw & for tests'", 'SSH_PASSWORD='))
        self.computer('status')
        self.assertIn('ssh-master', self.calls())
        self.assertFalse([c for c in self.calls() if c.startswith('askpass')])         # ssh asks itself

    def test_install_landing_on_omap(self):
        out = self.computer('install', '--host', 'root@10.0.0.2', TEST_LAND='hu-omap')
        self.assertIn('Done: CarPlay Advanced Settings is installed', out)
        self.assertNotIn('on', self.calls())
        self.assertIn('ssh-host root@10.0.0.2', self.calls())

    def test_dry_run_over_ssh(self):
        out = self.computer('install', '--dry-run')
        self.assertIn('DRY RUN', out)
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual(self.hooked(), 0)
        self.assertFalse(self.p['STAGE'].exists())
        self.assertFalse((self.root / 'BACKUP').exists())

    def test_restart_after_the_backup_copy(self):
        out = self.computer('install', '--restart-hmi')
        self.assertIn('appreset', ' '.join(self.calls()))
        self.assertLess(out.index('Backup copied'), out.index('Restarting the head unit'))
        self.assertTrue(any((self.root / 'BACKUP').glob('*-install/COMMITTED')))

    def test_restart_does_not_wait_for_the_dropped_connection(self):
        # a real reboot leaves the SSH session hanging until a timeout: the installer stops once the head unit says it restarts
        import time
        t0 = time.monotonic()
        out = self.computer('install', '--restart-hmi', TEST_HMI_SLEEP='60')
        self.assertLess(time.monotonic() - t0, 40)
        self.assertIn('The head unit is restarting', out)

    def test_plugin_checked_before_connecting(self):
        (self.pl / 'librghook.so').write_bytes(elf(machine=3))                       # x86
        self.assertIn('Not an ARM shared library', self.computer('install', rc=1))
        self.assertEqual(self.calls(), [])
        self.pl = self.payload()
        with zipfile.ZipFile(self.pl / 'CarPlayMenu_ALL_Texts.zip', 'w') as z:
            z.writestr('db', b'texts')
        (self.pl / 'CarPlayMenu_ALL_Texts.zip').write_bytes(b'XX' + (self.pl / 'CarPlayMenu_ALL_Texts.zip').read_bytes())
        self.assertIn('Invalid ZIP signature', self.computer('install', rc=1))
        self.pl = self.payload()
        (self.pl / 'CarPlayMenu.manifest').write_text('<plugin/>\n')
        self.assertIn('does not name the CarPlayMenu plugin', self.computer('install', rc=1))
        self.assertEqual(self.calls(), [])

    def test_status_and_uninstall_over_ssh(self):
        self.computer('install')
        self.assertIn('Plugin: installed', self.computer('status'))
        out = self.computer('uninstall')
        self.assertIn('Done: CarPlay Advanced Settings is uninstalled', out)
        self.assertTrue(any((self.root / 'BACKUP').glob('*-uninstall/COMMITTED')))
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual(self.hooked(), 0)

    def test_failure_on_the_head_unit_is_reported(self):
        out = self.computer('install', rc=1, TEST_EUID=501)
        self.assertIn('Root SSH access could not be verified', out)
        self.assertIn('reported a failure (exit 1)', out)
        self.assertFalse(self.p['STAGE'].exists())
        self.assertFalse((self.root / 'BACKUP').exists())

    def test_missing_plugin_file(self):
        (self.pl / 'libcppinch.so').unlink()
        self.assertIn('build.sh', self.computer('install', rc=1))
        self.assertEqual(self.calls(), [])                                            # nothing sent

    def test_connection_failure(self):
        self.assertIn('Cannot connect', self.computer('status', rc=1, TEST_SSH_RC=255))

    def test_unreachable_head_unit(self):
        out = self.computer('install', rc=1, TEST_NC_RC=1)
        self.assertIn('does not answer on port 22', out)
        self.assertNotIn('ssh-master', self.calls())                                 # no login tried

    def test_summary(self):
        out = self.computer('install')
        self.assertIn('Done: CarPlay Advanced Settings is installed', out)
        self.assertIn('A restart of the head unit is needed', out)
        self.assertNotIn('SUCCESS: install completed', out)                          # the head unit's closing lines: in the summary
        self.assertIn('answers on port 22', out)

    def test_payload_option_refused(self):
        self.assertIn('--payload is for runs on the head unit', self.computer('install', '--payload', '/x', rc=1))


class UninstallStatusTest(Base):
    def install_with_switches(self):
        self.run_script('install')
        settings = self.p['SETTINGS']
        settings.mkdir(exist_ok=True)
        (settings / 'hud_guidance').write_text('1')
        (settings / 'mm-ipod').write_text('#!/bin/ksh\nexec wrapper\n')
        self.p['MMIPOD'].unlink()
        self.p['MMIPOD'].symlink_to(settings / 'mm-ipod')

    def test_uninstall(self):
        self.install_with_switches()
        installed = {f: (self.p['PLUGIN'] / f).read_bytes() for f in FILES}
        out = self.run_script('uninstall')
        self.assertIn('SUCCESS: uninstall completed', out)
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertFalse(self.p['SETTINGS'].exists())
        self.assertFalse(self.p['MMIPOD'].is_symlink())
        self.assertEqual(self.p['START_SCREEN'].read_bytes(), START_SCREEN)
        self.assertIn('identical to the kept original', out)
        (b,) = self.backups('uninstall')
        for f in FILES:
            self.assertEqual((b / 'plugin' / f).read_bytes(), installed[f])
        self.assertEqual((b / 'settings/hud_guidance').read_text(), '1')
        self.assertEqual((b / 'start_screen.sh').read_bytes().count(MARK), 1)
        self.assertEqual(self.calls().count('boot new'), 1)                         # only the install's relink
        self.assertIn('Already uninstalled', self.run_script('uninstall'))

    def link_both_layouts(self):
        # boot.sh links both spellings of the layout; in the car /opt/hmi/data is a link to /opt/hmi/ID5/data, so each link alone
        # shadows the same file (HMI 1703, 2026-11-13: uninstall stopped with "still a link after removing it")
        settings = self.p['SETTINGS']
        settings.mkdir(exist_ok=True)
        (settings / 'fullscreen').write_text('1')
        (settings / 'Spec_State_Merged.pbw').write_bytes(b'PBW wide layout')
        for key in ('LAYOUT', 'LAYOUT_ID5'):
            self.p[key].unlink(missing_ok=True)
            self.p[key].symlink_to(settings / 'Spec_State_Merged.pbw')

    def test_uninstall_removes_both_layout_links(self):
        self.install_with_switches()
        self.link_both_layouts()
        out = self.run_script('uninstall')
        self.assertIn('SUCCESS: uninstall completed', out)
        self.assertFalse(self.p['LAYOUT'].is_symlink())
        self.assertFalse(self.p['LAYOUT_ID5'].is_symlink())

    def test_uninstall_removes_both_common_layout_links(self):
        # a BMW: the plugin's copy of the HMI's common layout, linked under both spellings by boot.sh (a symlink here replaces the
        # file; a path-manager link in the car only shadows it)
        self.install_with_switches()
        settings = self.p['SETTINGS']
        (settings / 'fullscreen').write_text('1')
        (settings / 'Spec_State_Merged-common.pbw').write_bytes(b'PBW wide common layout')
        for key in ('LAYOUT_COMMON', 'LAYOUT_COMMON_ID5'):
            self.p[key].unlink(missing_ok=True)
            self.p[key].symlink_to(settings / 'Spec_State_Merged-common.pbw')
        self.assertIn('shadowed by a link', self.run_script('status'))
        out = self.run_script('uninstall')
        self.assertIn('SUCCESS: uninstall completed', out)
        self.assertNotIn('still a link', out)
        self.assertFalse(self.p['LAYOUT_COMMON'].is_symlink())
        self.assertFalse(self.p['LAYOUT_COMMON_ID5'].is_symlink())

    def test_uninstall_both_layout_links_without_kept_original(self):
        self.install_with_switches()
        orig = self.p['BACKUPS'] / 'originals'
        for f in orig.glob('Spec_State_Merged.pbw*'):
            f.unlink()
        self.link_both_layouts()
        out = self.run_script('uninstall')
        self.assertIn('SUCCESS: uninstall completed', out)
        self.assertNotIn('still a link', out)
        self.assertFalse(self.p['LAYOUT'].is_symlink())
        self.assertFalse(self.p['LAYOUT_ID5'].is_symlink())

    def test_uninstall_keep_config(self):
        self.install_with_switches()
        self.run_script('uninstall', '--keep-config')
        self.assertFalse(self.p['PLUGIN'].exists())
        self.assertEqual((self.p['SETTINGS'] / 'hud_guidance').read_text(), '1')
        self.assertEqual(self.hooked(), 0)

    def test_uninstall_dry_run(self):
        self.install_with_switches()
        before = sorted(str(p) for p in self.root.rglob('*'))
        out = self.run_script('uninstall', '--dry-run')
        self.assertIn('remove the boot-hook line', out)
        self.assertEqual(sorted(str(p) for p in self.root.rglob('*')), before)
        self.assertEqual(self.hooked(), 1)

    def test_uninstall_failure_rolls_back_and_relinks(self):
        self.install_with_switches()
        out = self.run_script('uninstall', rc=1, TEST_SYNC_RC=1)
        self.assertIn('Previous files restored', out)
        self.assertTrue(self.p['PLUGIN'].exists())
        self.assertTrue((self.p['SETTINGS'] / 'hud_guidance').exists())
        self.assertEqual(self.hooked(), 1)
        self.assertTrue(self.p['MMIPOD'].is_symlink())                                # made again by the restored boot.sh

    def test_keep_config_needs_uninstall(self):
        self.assertIn('requires uninstall', self.run_script('install', '--keep-config', rc=1))

    def test_status(self):
        self.install_with_switches()
        (self.root / 'processes').write_text('1234 /opt/hmi/bin/HmiMain3D -c=DSI\n77 mm-ipod -x\n')
        out = self.run_script('status')
        self.assertIn('HMI: NBTevo_ID5_1803_Release', out)
        self.assertIn('Plugin: installed', out)
        self.assertIn('Boot hook: installed', out)
        self.assertIn('hud_guidance', out)
        self.assertIn('%s: shadowed by a link' % self.p['MMIPOD'], out)
        self.assertIn('HmiMain3D PID(s): 1234', out)
        self.assertIn('mm-ipod PID(s): 77', out)
        self.assertIn('Connectivity PID(s): none', out)


class ClusterTest(Base):
    """Where CarPlay directions go (libcpprobe.so's answer, or --cluster most|kombi|both): librghook-kombi.so as librghook.so for
    kombi and both, librghook.so for most; recorded in carplaymenu/cluster (hud.h reads it: kombi = nothing drawn on MOST)."""
    def cluster(self):
        f = self.p['SETTINGS'] / 'cluster'
        return f.read_text().strip() if f.exists() else None

    def rghook(self):
        got = (self.p['PLUGIN'] / 'librghook.so').read_bytes()
        for name in ('librghook.so', 'librghook-kombi.so', 'librghook-most.so', 'librghook-both.so'):
            if got == (self.pl / name).read_bytes():
                return name
        return None

    def test_ethernet_kombi(self):
        out = self.run_script('install', TEST_PROBE='kombi=1')
        self.assertIn('Ethernet KOMBI found', out)
        self.assertIn('kombi: CarPlay directions to the KOMBI over SOME/IP from mm-ipod (librghook-kombi.so', out)
        self.assertEqual(self.rghook(), 'librghook-kombi.so')
        self.assertEqual(self.cluster(), 'kombi')
        self.assertIn('CarPlay directions (recorded at install): kombi', self.run_script('status', TEST_PROBE='kombi=1'))

    def test_most(self):
        out = self.run_script('install', TEST_PROBE='kombi=0 refused or unreachable')
        self.assertIn('no Ethernet KOMBI (refused or unreachable)', out)
        self.assertEqual(self.rghook(), 'librghook.so')
        self.assertEqual(self.cluster(), 'most')

    def test_no_answer_installs_most(self):
        out = self.run_script('install')
        self.assertIn('Cluster: not detected', out)
        self.assertIn('--cluster kombi', out)
        self.assertEqual(self.rghook(), 'librghook.so')
        self.assertEqual(self.cluster(), 'most')

    def test_forced(self):
        out = self.run_script('install', '--cluster', 'kombi', TEST_PROBE='kombi=0 refused or unreachable')
        self.assertIn('Cluster: kombi (--cluster)', out)
        self.assertNotIn('no Ethernet KOMBI', out)                    # the probe is not run
        self.assertEqual(self.rghook(), 'librghook-kombi.so')
        self.run_script('install', '--cluster', 'most', TEST_PROBE='kombi=1')
        self.assertEqual(self.rghook(), 'librghook.so')
        self.assertEqual(self.cluster(), 'most')
        out = self.run_script('install', '--cluster', 'both', TEST_PROBE='kombi=1')
        self.assertIn('both: the KOMBI over SOME/IP and the MOST HUDTerminal (librghook-kombi.so)', out)
        self.assertEqual(self.rghook(), 'librghook-kombi.so')
        self.assertEqual(self.cluster(), 'both')
        self.assertIn('--cluster needs most, kombi or both', self.run_script('install', '--cluster', 'x', rc=1))

    def hud_only(self, probe, *extra):
        return self.run_script('install', '--kind', 'id6', '--mode', 'hud', *extra, TEST_PROBE=probe)

    # without the menu plugin librghook itself draws: -most on MOST, -kombi on the KOMBI, -both for the two (fresh units: a second
    # HUD-only install on top of one stops at the mm-ipod link, as before)
    def test_hud_only_most(self):
        out = self.hud_only('kombi=0 refused or unreachable')
        self.assertIn('most: CarPlay directions on the MOST HUDTerminal (librghook-most.so)', out)
        self.assertIn('Drawn on MOST from mm-ipod (no menu plugin)', out)
        self.assertEqual(self.rghook(), 'librghook-most.so')
        self.assertTrue((self.p['PLUGIN'] / 'libcpprobe.so').exists())

    def test_hud_only_kombi(self):
        out = self.hud_only('kombi=1')
        self.assertNotIn('Drawn on MOST', out)
        self.assertEqual(self.rghook(), 'librghook-kombi.so')
        self.assertEqual(self.cluster(), 'kombi')

    def test_hud_only_both(self):
        self.hud_only('kombi=1', '--cluster', 'both')
        self.assertEqual(self.rghook(), 'librghook-both.so')
        self.assertEqual(self.cluster(), 'both')

    def test_dry_run_writes_nothing(self):
        out = self.run_script('install', '--dry-run', TEST_PROBE='kombi=1')
        self.assertIn('Ethernet KOMBI found', out)
        self.assertIsNone(self.cluster())
        self.assertFalse(self.p['PLUGIN'].exists())


def per_shell(cls):
    """One test class per shell (/bin/sh and ksh)."""
    out = {}
    for sh in SHELLS:
        name = '%s_%s' % (cls.__name__, os.path.basename(sh))
        out[name] = type(name, (cls, unittest.TestCase), {'SHELL': sh})
    return out


globals().update(per_shell(InstallTest))
globals().update(per_shell(UninstallStatusTest))
globals().update(per_shell(FromComputerTest))
globals().update(per_shell(ClusterTest))
del InstallTest, UninstallStatusTest, FromComputerTest, ClusterTest

if __name__ == '__main__':
    unittest.main()

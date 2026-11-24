"""HUD size menu: the real cpm.cpp setting functions with only local file/model I/O stubbed."""
import ast
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <map>
#include <string>
typedef unsigned u32;
static std::map<std::string, std::string> files;
static std::map<int, std::string> descriptors;
static std::string failure;
static unsigned nextFd = 10;
static bool ticks[3];
static const char *kCfgDir = "/var/opt/hmi/data/rw/carplaymenu";
static std::string key(const char *p) { const char *q = strrchr(p, '/'); return q ? q + 1 : p; }
static int fileOpen(const char *p, int mode, ...) {
  std::string k = key(p);
  if (failure == "open" && k == "hud_box.menu.tmp") { errno = EACCES; return -1; }
  if (failure == "readopen" && k == "hud_box") { errno = EACCES; return -1; }
  if (!(mode & 0x100) && !files.count(k)) { errno = ENOENT; return -1; }
  if (mode & 0x200) files[k] = "";
  descriptors[++nextFd] = k;
  return nextFd;
}
static long fileRead(int fd, void *to, unsigned long cap) {
  if (failure == "read") { errno = EIO; return -1; }
  const std::string &s = files[descriptors[fd]];
  unsigned long n = s.size() < cap ? s.size() : cap;
  memcpy(to, s.data(), n);
  return n;
}
static long fileWrite(int fd, const void *p, unsigned long n) {
  if (failure == "write") { errno = EIO; return -1; }
  if (failure == "short" && n) --n;
  files[descriptors[fd]].append((const char *)p, n);
  return n;
}
static int fileClose(int fd) {
  bool fail = failure == "close" && descriptors[fd] == "hud_box.menu.tmp";
  descriptors.erase(fd);
  if (fail) { errno = EIO; return -1; }
  return 0;
}
static int fileUnlink(const char *p) {
  std::string k = key(p);
  if ((failure == "unlink" && k == "hud_box") || (failure == "legacy" && k == "hud_box_6wa")) {
    errno = EACCES; return -1;
  }
  if (!files.erase(k)) { errno = ENOENT; return -1; }
  return 0;
}
static int fileRename(const char *a, const char *b) {
  if (failure == "rename") { errno = EIO; return -1; }
  files[key(b)] = files[key(a)]; files.erase(key(a)); return 0;
}
static int fileMkdir(const char *, unsigned) { return 0; }
static void logLine(const char *s) { printf("LOG %s\n", s); }
struct App { void updateModel(unsigned m, bool b) { if (m >= 119 && m <= 121) ticks[m - 119] = b; } } app;
static App *g_app = &app;
#define O_WRONLY 1
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define CPM_ERRNO errno
#define open fileOpen
#define read fileRead
#define write fileWrite
#define close fileClose
#define unlink fileUnlink
#define rename fileRename
#define mkdir fileMkdir
// PRODUCTION
#undef open
#undef read
#undef write
#undef close
#undef unlink
#undef rename
#undef mkdir
int main() {
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    if (!strncmp(line, "put ", 4)) {
      char *p = strchr(line + 4, ' '); if (p) { *p++ = 0; files[line + 4] = p; }
    } else if (!strncmp(line, "fail ", 5)) failure = line + 5;
    else if (!strncmp(line, "set ", 4)) {
      int k = -1; sscanf(line + 4, "%d", &k);
      bool ok = setHudSize(k);
      printf("SET %d OK %d LEVEL %d TICKS %d%d%d\n", k, ok, hudSizeLevel(), ticks[0], ticks[1], ticks[2]);
    } else if (!strcmp(line, "level")) printf("LEVEL %d\n", hudSizeLevel());
    else if (!strncmp(line, "get ", 4)) {
      std::string k = line + 4;
      printf("FILE %s ", k.c_str());
      if (!files.count(k)) printf("ABSENT");
      else for (unsigned char c : files[k]) printf("%02x", c);
      printf("\n");
    }
  }
}
'''


class HudProfileMenu(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cpp = (ROOT / 'src/cpm.cpp').read_text()
        config = cpp[cpp.index('struct HudSize {'):cpp.index('\nvoid optPath(')]
        setter = cpp[cpp.index('bool setHudSize(int k) {'):cpp.index('\nbool onHudSizeAuto(')]
        cls.tmp = tempfile.TemporaryDirectory(prefix='hud-profile-menu-')
        source = Path(cls.tmp.name) / 'menu.cpp'
        source.write_text(HARNESS.replace('// PRODUCTION', config + '\n' + setter))
        cls.exe = Path(cls.tmp.name) / 'menu'
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_lines(self, commands):
        return subprocess.run([str(self.exe)], input='\n'.join(commands) + '\n', text=True,
                              capture_output=True, check=True).stdout.splitlines()

    def test_auto_is_default_and_removes_both_old_overrides(self):
        out = self.run_lines(['level', 'put hud_box custom', 'put hud_box_6wa 1', 'set 0',
                              'get hud_box', 'get hud_box_6wa'])
        self.assertEqual(out[0], 'LEVEL 0')
        self.assertIn('SET 0 OK 1 LEVEL 0 TICKS 100', out)
        self.assertEqual(out[-2:], ['FILE hud_box ABSENT', 'FILE hud_box_6wa ABSENT'])

    def test_compact_persists_the_known_53_pixel_box(self):
        out = self.run_lines(['put hud_box_6wa 1', 'set 1', 'get hud_box', 'get hud_box_6wa', 'get hud_box.menu.tmp'])
        self.assertIn('SET 1 OK 1 LEVEL 1 TICKS 010', out)
        self.assertIn('FILE hud_box ' + b'36 16 89 69\n'.hex(), out)
        self.assertEqual(out[-2:], ['FILE hud_box_6wa ABSENT', 'FILE hud_box.menu.tmp ABSENT'])

    def test_large_persists_the_existing_6wa_profile(self):
        out = self.run_lines(['set 2', 'get hud_box'])
        self.assertIn('SET 2 OK 1 LEVEL 2 TICKS 001', out)
        self.assertEqual(out[-1], 'FILE hud_box ' + b'6wa\n'.hex())

    def test_custom_box_has_no_false_preset_even_with_legacy_flag(self):
        out = self.run_lines(['put hud_box 35 15 85 65', 'level', 'put hud_box_6wa 1', 'level'])
        self.assertEqual(out, ['LEVEL -1', 'LEVEL -1'])

    def test_numeric_box_overrides_legacy_flag_as_in_choose_box(self):
        out = self.run_lines(['put hud_box_6wa 1', 'put hud_box 36 16 89 69', 'level'])
        self.assertEqual(out, ['LEVEL 1'])

    def test_other_hud_settings_are_untouched(self):
        settings = {'hud_hide': '21 22', 'hud_layout': '6wb', 'hud_handles': '1 2 3 4 5 6 22'}
        out = self.run_lines(['put %s %s' % pair for pair in settings.items()] + ['set 1', 'set 2', 'set 0'] +
                             ['get ' + name for name in settings])
        for name, value in settings.items():
            self.assertIn('FILE %s %s' % (name, value.encode().hex()), out)

    def test_write_failures_preserve_previous_box_and_show_actual_profile(self):
        for fail in ('open', 'write', 'short', 'close', 'rename'):
            with self.subTest(fail=fail):
                out = self.run_lines(['put hud_box 6wa', 'fail ' + fail, 'set 1', 'get hud_box', 'get hud_box.menu.tmp'])
                self.assertIn('SET 1 OK 0 LEVEL 2 TICKS 001', out)
                self.assertEqual(out[-2:], ['FILE hud_box ' + b'6wa'.hex(), 'FILE hud_box.menu.tmp ABSENT'])

    def test_failed_auto_unlink_does_not_claim_auto(self):
        out = self.run_lines(['put hud_box 36 16 89 69', 'fail unlink', 'set 0', 'get hud_box'])
        self.assertIn('SET 0 OK 0 LEVEL 1 TICKS 010', out)
        self.assertEqual(out[-1], 'FILE hud_box ' + b'36 16 89 69'.hex())

    def test_failed_legacy_removal_shows_the_effective_numeric_box(self):
        out = self.run_lines(['put hud_box_6wa 1', 'fail legacy', 'set 1'])
        self.assertIn('SET 1 OK 0 LEVEL 1 TICKS 010', out)

    def test_failed_auto_legacy_removal_shows_remaining_large_override(self):
        out = self.run_lines(['put hud_box 36 16 89 69', 'put hud_box_6wa 1', 'fail legacy', 'set 0'])
        self.assertIn('SET 0 OK 0 LEVEL 2 TICKS 001', out)

    def test_read_errors_do_not_masquerade_as_auto(self):
        for failure in ('readopen', 'read'):
            with self.subTest(failure=failure):
                out = self.run_lines(['put hud_box 36 16 89 69', 'fail ' + failure, 'level'])
                self.assertEqual(out, ['LEVEL -1'])

    def test_oversized_or_empty_config_is_custom(self):
        for value in ('', '6wa' + ' ' * 70, 'mini'):
            with self.subTest(value=value):
                self.assertEqual(self.run_lines(['put hud_box ' + value, 'level']), ['LEVEL -1'])

    def test_invalid_selection_does_not_write(self):
        out = self.run_lines(['put hud_box 6wa', 'set -1', 'set 3', 'get hud_box'])
        self.assertEqual(out[-1], 'FILE hud_box ' + b'6wa'.hex())
        self.assertTrue(all('OK 0' in line for line in out if line.startswith('SET')))

    def test_menu_actions_models_and_labels_are_wired(self):
        app = ET.parse(ROOT / 'plugin/CarPlayMenu.xml').getroot().find('pluginApp')
        page = app.find("hmiStates/hmiState[@id='113']")
        radios = list(page.find('components'))
        self.assertEqual([(r.get('action'), r.get('model')) for r in radios], [('116', '119'), ('117', '120'), ('118', '121')])
        text_src = (ROOT / 'src/texts.py').read_text()
        texts = ast.literal_eval(re.search(r'TEXTS = (\{.*?\n\})', text_src, re.S).group(1))
        models = {m.get('id'): m for m in app.find('models')}
        self.assertEqual([texts[int(models[r.get('textModel')].get('textId'))] for r in radios],
                         ['Auto', 'Compact HUD (126 x 77)', 'Large HUD (199 x 129)'])
        cpp = (ROOT / 'src/cpm.cpp').read_text()
        self.assertIn('static const Stub sizes[3] = { onHudSizeAuto, onHudSizeCompact, onHudSizeLarge };', cpp)
        self.assertIn('self->addHandler(kHudSizes[k].action, d);', cpp)
        self.assertIn('self->updateModel(kHudSizes[k].model, k == hs);', cpp)

    def test_menu_checker_accepts_the_new_radio_group(self):
        subprocess.run(['python3', str(ROOT / '.claude/skills/hmi-menu/check_menu.py')], check=True, capture_output=True)

    def test_hud_only_menu_keeps_the_profile_page(self):
        app = ET.parse(ROOT / 'plugin/CarPlayMenu-hud.xml').getroot().find('pluginApp')
        self.assertIsNotNone(app.find("hmiStates/hmiState[@id='113']"))
        self.assertIsNotNone(app.find("hmiStates/hmiState[@id='81']/components/button[@action='112']"))
        self.assertEqual([r.get('action') for r in app.findall("hmiStates/hmiState[@id='113']/components/radiobutton")],
                         ['116', '117', '118'])
        subprocess.run(['python3', str(ROOT / '.claude/skills/hmi-menu/check_menu.py'), '--hud'],
                       check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()

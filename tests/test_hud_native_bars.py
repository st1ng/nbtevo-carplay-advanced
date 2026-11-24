"""Suppress the compact HUD's native bars without guessing other terminals' handles."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_hud_init import HARNESS as BASE_HARNESS, ROOT

# Use main's real HUD header; only transport, clock and route/configuration files are stubbed.
HARNESS = BASE_HARNESS.replace('extern "C" {', 'int scriptedSleep(unsigned);\nextern "C" {', 1)
HARNESS = HARNESS.replace('static unsigned g_id;', 'static unsigned testNow; static bool continuous, interruptNativeBar, lockWitness, failNextSend;\nstatic unsigned g_id;')
HARNESS = HARNESS.replace('int usleep(unsigned) { return 0; }', 'int usleep(unsigned n) { return scriptedSleep(n); }')
HARNESS = HARNESS.replace('static int sSend(void *, int) { return 0; }', 'static int sSend(void *, int) { if (g_id == kReqProcessMessage && failNextSend) { failNextSend=false; return -1; } return 0; }\nstatic void sNotify(void *, unsigned) {}')
HARNESS = HARNESS.replace('S.streamCtor = sCtor;', 'S.setNotif = sNotify; S.streamCtor = sCtor;')
HARNESS = HARNESS.replace('const unsigned char *d = p + 36;', '''const unsigned char *d = p + 36;
  if (interruptNativeBar && fkt == 0x204 && dn == 3 && d[0] == 0 && d[1] == 0x15) {
    interruptNativeBar = false;
    const unsigned char street[] = {0,2,0,0,0x41,0,0x41,0,0x44,0,0};
    inject(0x82, inst, 0x2000, g_host, 0x210, 6, street, sizeof street);
    puts("FOREIGN_AFTER_NATIVE_BAR");
  }''')
HELPERS = r'''
static int routeState=1; static char manualBox[64];
extern "C" int clock_gettime(int, hud_timespec *t) { t->sec=testNow/1000; t->nsec=(testNow%1000)*1000000; return 0; }
extern "C" int open(const char *path, int, ...) {
  if (!strcmp(path,kPps)) return 901;
  return !strcmp(path,"/var/opt/hmi/data/rw/carplaymenu/hud_box") && manualBox[0] ? 902 : -1;
}
extern "C" long read(int fd, void *to, unsigned long cap) {
  char guide[128]; const char *data=manualBox;
  if (fd==901) { snprintf(guide,sizeof guide,"route_state::%d\nmaneuver_index::0\nmaneuver_type::3\ndist_to_maneuver::30\n",routeState); data=guide; }
  else if (fd!=902) return -1;
  unsigned long n=strlen(data); if(n>cap)n=cap; memcpy(to,data,n); return n;
}
extern "C" int close(int) { return 0; }
static void foreign(const char *s) {
  unsigned inst,src,dst,fkt,op; int k=0; sscanf(s,"%x %x %x %x %x %n",&inst,&src,&dst,&fkt,&op,&k);
  unsigned char d[64];unsigned n;hex(s+k,d,&n);inject(0x82,inst,src,dst,fkt,op,d,n);
}
static void registry(const char *s) {
  unsigned t0=0,t2=0;sscanf(s,"%x %x",&t0,&t2);unsigned char d[12]={0}; unsigned count=0;
  if(t0){unsigned i=4+4*count++;d[i]=t0;d[i+1]=t0>>8;d[i+3]=0x82;}
  if(t2){unsigned i=4+4*count++;d[i]=t2;d[i+1]=t2>>8;d[i+2]=2;d[i+3]=0x82;}
  d[0]=count;char st[0x50]={0};*(unsigned*)(st+0x48)=kAttrRegistry;*(unsigned*)(st+8)=4+4*count;
  *(const unsigned char**)(st+0x10)=d;onData(0,st);
}
static void registered() {
  const unsigned char d[]={7,0,0,0,0x5e,0x20};char st[0x50]={0};
  *(unsigned*)(st+0x48)=kInfoClientResponse;*(unsigned*)(st+8)=6;*(const unsigned char**)(st+0x10)=d;onData(0,st);
}
static bool command(const char *line) {
  char cmd[24]={0};sscanf(line,"%23s",cmd);const char *p=strchr(line,' ')?strchr(line,' ')+1:"";
  if(!strcmp(cmd,"foreign"))foreign(p);
  else if(!strcmp(cmd,"registry"))registry(p);
  else if(!strcmp(cmd,"registered"))registered();
  else if(!strcmp(cmd,"detach"))onDetached(0);
  else if(!strcmp(cmd,"clock"))testNow=strtoul(p,0,10);
  else if(!strcmp(cmd,"guide"))routeState=atoi(p);
  else if(!strcmp(cmd,"answer"))sscanf(p,"%d %u %u",&g_sizeMode,&g_w,&g_h);
  else if(!strcmp(cmd,"overridebox")){snprintf(manualBox,sizeof manualBox,"%s",p);chooseBox();}
  else if(!strcmp(cmd,"handlesmap")){hudh::Map m;if(!hudh::parse(p,m))abort();useMap(m,"test map");}
  else if(!strcmp(cmd,"map")){unsigned a=atoi(p);hudh::Map m={hFrame,hStreet,hDist,hUnit,(unsigned short)a,(unsigned short)(a+1),hBar};useMap(m,"test map");}
  else if(!strcmp(cmd,"poll")){testNow=strtoul(p,0,10);measureBox();}
  else if(!strcmp(cmd,"sixwb"))g_6wb=atoi(p);
  else if(!strcmp(cmd,"barinterrupt"))interruptNativeBar=true;
  else if(!strcmp(cmd,"locked")){lockWitness=true;g_lock=1;}
  else if(!strcmp(cmd,"fail"))failNextSend=true;
  else if(!strcmp(cmd,"nativehide")){const unsigned char d[]={0,(unsigned char)atoi(p),0};send(g_host,kFBlock,g_inst,0x204,6,d,3,true);}
  else if(!strcmp(cmd,"clear"))drawClear();
  else if(!strcmp(cmd,"cycle")){continuous=true;g_enabled=1;try{worker(0);}catch(int){}continuous=false;}
  else return false;
  return true;
}
int scriptedSleep(unsigned n) {
  if(lockWitness && n==1000){lockWitness=false;g_lock=0;foreign("0 2000 101 210 6 00 02 00 00 41 00 41 00 44 00 00");puts("FOREIGN_DURING_LOCK_WAIT");}
  if(continuous && n>=300000){char line[256];while(fgets(line,sizeof line,stdin)){line[strcspn(line,"\n")]=0;
    if(!strncmp(line,"tick ",5)){testNow=strtoul(line+5,0,10);printf("TICK %u\n",testNow);return 0;}
    if(!strcmp(line,"stop"))throw 1;
    if(!command(line))abort();}throw 1;}
  return 0;
}
'''
HARNESS = HARNESS.replace('int main() {', HELPERS + '\nint main() {')
HARNESS = HARNESS.replace('if (!strcmp(cmd, "setup"))', 'if (command(line)) {}\n    else if (!strcmp(cmd, "setup"))')
STREET = '210 6 00 02 00 00 41 00 41 00 44 00 00'

SIZE = 'foreign 0 101 205e c04 d 00 05 00 7e 00 4d'
COMPACT = ['answer 1 126 77', 'poll 0']


class HudNativeBars(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='hud-native-bars-')
        source = Path(cls.tmp.name) / 'h.cpp'
        source.write_text(HARNESS)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run(['clang++', '-std=c++17', '-w', '-I', str(ROOT), '-I', str(ROOT / 'src'),
                        '-o', str(cls.exe), str(source)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_lines(self, commands):
        return subprocess.run([str(self.exe)], input='\n'.join(commands) + '\n', text=True,
                              capture_output=True, check=True).stdout.splitlines()

    def bars(self, lines, inst='00'):
        return [line for line in lines if line in (inst + ' 204 00 15 00', inst + ' 204 00 16 00')]

    def test_missing_navigation_setup_still_hides_both_compact_bars(self):
        lines = self.run_lines(COMPACT + ['draw', 'clear', 'draw'])
        self.assertEqual(self.bars(lines), ['00 204 00 15 00', '00 204 00 16 00'] * 2)
        self.assertEqual(lines.count('00 204 00 01 01'), 2)

    def test_unmeasured_and_other_buffers_are_not_guessed(self):
        for answer in ('0 0 0', '1 120 72', '1 199 129', '1 127 77'):
            with self.subTest(answer=answer):
                self.assertEqual(self.bars(self.run_lines(['answer ' + answer, 'poll 0', 'draw'])), [])

    def test_manual_picture_boxes_do_not_disable_confirmed_bar_suppression(self):
        for box in ('36 16 89 69', '6wa', '35 15 85 65'):
            with self.subTest(box=box):
                lines = self.run_lines(['overridebox ' + box, SIZE, 'draw'])
                self.assertEqual(self.bars(lines), ['00 204 00 15 00', '00 204 00 16 00'])

    def test_known_different_bar_and_custom_maps_keep_their_own_handles(self):
        for handles in ('32 64 66 65 49 50 80', '1 2 3 4 5 6 80', '1 21 3 4 5 6 0'):
            with self.subTest(handles=handles):
                lines = self.run_lines(['handlesmap ' + handles, SIZE, 'draw'])
                self.assertEqual(self.bars(lines), [])
                if handles.endswith(' 80'):
                    self.assertIn('00 204 00 50 00', lines)

    def test_primary_bar_numbers_are_not_copied_to_a_mirror(self):
        lines = self.run_lines(COMPACT + ['mirror 1', 'draw'])
        self.assertEqual(len(self.bars(lines)), 2)
        self.assertEqual(self.bars(lines, '02'), [])
        self.assertIn('02 204 00 01 01', lines)

    def test_real_sixwb_plane_does_not_get_compact_bar_numbers(self):
        self.assertEqual(self.bars(self.run_lines(COMPACT + ['sixwb 1', 'draw'])), [])

    def test_foreign_guidance_blocks_bar_hiding(self):
        lines = self.run_lines(COMPACT + ['clock 1000', 'foreign 0 2000 101 ' + STREET, 'draw'])
        self.assertEqual(self.bars(lines), [])

    def test_foreign_guidance_mid_batch_blocks_the_remaining_hide(self):
        lines = self.run_lines(COMPACT + ['clock 1000', 'barinterrupt', 'draw'])
        self.assertIn('FOREIGN_AFTER_NATIVE_BAR', lines)
        self.assertEqual(self.bars(lines), ['00 204 00 15 00'])
        after = lines[lines.index('FOREIGN_AFTER_NATIVE_BAR') + 1:]
        self.assertFalse(any(line.startswith(('00 ', '02 ')) for line in after), after)

    def test_registry_epoch_does_not_reuse_the_previous_terminals_profile(self):
        lines = self.run_lines(COMPACT + ['registry 0 0', 'registry 101 0', 'draw'])
        self.assertEqual(self.bars(lines), [])

    def test_remapped_picture_does_not_reuse_the_old_profile(self):
        lines = self.run_lines(COMPACT + ['map 49', 'map 5', 'draw'])
        self.assertEqual(self.bars(lines), [])

    def test_wrong_and_malformed_size_replies_cannot_enable_hiding(self):
        for packet in (SIZE.replace('0 101 205e', '0 102 205e'),
                       SIZE.replace('0 101 205e', '2 101 205e'),
                       SIZE.replace('0 101 205e', '0 101 201f'),
                       SIZE.replace('00 05 00', '00 06 00'),
                       SIZE + ' 00', SIZE.replace('c04 d', 'c04 f')):
            with self.subTest(packet=packet):
                self.assertEqual(self.bars(self.run_lines([packet, 'draw'])), [])

    def test_late_size_reply_with_a_fixed_box_applies_to_the_same_maneuver(self):
        lines = self.run_lines(['overridebox 36 16 89 69', 'cycle', 'tick 0', 'clock 1000', SIZE,
                                'tick 1001', 'tick 2000', 'stop'])
        late = lines[lines.index('TICK 1001') + 1:]
        self.assertEqual(self.bars(late), ['00 204 00 15 00', '00 204 00 16 00'])
        self.assertEqual(self.bars(lines[:lines.index('TICK 1001')]), [])
        steady = lines[lines.index('TICK 2000') + 1:]
        self.assertFalse(any(line.startswith(('00 ', '02 ')) for line in steady), steady)

    def test_size_change_while_route_ends_does_not_forget_to_clear_the_frame(self):
        large = SIZE.replace('00 7e 00 4d', '00 c7 00 81')
        lines = self.run_lines(['overridebox 6wa', SIZE, 'cycle', 'tick 0', 'clock 1000', large,
                                'guide 0', 'tick 1001', 'stop'])
        after = lines[lines.index('TICK 1001') + 1:]
        self.assertIn('00 204 00 01 00', after)

    def test_registration_and_detach_invalidate_the_compact_profile(self):
        for event in ('registered', 'detach'):
            with self.subTest(event=event):
                self.assertEqual(self.bars(self.run_lines(COMPACT + [event, 'draw'])), [])

    def test_ownership_is_rechecked_after_waiting_for_the_send_lock(self):
        lines = self.run_lines(COMPACT + ['clock 1000', 'locked', 'nativehide 21'])
        self.assertIn('FOREIGN_DURING_LOCK_WAIT', lines)
        self.assertEqual(self.bars(lines), [])

    def test_failed_native_hide_does_not_hide_foreign_traffic_as_our_echo(self):
        lines = self.run_lines(COMPACT + ['clock 1000', 'fail', 'nativehide 21', 'clock 1002',
                                'foreign 0 2000 101 204 6 00 15 00', 'nativehide 22'])
        self.assertNotIn('00 204 00 16 00', lines)

    def test_blocked_native_hide_does_not_hide_foreign_traffic_as_our_echo(self):
        lines = self.run_lines(COMPACT + ['clock 1000', 'foreign 0 2000 101 ' + STREET,
                                'clock 2000', 'nativehide 21', 'clock 6000',
                                'foreign 0 2000 101 204 6 00 15 00', 'clock 6500', 'nativehide 22'])
        self.assertEqual(self.bars(lines), [])

    def test_same_address_registration_reapplies_suppression_after_a_new_reply(self):
        lines = self.run_lines(['answer 1 126 77', 'cycle', 'tick 0', 'clock 1000',
                                'registered', SIZE, 'tick 1001', 'tick 2000', 'stop'])
        after = lines[lines.index('TICK 1001') + 1:]
        self.assertEqual(self.bars(after), ['00 204 00 15 00', '00 204 00 16 00'])
        self.assertEqual(self.bars(lines[lines.index('TICK 2000') + 1:]), [])


if __name__ == '__main__':
    unittest.main()

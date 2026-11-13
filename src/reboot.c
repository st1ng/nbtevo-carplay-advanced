/* libcpreboot.so — reboots the head unit (NBT Evo, run on hu-omap):  LD_PRELOAD=<plugin>/libcpreboot.so /bin/cat
 *
 * Why: in the car the system launcher does not restart the HMI (starterPackageCtrl -d0 -s/-e HMI only flips the package state; the
 * HmiMain3D process keeps running), the MINI ignores holding the volume knob, OnOffDSICommander does not exist on HMI 1703, and
 * /opt/sys/bin/sysshutdown only unmounts the disks and waits for someone else to reset. The links the plugin needs are made at boot
 * (boot.sh from /bin/start_screen.sh), so "Restart to apply" is a reboot.
 * A freestanding LD_PRELOAD (no crt for a standalone binary in this toolchain): the constructor
 * flushes the files the plugin writes (fdatasync) and every filesystem (sync), then calls sysmgr_reboot() (all in libc.so.3). The host command never runs. Exit status 1 if the reboot call returns.
 */
extern int open(const char *, int, ...);
extern int close(int);
extern int fdatasync(int);
extern long write(int, const void *, unsigned long);
extern unsigned sleep(unsigned);
extern int sysmgr_reboot(void);
extern void sync(void);
extern void _exit(int);

static void say(const char *s) {
  unsigned long n = 0;
  while (s[n])
    n++;
  write(2, s, n);
}

static const char *const kFlush[] = {
  "/var/opt/hmi/data/rw/carplaymenu", /* the switches and generated files (directory: its entries) */
  "/var/opt/hmi/data/rw/carplaymenu/carplay.ini",
  "/var/opt/hmi/data/rw/carplaymenu/Spec_State_Merged.pbw",
  "/var/opt/hmi/data/rw/Plugins/CarPlayMenu",
  "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/boot.sh",
};

__attribute__((constructor)) static void cp_reboot(void) {
  for (unsigned i = 0; i < sizeof kFlush / sizeof kFlush[0]; i++) {
    int fd = open(kFlush[i], 0);
    if (fd >= 0) {
      fdatasync(fd);
      close(fd);
    }
  }
  sync();
  say("CarPlayMenu: rebooting the head unit (sysmgr_reboot)\n");
  sleep(1);
  sysmgr_reboot();
  say("CarPlayMenu: sysmgr_reboot returned: not rebooted\n");
  _exit(1);
}

#!/bin/ksh
# CarPlay Advanced Settings (plugin CarPlayMenu): "Restart to apply". Started by the plugin as its own process, so it outlives the HMI.
# The switches are read when the HMI loads the plugin (knob, touchpad, HUD) and when the HMI, Connectivity or mm-ipod start (fullscreen
# layout and carplay.ini, pinch, route guidance), so those processes have to start again with the links in place:
#   * car: the head unit is rebooted (libcpreboot.so: sync + sysmgr_reboot), and boot.sh makes the links from /bin/start_screen.sh before
#     the HMI and Connectivity start. Measured on HMI ID5_1703-490 (NBTevo_M17345I):
#       - starterPackageCtrl -d0 -s/-e HMI only flips the package state: the HmiMain3D process keeps running, nothing is reread;
#       - starterPackageCtrl -d1 -s CONN_MOCCA does stop Connectivity, but its watchdog registration stays and the system resets the
#         head unit a minute later anyway ("WD ERROR ... Connectivity ... CODE 1" in /var/dump/wd_err_omap_<pid>.log);
#       - holding the volume knob does nothing on the MINI, OnOffDSICommander does not exist, /opt/sys/bin/sysshutdown only unmounts.
#   * without the system launcher (/dev/starter): the HMI is stopped, with the flag /dev/shmem/carplaymenu-restart for whatever
#     starts it again.
P=/var/opt/hmi/data/rw/Plugins/CarPlayMenu
LOG=/dev/shmem/carplaymenu.log
log() { echo "CarPlayMenu: restart.sh: $*" >> $LOG; }

if [ -e /dev/starter ]; then
  sleep 1                                          # let the plugin's handler return first
  if [ -f $P/libcpreboot.so ]; then
    log "rebooting the head unit (the links are made again by boot.sh at boot)"
    LD_PRELOAD=$P/libcpreboot.so /bin/cat < /dev/null
    log "reboot failed (libcpreboot.so returned)"
  else
    log "libcpreboot.so missing: not rebooted; restart the head unit to apply"
  fi
else
  log "no system launcher: stopping the HMI"
  echo 1 > /dev/shmem/carplaymenu-restart
  slay -s SIGTERM HmiMain3D > /dev/null 2>&1
fi
exit 0

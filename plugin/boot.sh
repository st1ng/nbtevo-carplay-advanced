#!/bin/ksh
# CarPlay Advanced Settings (plugin CarPlayMenu): the links its switches need, made before the HMI starts.
#
# The switches are files in $D (the plugin writes them; this script only reads them). Two of them work through path-manager links
# (ln -sP), which the system forgets at every reboot, so this runs at every boot, before the HMI and before the CarPlay drivers:
#   one line at the top of /bin/start_screen.sh (carplay-advanced.sh install); the HMI process needs /dev/screen, which that script
#   creates, so the line always runs first
# It is also run by restart.sh ("Restart to apply"). Running it twice is harmless: a link is replaced, never a real file.
# In the car /var (-> /fs/sda1), /opt/hmi/data and the HMI's rw (-> ID5) are path-manager links the launcher makes AFTER
# /bin/start_screen.sh runs (seen on HMI ID5_1703-490), so at boot the switches are read from the disk itself; the links made here
# point there too. The disk is mounted long before (devb-sdmmc, fs-qnx6 /fs/sda1).
D=${CPM_DIR:-/var/opt/hmi/data/rw/carplaymenu}          # CPM_DIR, CPM_PLUGIN_DIR, CPM_LOG: tests only
[ -d $D ] || D=/fs/sda1/opt/hmi/data/ID5/carplaymenu
PL=${CPM_PLUGIN_DIR:-${D%/carplaymenu}/Plugins/CarPlayMenu}
LOG=${CPM_LOG:-/dev/shmem/carplaymenu.log}
log() { echo "CarPlayMenu: boot.sh: $*" >> $LOG; }
on=; for f in fullscreen remap_knob remap_touchpad two_finger media_gestures hud_guidance; do [ -f $D/$f ] && on="$on $f"; done
log "start, switches in $D:${on:- none}"

# Bootloop guard. This script writes $D/boot_pending at every boot; the plugin deletes it once the HMI has run with it for a minute
# (kStableS in cpm.cpp), and "Restart to apply" deletes it before its reboot. Still there at the next boot = that boot failed (an HMI
# crash and watchdog reset, or a power cut within the first minute). $D/boot_strikes counts failed boots in a row:
#   * 3 in a row: SAFE MODE ($D/safe_mode): no links from here, and the plugin applies none of its switches (no code patches, no
#     hooks); the menu still works. It stays until "Restart to apply" (the plugin deletes safe_mode) or `rm $D/safe_mode`.
#   * 3 more in a row while in safe mode (the plugin itself is the problem): the plugin is disabled, its manifest renamed to
#     CarPlayMenu.manifest.disabled. Rename it back to enable it again.
# One failed boot alone changes nothing (before, it disabled the plugin at once).
# Only at boot: run while HmiMain3D is up (the installer's relink) it leaves the count and boot_pending alone.
guard() {
  n=0; [ -f $D/boot_strikes ] && read n < $D/boot_strikes
  case "$n" in [0-9]|[0-9][0-9]) ;; *) n=0 ;; esac
  if [ -f $D/boot_pending ]; then n=$((n + 1)); log "the last boot did not run a minute with the plugin (failed boots in a row: $n)"; else n=0; fi
  if [ $n -ge 3 ]; then
    if [ ! -f $D/safe_mode ]; then
      echo 1 > $D/safe_mode && log "SAFE MODE: $n failed boots in a row; the switches are not applied until Restart to apply"
    elif [ -f $PL/CarPlayMenu.manifest ]; then
      mv $PL/CarPlayMenu.manifest $PL/CarPlayMenu.manifest.disabled && log "PLUGIN DISABLED: $n failed boots in a row in safe mode"
    fi
    n=0
  fi
  echo $n > $D/boot_strikes
  rm -f $D/boot_pending
  [ -f $PL/CarPlayMenu.manifest ] && echo 1 > $D/boot_pending
}
if [ -f $PL/hud_only ]; then
  # the HUD-only menu (turn-by-turn-only install) patches nothing and may never be started (a car not coded for apps): no guard
  rm -f $D/boot_pending $D/boot_strikes
elif [ "$(pidin -p HmiMain3D 2>/dev/null | grep -c HmiMain3D)" != 0 ]; then
  log "HMI running: not a boot, bootloop guard left as it is"
else
  guard
fi
if [ -f $D/safe_mode ]; then log "safe mode: no links"; exit 0; fi

# relink <file in $D> <system path> <switch>: the system path resolves to our file while the switch is on, to the original otherwise
relink() {
  [ -L "$2" ] && rm -f "$2"          # only an earlier path-manager link, never the real file
  [ -f "$D/$3" ] && [ -f "$D/$1" ] && ln -sP "$D/$1" "$2" && log "linked $2 -> $D/$1"
}

# Enable fullscreen: the MINI iDrive 5 layout with the wide CarPlay slot (written by the plugin, src/pbwpatch.h)
# The HMI reads it as /opt/hmi/data/ro/... (-ro=, starter.cfg); /opt/hmi/data may not exist yet at boot, so both spellings are linked
# (ln -sP does not need the path to exist; the longer registered path wins)
relink Spec_State_Merged.pbw /opt/hmi/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw fullscreen
relink Spec_State_Merged.pbw /opt/hmi/ID5/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw fullscreen
# ... on a BMW the HMI's common layout instead (the plugin writes one of the two files, by the car's brand)
relink Spec_State_Merged-common.pbw /opt/hmi/data/ro/common/widget/generated/Spec_State_Merged.pbw fullscreen
relink Spec_State_Merged-common.pbw /opt/hmi/ID5/data/ro/common/widget/generated/Spec_State_Merged.pbw fullscreen
# carplay.ini as the plugin wrote it for what is on (1180 wide for fullscreen, NudgeSupport for the knob directions; Connectivity reads
# it when it starts). The plugin deletes its copy when nothing needs it, so the file itself is the switch.
relink carplay.ini /opt/conn/data/carplay/carplay.ini carplay.ini
# ... and Connectivity's working copy, which is what CarPlay reads (once, when Connectivity starts): NudgeSupport, the touchpad. Connectivity
# writes it from the template only when coded values change, so the template above alone never reached CarPlay in the car (2026-11-12).
# /var is not there yet at boot: the link is registered anyway, and the longer path wins once the launcher links /var.
relink carplay-conn.ini /var/opt/conn/carplay/carplay.ini carplay-conn.ini

# Add route guidance to HUD: mm-ipod with librghook.so, so the iPhone sends route guidance (script and binary copy by the plugin)
[ -f $D/bin/mm-ipod ] && relink mm-ipod /opt/conn/bin/carplay/mm-ipod hud_guidance

# Knob directions and two-finger touchpad: Connectivity with libcppinch.so (the plugin writes the wrapper $D/Connectivity while a
# controller or touchpad option is on, and deletes it otherwise). Whether Connectivity was already running is logged: then it only gets
# the library from the next boot.
if [ -f $D/Connectivity ] && [ -f $D/bin/Connectivity ]; then
  [ "$(pidin -p Connectivity 2>/dev/null | grep -c Connectivity)" != 0 ] && log "Connectivity was already running"
  relink Connectivity /opt/conn/bin/Connectivity Connectivity
fi
exit 0

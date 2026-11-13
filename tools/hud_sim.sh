#!/bin/ksh
# Runs ON THE HEAD UNIT (hu-omap). A pretend CarPlay route for the HUD: writes /var/iap/pps/routeguidance (the file librghook.so writes
# while the iPhone guides, read by the plugin's HUD, src/hud.h readGuide) with one maneuver whose distance counts down, so
# the HUD effects (hud_fill / hud_blink) can be watched without driving. No CarPlay route may be running (librghook would overwrite
# the file); the file is removed at the end (the HUD clears). The plugin logs each step ("HUD: fill level ...") only with the switch
# debug_log in carplaymenu/.
#
#   ksh hud_sim.sh [from=300] [step=20] [ms=1000] [type=2] [angle=90]
#     from   metres at the start (also the maneuver's step length)      step  metres per tick
#     ms     tick length (sleep has whole seconds only: 1000 or more)   type, angle  maneuver type (iAP2) and exit angle
# Copy:  ssh root@<hu> "on -f hu-omap /bin/ksh -c 'cat > /var/hud_sim.sh'" < tools/hud_sim.sh
F=/var/iap/pps/routeguidance
typeset -i from=${1:-300} step=${2:-20} ms=${3:-1000} type=${4:-2} angle=${5:-90} d secs
secs=ms/1000; [ $secs -lt 1 ] && secs=1
mkdir -p /var/iap/pps
d=from
while [ $d -ge 0 ]; do
  {
    echo "route_state:n:1"
    echo "dist_to_maneuver:n:$d"
    echo "dist_to_maneuver_units:n:2"
    echo "maneuver_step_length:n:$from"
    echo "maneuver_index:n:1"
    echo "maneuver_type:n:$type"
    echo "maneuver_exit_angle:n:$angle"
    echo "maneuver_driving_side:n:0"
    echo "maneuver_road::Test Road"
  } > $F.tmp && mv $F.tmp $F
  echo "$d m"
  [ $d -eq 0 ] && break
  sleep $secs
  d=d-step; [ $d -lt 0 ] && d=0
done
sleep 3
rm -f $F
echo "route ended (file removed)"

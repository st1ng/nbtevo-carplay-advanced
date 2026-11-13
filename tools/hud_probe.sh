#!/bin/ksh
# Runs ON THE HEAD UNIT (NBT Evo, over SSH). Probes the cluster's HUDTerminal (MOST FBlock 0x82/00) through the CarPlay menu
# plugin's test channel (/dev/shmem/carplaymenu-hudcmd, hud.h runCmds): finds which drawing surfaces the cluster has, their sizes,
# and which one is the distance bar, so CarPlay guidance can use the factory bar (PotentiometerSetValue 0x227).
# Formats from the old NBT + 6WB cluster (github.com/JayJay3071/nbt-android-auto, docs/CLUSTER-HUDTERMINAL.md):
#   C04 [inst u16][01]                   ImageGetSize -> answer [inst][01][w u16][h u16]  (6WB: bar 0x35 = 28 x 113)
#       F54 (car, 2026-11-13): C04 [inst u16] only -> op d [inst][w u16][h u16]; with the 01 byte (or longer) op 9 [inst] 05 =
#       ErrorAck "invalid length". sizes sends the F54 form; "sizes6wb" the 6WB one.
#   227 [inst u16][value i32][02]        PotentiometerSetValue, value 0..255 (6WB: full = far, empty = at the maneuver)
#   204 [inst u16][00|01]                SetVisibility
# Needs: the plugin installed with "CarPlay directions in HUD" on (its MOST client runs the test channel), no CarPlay route active
# (the plugin would draw over the test). BMW navigation must NOT be guiding either, except for "seen".
# Answers and errors go to the plugin log /dev/shmem/carplaymenu.log ("HUD in ... fkt c04 op d"; op f = ErrorAck): for 10 s after
# each batch even without the switch debug_log; "seen" needs it (touch /var/opt/hmi/data/rw/carplaymenu/debug_log, restart the HMI).
#
#   ksh hud_probe.sh sizes [first=1] [last=64]   ImageGetSize for every handle in the range, then the sizes found
#   ksh hud_probe.sh bar <inst>                  show <inst>, sweep 255 -> 0 in 9 steps (1 s each), hide it again
#   ksh hud_probe.sh bar <inst> keep             the same, but leave it visible at 0
#   ksh hud_probe.sh concave [mini|6wa] [buf=6] [secs=6]   can the terminal fill NON-convex polygons? Three stages on the picture
#                                                buffer (panel 1 on): a notched 7-point arrow, a U-turn (22-point half ring), a
#                                                swallow-tail flag (NBT-AAD's 6WA shapes; mini = the F54 box (35,15)-(85,65)). Clean
#                                                shapes = concave works (pictures could drop from ~64 calls to ~4); a filled hull,
#                                                spikes or nothing = convex only, as hudshapes.h assumes
#   ksh hud_probe.sh colours [first=0] [last=31] [secs=2] [buf=6]   a filled square in the F54 picture box for each fill colour
#                                                index (SetFillColor 21b) in turn, the index printed as it shows: which indexes are
#                                                colours on this HUD (white 4; 10 drew nothing on the F54, car 2026-11-13)
#   ksh hud_probe.sh vis <inst> 0|1              SetVisibility
#   ksh hud_probe.sh raw "<fkt> <hex>" ...       any calls, one argument each, e.g. raw "227 0035 000000ff 02"
#   ksh hud_probe.sh seen                        what the BMW navigation itself sent to 82/00 (bar 226/227/229, steps c07/c08),
#                                                as logged by the plugin while the BMW navigation guided (needs debug_log)
#   ksh hud_probe.sh log [lines=40]              the last HUD lines of the plugin log
# Handles are decimal or 0x-hex. Copy:  ssh root@<hu> 'cat > /var/hud_probe.sh' < tools/hud_probe.sh
CMD=/dev/shmem/carplaymenu-hudcmd
LOG=/dev/shmem/carplaymenu.log

# plain ksh only (the head unit's ksh is pdksh-like: no printf builtin, no 0x numbers, no grep -E / tail +N assumed)
set -A HX 0 1 2 3 4 5 6 7 8 9 a b c d e f
num() { case "$1" in 0x*|0X*) echo "16#${1#0?}" ;; *) echo "$1" ;; esac; }       # "0x35" -> 16#35 for typeset -i
hexn() { typeset -i v=$(num $1) n=$2; typeset s=""; while [ $n -gt 0 ]; do s="${HX[v % 16]}$s"; v=v/16; n=n-1; done; echo $s; }
hex4() { hexn $1 4; }
hex8() { hexn $1 8; }
lines() { if [ -f $LOG ]; then typeset -i n=$(wc -l < $LOG); echo $n; else echo 0; fi; }

# write a batch to the test channel and wait until the plugin has taken it (it polls every 0.5..1 s)
run() {
  if [ -f $CMD ]; then echo "test channel busy ($CMD still there)"; exit 1; fi
  print -r -- "$1" > $CMD.tmp && mv $CMD.tmp $CMD
  typeset -i t=0
  while [ -f $CMD ]; do
    sleep 1; t=t+1
    if [ $t -ge 8 ]; then
      rm -f $CMD
      echo "the plugin did not take the calls: is \"CarPlay directions in HUD\" on, and the HMI running? ($LOG)"; exit 1
    fi
  done
}

# HUD lines of the log after line $1
since() { typeset -i from=$1; [ -f $LOG ] && sed -n "$((from + 1)),\$p" $LOG | grep 'CarPlayMenu: HUD'; }

case "$1" in
sizes|sizes6wb)
  typeset -i i=${2:-1} last=${3:-64}
  x=""; [ "$1" = sizes6wb ] && x=01
  b=""; while [ $i -le $last ]; do b="${b}c04 $(hex4 $i)$x
"; i=i+1; done
  start=$(lines)
  run "$b"
  sleep 3
  echo "--- answers (inst: w x h) ---"
  since $start | grep 'fkt c04' | grep -v 'HUD out' | grep -v ' op 6 ' | while read -r l; do
    case "$l" in *" op d "*) ;; *" op 9 "*) continue ;; *) echo "error/other: ${l#*HUD }"; continue ;; esac
    set -- ${l##*: }                                    # data bytes: inst(2) [01] w(2) h(2)
    if [ $# -ge 7 ]; then
      typeset -i10 inst w h; inst=16#$1$2; w=16#$4$5; h=16#$6$7       # -i10: print in decimal (QNX ksh keeps the base)
      echo "  0x$(hexn $inst 2): $w x $h"
    elif [ $# -ge 6 ]; then
      typeset -i10 inst w h; inst=16#$1$2; w=16#$3$4; h=16#$5$6
      [ $w -eq 0 ] && [ $h -eq 0 ] && continue                              # 0 x 0: no such surface
      echo "  0x$(hexn $inst 2): $w x $h"
    else echo "error/other: ${l#*HUD }"; fi
  done
  echo "--- errors (op f) for c04 ---"
  since $start | grep 'fkt c04 op f' | sed -n 1,5p
  echo "(full lines: ksh $0 log 200)"
  ;;
colours)
  typeset -i c=${2:-0} last=${3:-31} secs=${4:-2} buf=$(num ${5:-6})
  B=$(hex4 $buf)
  SQ="21d ${B}0000030400280014005000140050003f0028003f"
  echo "buffer 0x$B, ${secs} s per colour: watch the HUD"
  run "204 000101
204 ${B}01
204 000301"
  while [ $c -le $last ]; do
    echo "colour $c ($(date +%T))"
    typeset -i t=c/10 o=c%10
    T="00$(hexn $((48 + o)) 4)"; [ $t -gt 0 ] && T="00$(hexn $((48 + t)) 4)$(hexn $((48 + o)) 4)"   # SetText: [h][00] UTF-16BE [0000]
    run "220 ${B}
21b ${B}$(hexn $c 2)
219 ${B}00
$SQ
210 0003${T}0000"
    sleep $secs; c=c+1
  done
  run "220 ${B}
210 0003000000
204 000300
204 000100"
  echo "done"
  ;;
bar)
  [ -z "$2" ] && { echo "usage: $0 bar <inst> [keep]"; exit 1; }
  typeset -i inst=$(num $2)
  h=$(hex4 $inst)
  start=$(lines)
  echo "bar on 0x$h: visible, 255 -> 0"
  run "204 ${h}01
227 ${h}$(hex8 255)02"
  for v in 224 192 160 128 96 64 32 0; do
    sleep 1; echo "  value $v"; run "227 ${h}$(hex8 $v)02"
  done
  sleep 1
  [ "$3" = keep ] || { echo "  hidden again"; run "204 ${h}00"; }
  sleep 2
  echo "--- answers ---"; since $start | grep -e 'fkt 204' -e 'fkt 227' | grep -v 'HUD out' | sed -n 1,30p
  ;;
concave)
  box=${2:-mini}; typeset -i buf=$(num ${3:-6}) secs=${4:-6}
  B=$(hex4 $buf)
  if [ "$box" = 6wa ]; then
    ARROW="21d ${B}00000307006f0074006f0036006400360078001e008c00360081003600810074"
    UTURN="21d ${B}00000304008300740083003e0095003e00950074
21d ${B}00010316005b003e005c00350061002d00670027006f00220078002100810022008900270090002d009400350095003e0083003e0083003b00810038007f0035007b0034007800330075003400720035006f0038006e003b006d003e
21d ${B}00020307006d003e006d0050007800500064006800500050005b0050005b003e"
    FLAG="21d ${B}00000304006b006e006b0024007100240071006e
21d ${B}000103050071002500960025008c00330096004000710040"
  else
    ARROW="21d ${B}000003070037003f0037002000320020003c001400460020004000200040003f"
    UTURN="21d ${B}000003040041003f00410024004a0024004a003f
21d ${B}00010316002d0024002e001f0030001b0033001800370016003c001500400016004400180048001b004a001f004a0024004100240041002200400021003f001f003d001f003c001e003a001f0039001f003700210037002200360024
21d ${B}00020307003600240036002d003c002d003200390028002d002d002d002d0024"
    FLAG="21d ${B}000003040035003c00350017003800170038003c
21d ${B}0001030500380017004b00170046001e004b002500380025"
  fi
  start=$(lines)
  prep="220 ${B}
21b ${B}04
219 ${B}00"
  echo "box $box, buffer 0x$B, ${secs} s per stage: watch the HUD"
  echo "1/3 one notched 7-point arrow (straight up). Clean arrow = concave fill works"
  run "204 000101
204 ${B}01
$prep
$ARROW"; sleep $secs
  echo "2/3 U-turn left: stem, 22-point half ring, notched arrow head pointing down"
  run "$prep
$UTURN"; sleep $secs
  echo "3/3 destination flag: pole and a swallow-tailed flag (notch on the right)"
  run "$prep
$FLAG"; sleep $secs
  run "220 ${B}
204 000100"
  sleep 2
  echo "--- cluster answers to the polygons (op d = taken, op f = refused: [code][param]) ---"
  since $start | grep 'fkt 21d' | grep -v 'HUD out' | grep -v ' op 6 ' | sed -n 1,12p
  echo "Note what the HUD showed for each stage (clean / hull / spikes / nothing); a photo settles it."
  ;;
vis)
  [ -z "$3" ] && { echo "usage: $0 vis <inst> 0|1"; exit 1; }
  start=$(lines); run "204 $(hex4 $2)0$3"; sleep 2; since $start | grep 'fkt 204' | grep -v 'HUD out'
  ;;
raw)
  shift; [ $# -eq 0 ] && { echo "usage: $0 raw \"<fkt> <hex>\" ..."; exit 1; }
  b=""; for c in "$@"; do b="${b}${c}
"; done
  start=$(lines); run "$b"; sleep 2; since $start
  ;;
seen)
  [ -f $LOG ] || { echo "no $LOG"; exit 1; }
  grep 'HUD seen' $LOG | grep -e 'fkt 226 ' -e 'fkt 227 ' -e 'fkt 229 ' -e 'fkt c07 ' -e 'fkt c08 ' -e 'fkt c04 ' -e 'fkt 200 ' -e 'fkt 22b ' | sed -n 1,60p
  echo "(with debug_log the plugin logs 600 HUD lines per boot / per test batch: guide with the BMW navigation soon after the HMI starts)"
  ;;
log)
  [ -f $LOG ] && grep 'CarPlayMenu: HUD' $LOG | tail -${2:-40}
  ;;
*)
  sed -n '2,32p' $0
  ;;
esac

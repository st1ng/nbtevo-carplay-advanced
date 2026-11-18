#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Installer for the "CarPlay Advanced Settings" HMI plugin (plugin/) on the NBT Evo, iDrive 5/6, HMI 006_017_006,
# ID5_1703-490-1650ZD Build 139 or ID5_1607-490-1610T Build 153 (the plugin picks its addresses by build; no Full-screen CarPlay on
# 1607).
#
# Run it from a computer (macOS or Linux, POSIX sh + OpenSSH), in the folder with plugin/:
#     sh carplay-advanced.sh install [--host root@169.254.199.99] [--dry-run] [--restart-hmi]
# It checks plugin/, opens one SSH connection (one password prompt), uploads itself and plugin/ to /fs/sda1/opt/carplay-advanced-upload
# on hu-omap (every file read back and compared), runs itself there, removes the upload and copies the run's backup to BACKUP/.
# On the head unit itself (QNX /bin/sh, root): sh carplay-advanced.sh install --payload <folder with the plugin files>;
# from hu-jacinto it forwards itself to hu-omap.
# The pre-installation checks follow NBT-AAD USB Tools (github.com/Yalozo/nbt-aad-usb-tools, MIT); the code is ours.
#
# What an installation changes, and where the previous state goes (nothing is changed before it is backed up):
#   * /var/opt/hmi/data/rw/Plugins/CarPlayMenu  the plugin (replaced; the previous folder is moved to the backup)
#   * /bin/start_screen.sh                      one boot-hook line after the shebang (the same line older versions installed)
#   * the system files the plugin shadows with path-manager links when a switch is on (never written, but backed up once, first):
#       the iDrive 5 layout Spec_State_Merged.pbw, MINI's or (BMW) the HMI's common one (Enable fullscreen),
#       /opt/conn/bin/carplay/mm-ipod (HUD route guidance),
#       /opt/conn/bin/Connectivity (Remap iDrive touch panel)
#   * /var/opt/hmi/data/rw/carplaymenu          the switches: left in place on install (a copy goes to the backup); install writes
#                                                its file "cluster" (most / kombi / both): where CarPlay directions go (see "the cluster")
set -eu
LC_ALL=C
export LC_ALL
umask 022

VERSION=1.3.0
REMOTE_NODE=/net/hu-omap
STAGE=/fs/sda1/opt/carplay-advanced-upload            # upload folder on hu-omap (runs from a computer), removed afterwards
SSH_HOST=root@169.254.199.99                          # the head unit over ENET (sshd on hu-jacinto)
SSH_PASSWORD='ts&SK412'                               # root login of the head unit; empty = ssh asks for it
FS_ROOT=/fs/sda0
HMI_VERSION_FILE=/opt/hmi/ID5/data/ro/version.txt
HMI_BIN=/opt/hmi/bin/zipped/HmiMain3D                 # what the HMI runs (LZO-packed; read through ziploader it is the plain ELF)
HMI_BIN_PLAIN=/opt/hmi/bin/HmiMain3D                  # used when there is no zipped one
CONN_LIB=/opt/conn/lib/carplay/devctrl/libconn_carplay.so
ISSUES_URL=https://github.com/st1ng/nbtevo-carplay-advanced/issues/new
PLUGIN=/var/opt/hmi/data/rw/Plugins/CarPlayMenu
PLUGIN_DISK=/fs/sda1/opt/hmi/data/ID5/Plugins/CarPlayMenu   # the same folder on the disk: /var is a link the launcher makes later
SETTINGS=/var/opt/hmi/data/rw/carplaymenu
START_SCREEN=/bin/start_screen.sh
LAYOUT=/opt/hmi/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw
LAYOUT_ID5=/opt/hmi/ID5/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw
LAYOUT_COMMON=/opt/hmi/data/ro/common/widget/generated/Spec_State_Merged.pbw       # BMW (BMW i, Rolls-Royce): the HMI's common layout
LAYOUT_COMMON_ID5=/opt/hmi/ID5/data/ro/common/widget/generated/Spec_State_Merged.pbw
MMIPOD=/opt/conn/bin/carplay/mm-ipod
CONNECTIVITY=/opt/conn/bin/Connectivity
CARPLAY_INI=/opt/conn/data/carplay/carplay.ini
NBT_AAD_PLUGIN=/opt/hmi/ID5/data/ro/Plugins/NbtAad
BACKUPS=/var/opt/carplay-advanced-backups
LOCK=/var/opt/carplay-advanced.lock
# OnOffDSICommander (the head unit's own lifecycle tool; "appreset" = an application reset, the normal restart). It is not on the
# PATH of an SSH shell: the 1803 image has it in /opt/core/bin and /opt/car/bin; on HMI 1703 (MINI F54) it is on hu-jacinto only
# (/opt/car/bin), run from hu-omap with on -f. Its other commands (boloreset, transport, flash, garage, ...) switch the unit into
# bootloader or persistent modes: only appreset is ever used.
ONOFF_BINS='/opt/core/bin/OnOffDSICommander /opt/car/bin/OnOffDSICommander'
ONOFF_JACINTO=/net/hu-jacinto/opt/car/bin/OnOffDSICommander     # checked from hu-omap; run as: on -f <node> <program>
ONOFF_JACINTO_NODE=hu-jacinto
REBOOT_CONFIRM_S=75                                   # the computer waits this long for "restarting" (appreset + RESTART_WAIT + libcpreboot)
HUD_PROBE_WAIT=8                                      # hud-probe: seconds for the plugin's HUD to take a batch and answer
RESTART_WAIT=20                                       # seconds an appreset gets before libcpreboot.so restarts the unit instead
ONOFF_JACINTO_BIN=/opt/car/bin/OnOffDSICommander
PROBE_HOST=/bin/cat                                   # runs libcpprobe.so as LD_PRELOAD: its constructor prints one line and exits
# The file set depends on the HMI generation (KIND, set by hash/signature of HmiMain3D; see detect_kind / supported software):
#   id5  the full plugin: menu, HMI patches, fullscreen, knob/touchpad, HUD route guidance.
#   id6  turn-by-turn HUD only. On iDrive 6 the HMI does fullscreen, knob tilts and the touchpad itself (variant 0x9c71,
#        coded on the car), so the route-guidance hook (librghook in mm-ipod) and the HUD-only menu are added, no HMI patch. The
#        directions need no reboot (mm-ipod is re-spawned per CarPlay session: the next phone connection); the menu shows after a
#        restart, on cars coded for apps.
# KIND is the HMI generation (id5 / id6), found by the computer from HmiMain3D. MODE is what to install and is independent of it:
#   full  the whole plugin (menu, HMI patches, fullscreen, knob/touchpad, HUD). On id6 the menu needs the car coded for apps and the
#         HMI patches apply only to builds the plugin knows; the rest comes from the HMI itself (variant 0x9c71).
#   hud   turn-by-turn route guidance (librghook in mm-ipod; from the next CarPlay connection, no reboot) and the HUD-only menu
#         (after a restart, on cars coded for apps) -- no HMI patch.
# The default follows the generation (id5 -> full, id6 -> hud); --full / --hud choose the other. Both work on both generations.
KIND=id5
MODE=
MODE_REQ=
RGHOOKS='librghook.so librghook-kombi.so librghook-most.so librghook-both.so'   # one goes in as librghook.so (see "the cluster")
FILES_FULL="CarPlayMenu.manifest CarPlayMenu.xml CarPlayMenu_ALL_Texts.zip libCarPlayMenu.so $RGHOOKS libcppinch.so libcpreboot.so libcpprobe.so boot.sh restart.sh"
LIBS_FULL="libCarPlayMenu.so $RGHOOKS libcppinch.so libcpreboot.so libcpprobe.so"
# HUD-only: the route guidance, and the HUD-only menu (libCarPlayMenu-hud.so / CarPlayMenu-hud.xml, installed as libCarPlayMenu.so /
# CarPlayMenu.xml; the HMI shows it where the car is coded for apps). The mm-ipod wrapper is built on the unit.
FILES_HUD="$RGHOOKS libcpprobe.so libcpreboot.so boot.sh CarPlayMenu.manifest CarPlayMenu-hud.xml CarPlayMenu_ALL_Texts.zip libCarPlayMenu-hud.so"
LIBS_HUD="$RGHOOKS libcpprobe.so libcpreboot.so libCarPlayMenu-hud.so"     # libcpreboot.so: "restart" where there is no OnOffDSICommander
FILES=$FILES_FULL
LIBS=$LIBS_FULL
apply_mode() {    # set FILES/LIBS from $MODE (default full)
    if [ "$MODE" = hud ]; then FILES=$FILES_HUD; LIBS=$LIBS_HUD; else FILES=$FILES_FULL; LIBS=$LIBS_FULL; fi
}
default_mode() {  # the recommended mode for the detected generation
    if [ "$KIND" = id6 ]; then echo hud; else echo full; fi
}
MARK='CarPlayMenu boot hook'
SPACE_MARGIN_KB=2048
SHELL_TMP=/tmp
SHMEM=/dev/shmem

# QNX ksh writes every here-document to a temporary file in $TMPDIR (default /tmp), and not every image has /tmp (the emulator's
# minimal boot does not): then the here-documents go to /dev/shmem, which the head unit always has.
if [ -z "${TMPDIR:-}" ] && [ ! -d "$SHELL_TMP" ] && [ -d "$SHMEM" ]; then
    TMPDIR=$SHMEM
    export TMPDIR
fi

# ---------------------------------------------------------------------------------------------------------------- supported software
# The plugin patches HmiMain3D's code at fixed addresses and libcppinch.so hooks functions of libconn_carplay.so, so support is decided
# by the SHA-256 of those two files, not by the build name. A software is supported when every file listed for it matches.
# To add one: hash both files on that head unit (sh carplay-advanced.sh status prints them), add the addresses to src/cpm.cpp
# (kBuilds) and its lines here.
supported_table() {
    cat <<'EOF'
# software                       file                sha256
ID5_1803-510-1749MA_Build_71     HmiMain3D           2c906b5c6799a37a17b0ed51c37f67e0c1a2bc0edfb87596008f075062e9e6bf
ID5_1803-510-1749MA_Build_71     libconn_carplay.so  062a20f49fc4f4bab1a7e3b9a5910d12ba8e6b70895a434291c20d83ee0a5f85
ID5_1803-510-1749MA_Build_71     scan                edb7d483662c456de791a460e18d9a4cd456b77560b8b661f0ef7e66ea782869
ID5_1703-490-1650ZD_Build_139    HmiMain3D           81343a9349384b790f40510bc6ca6b89aa5efb6d3253fad0e590f296b5e348fb
ID5_1703-490-1650ZD_Build_139    libconn_carplay.so  062a20f49fc4f4bab1a7e3b9a5910d12ba8e6b70895a434291c20d83ee0a5f85
ID5_1703-490-1650ZD_Build_139    scan                28f623d87c3c934d058fb49b1d2b5f04cbfa76f574f2dbcf78814b5276acde6c
ID5_1811-490-1827D_Build_34      HmiMain3D           31026b3164be213d574bc41b1b69b3e7e1dfe1aef5ae00d9fbcc6790e82b6768
ID5_1811-490-1827D_Build_34      libconn_carplay.so  062a20f49fc4f4bab1a7e3b9a5910d12ba8e6b70895a434291c20d83ee0a5f85
ID5_1607-490-1610T_Build_153     HmiMain3D           0fa0e6e0de49235c02a8bcebca588dad18bd5eab512066ab44ed295a09ca3fbd
ID5_1607-490-1610T_Build_153     libconn_carplay.so  bbdbae7df387c448a3eec9dd607f9e7b196b899039a0123e6299d5ad0fa96827
ID5_1607-490-1610T_Build_153     scan                none
ID5_1607-490-1610T_Build_153     fullscreen          none
EOF
}
# supported_as <sha256 of HmiMain3D> <sha256 of libconn_carplay.so>: prints the matching software, nothing if none
supported_as() {
    table=$(supported_table)
    names=
    while read -r name file sum; do
        case "$name" in ''|'#'*) continue ;; esac
        case " $names " in *" $name "*) ;; *) names="$names $name" ;; esac
    done <<EOF
$table
EOF
    for name in $names; do
        match=1
        while read -r row_name file sum; do
            [ "$row_name" = "$name" ] || continue
            case "$file" in
                HmiMain3D) [ "$sum" = "$1" ] || match=0 ;;
                libconn_carplay.so) [ "$sum" = "$2" ] || match=0 ;;
                scan) ;;                                       # the expected HMI scan (checked separately); none = mapped by hand
                fullscreen) ;;                                 # none = no Full-screen CarPlay on this build (the plugin's pbw 0)
                *) match=0 ;;
            esac
        done <<EOF
$table
EOF
        if [ "$match" -eq 1 ]; then echo "$name"; return 0; fi
    done
}
# mm-ipod (the iAP2 driver librghook is preloaded into for the turn-by-turn directions, in every install mode). librghook only
# rewrites iAP2 messages in read()/write() on the transport (no addresses in mm-ipod), so another mm-ipod is no reason to stop; it is
# reported as untested. sha256 and where it was seen:
mmipod_table() {
    cat <<'EOF'
d3f7c8973feff37d512ba92baf42d40ce8a2626fe38fdf17e7da10db320bb36f  ID5_1703-490-1650ZD_Build_139 ID5_1803-510-1749MA_Build_71 ID5_1903-490-1837J_Build_41
5dd2351bb0d8102c0b591fe0b91403e2593a8e9786ab4bfd2b7448b0e1417b25  ID5_1607-490-1610T_Build_153
EOF
}
mmipod_known() {    # mmipod_known <sha256>: the builds that have this mm-ipod, or nothing
    mmipod_table | while read -r m_sum m_where; do
        if [ "$m_sum" = "$1" ]; then echo "$m_where"; fi
    done
}
is_software_name() {
    supported_table | grep "^$1 " >/dev/null 2>&1
}

# ---------------------------------------------------------------------------------------------------------------- HMI scan
# The plugin's addresses for an HMI build it does not know, found in HmiMain3D itself (run on the computer: od + awk only). Each
# "find" is a short window of instruction words around a place the plugin uses, with what moves between builds masked out ('.');
# the values (function addresses, offsets, the tilt and fullscreen patch words) are then read from the words found. The patterns
# hold no firmware and are generated from the two known builds by tools/hmipatterns.py, which also checks that they find exactly
# the known values there. The result (build.tab) goes into the plugin folder, and the plugin uses it when its first line names the
# running HMI build; the plugin still compares every word with its expected original before it writes it.
hmi_patterns() {
    cat <<'EOF'
# find <name> <index of the anchor word> <index of the lookup word> <words> / patch <group> <anchor> <offset> <new> <mode> (generated by tools/hmipatterns.py from 006_017_006 and 1703 Build 139; do not edit)
find touch 0 8 e92d4ff0 e28db020 e24dd074 e1a04000 e24b0028 e50b1064 ........ ........ e51b1064 e5913000 e5933...
find tdfirst 4 8 e6ff2072 e50b606c e50b6070 e50b6078 e5939... e50b2080 e1a08009 ea...... e0895006 e5d53007
find tdend 6 2 e58dc00c ........ e288800a e286600a e51b1064 e5913000 e5933... e1580003 1a...... e51b2070 e3520000 ........ ........
find ctl 14 5 e2800c4e e35c0000 11a02008 03a02000 e2800060 e355003a e3a0c000 01a05008 13a05000 e58dc00c e58d5010 e58dc000 e58dc004 e58dc008 ........
find play 5 2 e5933090 e12fff33 e3a0108d e2800c52 e28000b0 ........ e5943010 e1a00003 e5933000
find tA 4 2 1a...... e355000b 1355003a 0a...... e355000e 1355000d ........ e355000d 1a...... e359007e b1a0a009 a3a0a07e
find tB 0 19 ........ e1a01005 e1a02009 e1a0300a e58d8000 ........ e51b002c e5d03014 e3130004 0a...... e1a03009 ........ e1a02005 e58da000 e58d8004 ........ ........ ........ ........ e59100b0 e355000b 03833001 e3530000 e6ef107a 11a03008 03a03000 e2800c4e e35c0000 11a02008 03a02000 e2800060 e355003a e3a0c000 01a05008 13a05000 e58dc00c e58d5010 e58dc000 e58dc004 e58dc008 ........
find tC 12 12 e24b002c ........ ........ ........ ........ ea...... ........ e24b002c ........ e1a00004 e24bd020 e8bd8ff0 e51b302c e5d33014 e3130040 ........ ........ ........ ........ ........ ........ ........ ........ ........ ........ ........ to:16:tB
find f1 3 3 e8bd8800 e92d4800 e28db004 e5d00108 e8bd8800 e92d4800 e3a00001 e28db004
find f2 3 3 e28db004 e8bd8800 e92d4800 e5d00108 e28db004 e3500000 13a00002 03a00001
find f3 5 1 e58d2000 e2822c11 e593c018 e3e03000 e12fff3c e5d43108 e3530000 0a...... e1a00004 eb...... ea......
find f4 6 6 e1a04000 e24dd02c e24b5018 e1a00005 e59f.... eb...... e5d43108 e3530000 0a...... e1a00004 eb...... ea...... eb...... e2501000
find f5 7 7 ..00.... e50b3018 e3a03001 e54b300d eb...... ea...... ea...... e5d43108 e3530000 0a...... e1a00004 eb...... e1a00004 eb...... ea...... e1a04000
find f6 5 5 e3a0e000 e50bc048 e58de000 e50be044 eb...... e5d43108 e3530000 0a...... e1a00004 eb...... e1a00004
find f7 7 0 e50b0060 eb...... e3500000 ..09.... 15900008 e5903000 e593300c e12fff33 e3500000 1a...... eb...... e3500000 ..05.... 15900008 e5903000
patch tilt tA 0x0 e245c009 =
patch tilt tA 0x4 e35c0005 =
patch tilt tA 0x8 8a000000 imm24
patch tilt tB 0x0 e3a0c000 =
patch tilt tB 0x4 e58dc000 =
patch tilt tB 0x8 e58dc004 =
patch tilt tB 0xc e58dc008 =
patch tilt tB 0x10 e58dc00c =
patch tilt tB 0x14 e245c009 =
patch tilt tB 0x18 e35c0003 =
patch tilt tB 0x1c 978d810c =
patch tilt tB 0x20 e355003a =
patch tilt tB 0x24 01a05008 =
patch tilt tB 0x28 13a05000 =
patch tilt tB 0x2c e58d5010 =
patch tilt tB 0x30 ea00001a =
patch tilt tB 0x54 e320f000 =
patch tilt tB 0x7c eaffffdf =
patch tilt tC 0x0 ea000000 b:tB+0x40
patch fs f1 0 e3a00001 =
patch fs f2 0 e3a00001 =
patch fs f3 0 e3a03001 =
patch fs f4 0 e3a03001 =
patch fs f5 0 e3a03001 =
patch fs f6 0 e3a03001 =
patch fs f7 0 e3a00001 =
EOF
}

# hmi_scan <HmiMain3D (plain ELF)> <HMI version line>: prints build.tab; exit 1 (and the missing names on stderr) when a required
# place is not found exactly once. Tilt and fullscreen words are optional: all of a group or none of it.
hmi_scan() {
    sc_elf=$1
    u4() { od -An -tu4 -j "$1" -N 4 "$sc_elf" | tr -d ' \n'; }
    u2() { od -An -tu2 -j "$1" -N 2 "$sc_elf" | tr -d ' \n'; }
    [ "$(od -An -tx1 -N 6 "$sc_elf" | tr -d ' \n')" = 7f454c460101 ] || { echo 'not a little-endian ELF32 file' >&2; return 1; }
    [ "$(u4 0)" = 1179403647 ] || { echo 'od does not read little-endian words on this computer' >&2; return 1; }
    sc_phoff=$(u4 28); sc_phes=$(u2 42); sc_phn=$(u2 44)
    sc_off=; sc_va=; sc_size=
    sc_i=0
    while [ "$sc_i" -lt "$sc_phn" ]; do
        sc_p=$((sc_phoff + sc_i * sc_phes))
        if [ "$(u4 "$sc_p")" = 1 ] && [ $(($(u4 $((sc_p + 24))) % 2)) = 1 ]; then          # PT_LOAD, executable
            sc_off=$(u4 $((sc_p + 4))); sc_va=$(u4 $((sc_p + 8))); sc_size=$(u4 $((sc_p + 16)))
            break
        fi
        sc_i=$((sc_i + 1))
    done
    [ -n "$sc_off" ] || { echo 'no executable segment' >&2; return 1; }
    CPA_PATTERNS=$(hmi_patterns)                   # through the environment: awk -v takes no newlines
    export CPA_PATTERNS
    od -An -v -tx4 -j "$sc_off" -N "$sc_size" "$sc_elf" | awk -v base="$sc_va" -v hmi="$2" '
    function hex(s,   i, n) { n = 0; for (i = 1; i <= length(s); i++) n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1; return n }
    function imm12(w) { return hex(substr(w, 6, 3)) }
    function rotimm(w,   r, v) { r = 2 * hex(substr(w, 6, 1)); v = hex(substr(w, 7, 2)); return r ? (v % 2 ^ r) * 2 ^ (32 - r) + int(v / 2 ^ r) : v }
    function target(a, w,   v) { v = hex(substr(w, 3, 6)); if (v >= 8388608) v -= 16777216; return a + 8 + 4 * v }
    function b24(from, to,   v) { v = (to - from - 8) / 4; if (v < 0) v += 16777216; return sprintf("%06x", v) }
    BEGIN {
        np = split(ENVIRON["CPA_PATTERNS"], L, "\n")
        for (l = 1; l <= np; l++) {
            n = split(L[l], f, " ")
            if (f[1] == "find") {
                p = f[2]; P[++NP] = p; IDX[p] = f[3]; KEY = f[4]; LEN[p] = 0; TO[p] = ""
                for (k = 5; k <= n; k++) {
                    if (substr(f[k], 1, 3) == "to:") { split(f[k], t, ":"); TO[p] = t[2]; TOA[p] = t[3]; continue }
                    T[p, LEN[p]] = f[k]; LEN[p]++
                }
                K[T[p, KEY]] = K[T[p, KEY]] " " p ":" KEY
                if (LEN[p] > RING) RING = LEN[p]
            } else if (f[1] == "patch") {
                NQ++; QG[NQ] = f[2]; QA[NQ] = f[3]; QO[NQ] = hex(substr(f[4], 3)); QN[NQ] = f[5]; QM[NQ] = f[6]
            }
        }
        RING += 1; i = -1
    }
    {
        for (c = 1; c <= NF; c++) {
            w = $c; i++; R[i % RING] = w
            if (w in K) {
                m = split(substr(K[w], 2), ks, " ")
                for (j = 1; j <= m; j++) { split(ks[j], kk, ":"); s = i - kk[2]; e = s + LEN[kk[1]] - 1; if (s >= 0) PEND[e] = PEND[e] " " kk[1] ":" s }
            }
            if (i in PEND) {
                m = split(substr(PEND[i], 2), ks, " ")
                for (j = 1; j <= m; j++) {
                    split(ks[j], kk, ":"); p = kk[1]; s = kk[2]; ok = 1
                    for (k = 0; k < LEN[p] && ok; k++) if (R[(s + k) % RING] !~ ("^" T[p, k] "$")) ok = 0
                    if (!ok) continue
                    h = ++NH[p]; HS[p, h] = s
                    for (k = 0; k < LEN[p]; k++) HW[p, h, k] = R[(s + k) % RING]
                }
                delete PEND[i]
            }
        }
    }
    function resolve(p,   h, n, keep) {
        n = 0
        for (h = 1; h <= NH[p]; h++) {
            if (TO[p] != "" && target(base + 4 * (HS[p, h] + TO[p]), HW[p, h, TO[p]]) != A[TOA[p]]) continue
            n++; keep = h
        }
        if (n != 1) return 0
        A[p] = base + 4 * (HS[p, keep] + IDX[p]); for (k = 0; k < LEN[p]; k++) W[p, k - IDX[p]] = HW[p, keep, k]
        return 1
    }
    function word(p, off) { return W[p, off / 4] }
    END {
        for (q = 1; q <= NP; q++) if (TO[P[q]] == "") FOUND[P[q]] = resolve(P[q])
        for (q = 1; q <= NP; q++) if (TO[P[q]] != "") FOUND[P[q]] = (TOA[P[q]] in A) ? resolve(P[q]) : 0
        miss = ""
        split("touch tdfirst tdend ctl play", req, " ")
        for (q = 1; q <= 5; q++) if (!FOUND[req[q]]) miss = miss " " req[q]
        if (miss != "") { print "not found:" miss > "/dev/stderr"; exit 1 }
        fs = 1; tilt = 1
        for (q = 1; q <= NQ; q++) if (!FOUND[QA[q]]) { if (QG[q] == "fs") fs = 0; else tilt = 0 }
        to = imm12(W["touch", 10])
        print "hmi " hmi
        printf "touchFn %x\nctlSetFn %x\nctlProxy %x\n", A["touch"], target(A["ctl"], W["ctl", 0]), rotimm(W["ctl", -14]) + rotimm(W["ctl", -10])
        printf "playFn %x\nplayObj %x\n", target(A["play"], W["play", 0]), rotimm(W["play", -2]) + rotimm(W["play", -1])
        printf "tdFirst %x\ntdEnd %x\ntdTime %x\ntdOrigin %x\n", imm12(W["tdfirst", 0]), imm12(W["tdend", 0]), to - 4, to
        printf "pbw %d\npbwBmw %d\nini 1\n", fs ? 1 : 2, fs ? 1 : 2
        for (q = 1; q <= NQ; q++) {
            if ((QG[q] == "fs" && !fs) || (QG[q] == "tilt" && !tilt)) continue
            a = A[QA[q]] + QO[q]; o = word(QA[q], QO[q]); nw = QN[q]
            if (QM[q] == "imm24") nw = substr(nw, 1, 2) substr(o, 3, 6)
            else if (substr(QM[q], 1, 2) == "b:") { split(substr(QM[q], 3), bb, "+"); nw = substr(nw, 1, 2) b24(a, A[bb[1]] + hex(substr(bb[2], 3))) }
            printf "%s %x %s %s\n", QG[q], a, o, nw
        }
        if (!tilt) print "# tilt: not found (knob tilts off)"
        if (!fs) print "# fs: no CARPLAY_FULLSCREEN flag code (full screen by the layout only)"
    }'
}

ACTION=
PROBE_STEP=                                          # hud-probe's visual check, one test picture: <picture>:draw|clear (internal)
PAYLOAD=
HOST_SET=0
SOFTWARE=
DRY_RUN=0
ASSUME_YES=0
RESTART=0
KEEP_CONFIG=0
CLUSTER_FORCE=
LOG=
BACKUP=
LOCKED=0
ACTIVE=0
SS_TEMP=

say() {
    echo "$*"
    if [ -n "$LOG" ]; then echo "$*" >> "$LOG" 2>/dev/null || :; fi
}
die() { say "ERROR: $*" >&2; exit 1; }
exists() { [ -e "$1" ] || [ -L "$1" ]; }
# mkdir -p that follows links. QNX mkdir -p fails with "File exists" when the path it is given is a link to an existing folder, and on
# iDrive 5 /var/opt/hmi/data/rw is such a link (to /fs/sda1/opt/hmi/data/ID5). Each part is made only when [ -d ] (which follows
# links) says it is not there.
make_dirs() {
    md_path=
    md_rest=${1#/}
    while [ -n "$md_rest" ]; do
        case "$md_rest" in
            */*) md_part=${md_rest%%/*}; md_rest=${md_rest#*/} ;;
            *) md_part=$md_rest; md_rest= ;;
        esac
        if [ -z "$md_part" ]; then continue; fi
        md_path=$md_path/$md_part
        if [ ! -d "$md_path" ]; then mkdir "$md_path" || return 1; fi
    done
    return 0
}

# Reboots hu-omap (exit 3 when it cannot). HMI ID5_1703-490 has no OnOffDSICommander (and the MINI ignores holding the volume knob):
# then the plugin's libcpreboot.so (sync + sysmgr_reboot, run as LD_PRELOAD of /bin/cat) does it. Stopping launcher packages is no
# restart (restart.sh).
restart_unit() {
    REBOOT_LIB=
    # the installed plugin's, the last backup's, or the one the computer sent next to this script (a unit without the plugin)
    for f in "$PLUGIN/libcpreboot.so" "$BACKUP/plugin/libcpreboot.so" "${0%/*}/libcpreboot.so"; do
        if [ -f "$f" ]; then REBOOT_LIB=$f; break; fi
    done
    ONOFF=
    if command -v OnOffDSICommander >/dev/null 2>&1; then ONOFF=OnOffDSICommander; fi
    for f in $ONOFF_BINS; do if [ -z "$ONOFF" ] && [ -f "$f" ]; then ONOFF=$f; fi; done
    ONOFF_ON=                                         # "on -f hu-jacinto": the tool runs on the node that has it
    if [ -z "$ONOFF" ] && [ -f "$ONOFF_JACINTO" ] && command -v on >/dev/null 2>&1; then
        ONOFF=$ONOFF_JACINTO_BIN; ONOFF_ON=$ONOFF_JACINTO_NODE
    fi
    if [ -n "$ONOFF" ]; then how="${ONOFF_ON:+on -f $ONOFF_ON }$ONOFF appreset"
    elif [ -n "$REBOOT_LIB" ]; then how="$REBOOT_LIB"
    else say 'Neither OnOffDSICommander nor libcpreboot.so is available. Reboot manually.'; exit 3; fi
    say "Restarting the head unit ($how). The SSH connection may close."
    # A reset may end this shell before its EXIT trap: release the lock and flush first.
    if [ "$LOCKED" -eq 1 ] && ! release_lock; then
        say 'Files changed, but the lock cannot be released. Restart skipped.'
        exit 3
    fi
    if command -v sync >/dev/null 2>&1; then
        sync || { say 'Final sync failed. Restart skipped; reboot manually after inspection.'; exit 3; }
    fi
    if [ -n "$ONOFF" ]; then
        onoff_rc=0
        if [ -n "$ONOFF_ON" ]; then on -f "$ONOFF_ON" "$ONOFF" appreset || onoff_rc=$?
        else "$ONOFF" appreset || onoff_rc=$?; fi
        # appreset can return 0 and still not reset the unit: still running after RESTART_WAIT seconds = it did not happen
        if [ "$onoff_rc" -eq 0 ]; then
            [ "$RESTART_WAIT" -gt 0 ] || return 0              # 0: trust a successful appreset (the tests)
            sleep "$RESTART_WAIT"
        fi
        [ -n "$REBOOT_LIB" ] || { say 'The restart did not happen. Reboot manually.'; exit 3; }
        say "$ONOFF appreset did not restart the head unit; restarting with $REBOOT_LIB instead."
        LD_PRELOAD=$REBOOT_LIB /bin/cat < /dev/null
        say 'The reboot did not happen (libcpreboot.so returned). Reboot manually.'; exit 3
    else
        LD_PRELOAD=$REBOOT_LIB /bin/cat < /dev/null
        say 'The reboot did not happen (libcpreboot.so returned). Reboot manually.'; exit 3
    fi
}

usage() {
    cat <<'EOF'
CarPlay Advanced Settings installer 1.3.0 (NBT Evo iDrive 5 / iDrive 6)
Usage: sh carplay-advanced.sh install|uninstall|status|restart|hud-probe [options]
  hud-probe             Ask the cluster / HUD what it has (about 40 s, nothing installed or changed): the plugin's HUD state,
                        the HUDTerminal instances, the surfaces and their sizes, the navigation's instance names. Then, on a
                        terminal, a visual check: five test pictures (6WA, MINI, 6WB layouts; cluster terminal 82/00 and HUD
                        terminal 82/02), each asked "do you see it on the cluster / on the HUD?". Needs the plugin with
                        "CarPlay directions in HUD" on and no route guiding; from a computer the output and the answers are
                        saved in BACKUP/.
  --host user@address   From a computer: the head unit to connect to (default root@169.254.199.99)
  --full               Install the full plugin (menu, HMI patches, fullscreen, knob/touchpad, HUD). Default on iDrive 5.
  --hud                 Install turn-by-turn directions (from the next CarPlay connection, no reboot) and the HUD-only menu
                        (after a restart, on cars coded for apps); no HMI patch. Default on iDrive 6.
  --payload <folder>    On the head unit: the folder with the plugin files (a computer uploads plugin/ itself)
  --hmi <software>      On the head unit: the supported iDrive 5 software the computer found by hash (full mode only)
  --kind id5|id6        On the head unit: the HMI generation the computer detected
  --mode full|hud       On the head unit: the install variant the computer resolved
  --dry-run             Run every check and show the plan without changing anything
  --yes, -y             Do not ask before installing or restarting (asked only on a terminal)
  --restart-hmi         Reboot the head unit after a successful change (OnOffDSICommander appreset, or the plugin's libcpreboot.so)
  --keep-config         Uninstall, but leave the switches in /var/opt/hmi/data/rw/carplaymenu
  --cluster most|kombi|both
                        Where CarPlay directions go, instead of what the probe finds: most = the MOST HUDTerminal (F-series),
                        kombi = the Ethernet KOMBI (G-series, J29), both = the two (a car with a KOMBI and a MOST HUDTerminal)
  --help                Show this help

The computer detects the generation from HmiMain3D: iDrive 6 has the native CarPlay knob-tilt handlers, iDrive 5 does not.
iDrive 6 does fullscreen, knob tilts and the touchpad itself (HMI variant 0x9c71, coded on the car), so the HUD is all the
plugin adds there; on iDrive 5 the plugin provides everything. The default follows the generation; --full / --hud override it.
install also updates an existing installation and keeps its switches.
Every file that is replaced or changed is backed up first, in /var/opt/carplay-advanced-backups.
Default: no automatic restart; restart the head unit afterwards.
From a computer it connects with ssh (extra ssh options: CPA_SSH_OPTS). On the head unit, execution is forwarded from
hu-jacinto to hu-omap via on -f.
EOF
}

# Parse inside a function so the original argument vector survives for on -f.
parse_args() {
 while [ "$#" -gt 0 ]; do
    case "$1" in
        install|uninstall|status|restart|hud-probe)
            [ -z "$ACTION" ] || die 'Specify exactly one action.'
            ACTION=$1 ;;
        --host)
            [ "$#" -ge 2 ] || die '--host needs user@address.'
            SSH_HOST=$2; HOST_SET=1; shift ;;
        --probe-step)
            [ "$#" -ge 2 ] || die '--probe-step needs <picture>:draw|clear.'
            PROBE_STEP=$2; shift ;;
        --payload)
            [ "$#" -ge 2 ] || die '--payload needs a folder.'
            PAYLOAD=$2; shift ;;
        --hmi)
            [ "$#" -ge 2 ] || die '--hmi needs a software name.'
            SOFTWARE=$2; shift ;;
        --kind)
            [ "$#" -ge 2 ] || die '--kind needs id5 or id6.'
            case "$2" in id5|id6) KIND=$2 ;; *) die "--kind must be id5 or id6, not '$2'." ;; esac
            shift ;;
        --mode)                                 # on the head unit: the install variant the computer resolved
            [ "$#" -ge 2 ] || die '--mode needs full or hud.'
            case "$2" in full|hud) MODE=$2 ;; *) die "--mode must be full or hud, not '$2'." ;; esac
            shift ;;
        --full) MODE_REQ=full ;;                # force the full plugin (menu + everything the build supports)
        --hud)  MODE_REQ=hud ;;                 # force turn-by-turn HUD route guidance only
        --dry-run) DRY_RUN=1 ;;
        --yes|-y) ASSUME_YES=1 ;;
        --restart-hmi) RESTART=1 ;;
        --keep-config) KEEP_CONFIG=1 ;;
        --cluster)
            [ "$#" -ge 2 ] || die '--cluster needs most, kombi or both.'
            case "$2" in most|kombi|both) CLUSTER_FORCE=$2 ;; *) die "--cluster needs most, kombi or both, not '$2'." ;; esac
            shift ;;
        --help|-h) usage; exit 0 ;;
        *) die "Unknown argument: $1" ;;
    esac
    shift
 done
}
# The older QNX shell treats an empty "$@" as unset under set -u.
[ "$#" -gt 0 ] || { usage; exit 1; }
parse_args "$@"
[ -n "$ACTION" ] || { usage; exit 1; }
[ "$KEEP_CONFIG" -eq 0 ] || [ "$ACTION" = uninstall ] || die '--keep-config requires uninstall.'
[ "$RESTART" -eq 0 ] || [ "$ACTION" = install ] || [ "$ACTION" = uninstall ] || die '--restart-hmi goes with install or uninstall.'
# On the head unit the computer passes --mode (and --kind); a manual run falls back to the generation's default. From a computer the
# mode is resolved after the generation is detected, so this initial value is replaced there.
[ -n "$MODE" ] || MODE=$(default_mode)
apply_mode
# iDrive 6, turn-by-turn only: mm-ipod is shadowed by a wrapper that LD_PRELOADs librghook and runs a copy of the original (the plugin
# makes this at runtime on id5; on id6 there is no menu plugin, so the installer builds it). $SETTINGS holds the switch, the wrapper and
# the copy; librghook lives in $PLUGIN.
MMIPOD_BIN=$SETTINGS/bin/mm-ipod                       # a copy of the original mm-ipod (the wrapper execs it; same name for slay/pidin)
MMIPOD_WRAP=$SETTINGS/mm-ipod                          # the wrapper script, linked over $MMIPOD by boot.sh when hud_guidance is set
HUD_SWITCH=$SETTINGS/hud_guidance

# ---------------------------------------------------------------------------------------------------------------- from a computer
# Everything goes through one SSH master connection; the remote commands run on hu-omap (on -f when sshd is on hu-jacinto).
# The head unit has no tar and scp may be missing, so every file is sent and fetched with cat and compared.
# The plugin files get the same checks here as on the head unit, before anything is sent. After a change, the run's backup and the
# kept system originals are copied to BACKUP/ next to this script; --restart-hmi restarts the head unit only after that.
if [ "$(uname -s)" != QNX ]; then
    [ -z "$PAYLOAD" ] || die '--payload is for runs on the head unit; from a computer the plugin files come from plugin/.'
    [ -z "$SOFTWARE" ] || die '--hmi is for runs on the head unit; from a computer the software is found by hash.'
    HERE=$(cd "$(dirname "$0")" && pwd)
    # -------- how the computer prints: a heading per step, a mark per result, the head unit's lines indented; colours only on a
    # terminal (NO_COLOR turns them off). The head unit's own log (operation.log in the backup) keeps every line as it was.
    if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
        C_B=$(printf '\033[1m'); C_G=$(printf '\033[32m'); C_Y=$(printf '\033[33m'); C_R=$(printf '\033[31m'); C_D=$(printf '\033[2m')
        C_N=$(printf '\033[0m')
    else
        C_B=; C_G=; C_Y=; C_R=; C_D=; C_N=
    fi
    step() { printf '\n%s==> %s%s\n' "$C_B" "$*" "$C_N"; }
    ok() { printf '  %s✓%s %s\n' "$C_G" "$C_N" "$*"; }
    note() { printf '  %s!%s %s\n' "$C_Y" "$C_N" "$*"; }
    say() { printf '  %s\n' "$*"; }
    die() { printf '\n  %s✗ ERROR:%s %s\n' "$C_R" "$C_N" "$*" >&2; exit 1; }
    row() { printf '  %-12s %s\n' "$1" "$2"; }
    # the head unit's lines: indented; the closing lines are left out (the summary below says the same, with the local paths)
    show_remote() {
        while IFS= read -r l; do
            case "$l" in
                'SUCCESS: '*|'Running processes keep '*|'The new plugin is loaded when '*|'Restart the head unit to apply'*) ;;
                'CarPlay Advanced Settings installer '*'; backup: '*) ;;
                'ERROR: '*|'ROLLBACK'*) printf '    %s%s%s\n' "$C_R" "$l" "$C_N" ;;
                'Note: '*) printf '    %s%s%s\n' "$C_Y" "$l" "$C_N" ;;
                'DRY RUN'*) printf '    %s%s%s\n' "$C_B" "$l" "$C_N" ;;
                *) printf '    %s\n' "$l" ;;
            esac
        done
    }
    printf '%sCarPlay Advanced Settings installer %s%s  %s(%s, %s)%s\n' "$C_B" "$VERSION" "$C_N" "$C_D" "$ACTION" "$SSH_HOST" "$C_N"
    SRC=$HERE/plugin
    LOCAL_BACKUPS=$HERE/BACKUP
    for tool in ssh cmp mktemp od tr wc tee sed; do
        command -v "$tool" >/dev/null 2>&1 || die "Required command is missing on this computer: $tool"
    done
    if command -v sha256sum >/dev/null 2>&1; then SHA256=sha256sum
    elif command -v shasum >/dev/null 2>&1; then SHA256='shasum -a 256'
    else die 'Required command is missing on this computer: sha256sum or shasum'; fi
    sh -n "$0" || die "$0 does not parse with sh -n."

    # -------- the plugin files, checked here first (the head unit checks them again)
    hex_at() { od -An -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'; }     # hex_at <file> <offset> <count>
    if [ "$ACTION" = install ]; then
        step 'Checking the plugin files'
        for item in $FILES; do
            [ -f "$SRC/$item" ] && [ -s "$SRC/$item" ] && [ ! -L "$SRC/$item" ] || die "Missing, empty or linked $SRC/$item (build it with build.sh)."
        done
        for item in $LIBS; do
            [ "$(wc -c < "$SRC/$item" | tr -d ' ')" -ge 52 ] || die "Truncated ELF header: $SRC/$item"
            [ "$(hex_at "$SRC/$item" 0 7)" = 7f454c46010101 ] || die "Not a little-endian ELF32 file: $SRC/$item"
            [ "$(hex_at "$SRC/$item" 16 4)" = 03002800 ] || die "Not an ARM shared library: $SRC/$item"
            [ "$(hex_at "$SRC/$item" 40 2)" = 3400 ] || die "Invalid ELF header size: $SRC/$item"
        done
        [ "$(hex_at "$SRC/CarPlayMenu_ALL_Texts.zip" 0 4)" = 504b0304 ] || die "Invalid ZIP signature: $SRC/CarPlayMenu_ALL_Texts.zip"
        grep '<name>CarPlayMenu</name>' "$SRC/CarPlayMenu.manifest" >/dev/null 2>&1 || die 'CarPlayMenu.manifest does not name the CarPlayMenu plugin.'
        grep 'pluginApp' "$SRC/CarPlayMenu.xml" >/dev/null 2>&1 || die 'CarPlayMenu.xml is not an RHMI plugin description.'
        if command -v ksh >/dev/null 2>&1; then
            for item in boot.sh restart.sh; do ksh -n "$SRC/$item" >/dev/null 2>&1 || die "$item does not parse with ksh -n."; done
        else
            note 'No ksh on this computer; boot.sh and restart.sh are checked with ksh -n on the head unit only.'
        fi
        ok "Plugin files in plugin/ checked ($(echo $FILES | wc -w | tr -d ' ') files)."
    fi

    # -------- one SSH connection
    WORK=$(mktemp -d /tmp/cpa.XXXXXX) || die 'Cannot create a temporary folder in /tmp.'
    SOCK=$WORK/ssh
    trap 'ssh -o ControlPath="$SOCK" -O exit "$SSH_HOST" >/dev/null 2>&1 || :; rm -rf "$WORK"' 0
    trap 'exit 129' 1
    trap 'exit 130' 2
    trap 'exit 143' 15
    step "Connecting to $SSH_HOST"
    # Reachable at all? A plain TCP connect to its SSH port first, so a missing cable or a sleeping head unit gets a clear answer
    # instead of an SSH timeout. nc: -G is the connect timeout on macOS, -w elsewhere; without nc the SSH login is the probe.
    HU_ADDR=${SSH_HOST#*@}
    if command -v nc >/dev/null 2>&1; then
        if nc -z -G 4 "$HU_ADDR" 22 >/dev/null 2>&1 || nc -z -w 4 "$HU_ADDR" 22 >/dev/null 2>&1; then
            ok "$HU_ADDR answers on port 22."
        else
            case "$HU_ADDR" in
                169.254.*)
                    if command -v ifconfig >/dev/null 2>&1 && ! ifconfig 2>/dev/null | grep 'inet 169\.254\.' >/dev/null; then
                        hint='This computer has no 169.254.x.x (link-local) address: is the ENET cable plugged in, and its adapter up?'
                    else
                        hint='Is the ignition on (the head unit running) and the ENET cable in the OBD port? It takes ~1 min after start.'
                    fi ;;
                *) hint='Check the address (--host) and the network.' ;;
            esac
            die "$HU_ADDR does not answer on port 22 (SSH). $hint"
        fi
    fi
    # The head unit's OpenSSH 5.2 offers only an ssh-rsa host key, which newer clients no longer accept by default.
    # With SSH_PASSWORD set, ssh takes the password from an askpass helper (OpenSSH 8.4+: SSH_ASKPASS_REQUIRE=force) instead of asking;
    # the helper reads it from its environment, so it is not written to a file. A host key seen for the first time is then accepted
    # without a question (StrictHostKeyChecking=accept-new: the helper could not answer it); a changed one still stops the login.
    master_login() {
        if [ -n "$SSH_PASSWORD" ]; then
            cat > "$WORK/askpass" <<'ASKPASS'
#!/bin/sh
printf '%s\n' "$CPA_ASKPASS_PASSWORD"
ASKPASS
            chmod 700 "$WORK/askpass"
            env CPA_ASKPASS_PASSWORD="$SSH_PASSWORD" SSH_ASKPASS="$WORK/askpass" SSH_ASKPASS_REQUIRE=force DISPLAY="${DISPLAY:-none}" \
                ssh -o LogLevel=ERROR -o PreferredAuthentications=keyboard-interactive,password -o NumberOfPasswordPrompts=1 \
                -o StrictHostKeyChecking=accept-new "$@"
        else
            ssh -o LogLevel=ERROR "$@"
        fi
    }
    # shellcheck disable=SC2086
    master_login -o ControlMaster=yes -o ControlPath="$SOCK" -o HostKeyAlgorithms=+ssh-rsa -o ConnectTimeout=15 ${CPA_SSH_OPTS:-} -fN "$SSH_HOST" \
        < /dev/null || die "Cannot connect to $SSH_HOST."
    # LogLevel=ERROR: no OpenSSH notices (the head unit's OpenSSH 5.2 draws the "not using a post-quantum key exchange" warning)
    remote() { ssh -o LogLevel=ERROR -o ControlPath="$SOCK" -o BatchMode=yes "$SSH_HOST" "$1"; }
    node=$(remote 'uname -n' < /dev/null) || die 'The head unit does not answer over the SSH connection.'
    case "$node" in
        hu-omap|hu-omap.*) ON= ;;
        hu-jacinto|hu-jacinto.*) ON="on -f ${REMOTE_NODE##*/} " ;;      # the node name, as in "on -f hu-omap"
        *) die "Unexpected node '$node'; expected an NBT Evo (hu-jacinto or hu-omap)." ;;
    esac
    ok "Connected (one login; every step reuses it)."
    omap() { remote "$ON$1"; }
    send() {    # send <local file> <path on hu-omap>
        omap "/bin/ksh -c 'cat > $2'" < "$1" || die "Upload of ${1##*/} failed."
        omap "/bin/cat $2" < /dev/null | cmp -s - "$1" || die "The uploaded ${1##*/} differs from $1."
    }
    fetch_file() {    # fetch_file <file on hu-omap> <local file>: compared by size with the original
        omap "/bin/cat $1" < /dev/null > "$2" || return 1
        remote_size=$(omap "/bin/ksh -c 'wc -c < $1'" < /dev/null | tr -d ' \r') || return 1
        [ "$(wc -c < "$2" | tr -d ' ')" = "$remote_size" ] || { say "Size of the copy of $1 differs."; return 1; }
    }
    fetch_tree() {    # fetch_tree <folder on hu-omap> <local folder>: every file, compared by size with the original
        walk='walk() { for f in "$1"/*; do if [ -d "$f" ] && [ ! -L "$f" ]; then echo "D $f"; walk "$f"; elif [ -f "$f" ]; then echo "F $f"; fi; done; }; walk '
        listing=$(omap "/bin/ksh -c '$walk$1'" < /dev/null) || return 1
        mkdir -p "$2" || return 1
        while read -r kind path; do
            [ -n "$path" ] || continue
            local_path=$2/${path#"$1"/}
            if [ "$kind" = D ]; then mkdir -p "$local_path" || return 1; continue; fi
            fetch_file "$path" "$local_path" || return 1
        done <<EOF
$listing
EOF
    }
    # Before anything is sent: every head unit file this run may replace or link over, copied here (BACKUP/<time>-<action>-before).
    # A file that is already one of the plugin's links is not copied (cat would read the plugin's copy): its original is in the
    # switches folder (bin/) or the kept originals, which are copied whole. manifest.txt says what was found where.
    pre_backup() {
        PRE=$LOCAL_BACKUPS/$(date +%Y%m%d-%H%M%S)-$ACTION-before
        mkdir -p "$PRE/system" || return 1
        : > "$PRE/manifest.txt" || return 1
        n_pre=0
        for path in $START_SCREEN $MMIPOD $CONNECTIVITY $CARPLAY_INI $LAYOUT $LAYOUT_ID5 $LAYOUT_COMMON $LAYOUT_COMMON_ID5; do
            state=$(omap "/bin/ksh -c 'if [ -L $path ]; then echo link; elif [ -f $path ]; then echo file; else echo absent; fi'" < /dev/null) || return 1
            case "$state" in
                file)
                    mkdir -p "$PRE/system${path%/*}" || return 1
                    fetch_file "$path" "$PRE/system$path" || return 1
                    n_pre=$((n_pre + 1)); echo "file    $path -> system$path" >> "$PRE/manifest.txt" ;;
                link) echo "link    $path (the plugin's link; its original is in the folders below)" >> "$PRE/manifest.txt" ;;
                *) echo "absent  $path" >> "$PRE/manifest.txt" ;;
            esac
        done
        for d in $PLUGIN $SETTINGS $BACKUPS/originals; do
            if omap "/bin/ksh -c '[ -d $d ]'" < /dev/null; then
                fetch_tree "$d" "$PRE/folders$d" || return 1
                n_pre=$((n_pre + $(find "$PRE/folders$d" -type f | wc -l)))
                echo "folder  $d -> folders$d" >> "$PRE/manifest.txt"
            else
                echo "absent  $d" >> "$PRE/manifest.txt"
            fi
        done
    }

    # A reboot never answers: the head unit goes down with the connection open, and ssh would wait for its timeout. So the restart runs
    # in the background, its lines are shown as they come, and once the head unit says it reboots the installer stops waiting.
    reboot_said() { grep 'rebooting the head unit' "$1" >/dev/null 2>&1 || grep 'Restarting the head unit (.*OnOffDSICommander' "$1" >/dev/null 2>&1; }
    # hud-probe's visual check: each test picture is drawn, you say whether the cluster and the HUD show it, and it is cleared. The
    # answers go into the saved output (they settle the layout where the terminal's own answers do not). Only on a terminal.
    ask_seen() {    # ask_seen <where>: y / n / ? from the terminal
        printf '    %s: do you see the white square / TEST? [y/n/?] ' "$1"
        as_a=; read -r as_a < /dev/tty || as_a=?
        case "$as_a" in [Yy]*) echo y ;; [Nn]*) echo n ;; *) echo '?' ;; esac
    }
    hud_visual_check() {
        if [ "$ASSUME_YES" -eq 1 ] || [ ! -t 0 ] || [ ! -r /dev/tty ]; then
            note 'Visual check skipped (no terminal): run hud-probe on a terminal to answer what the cluster and the HUD show.'
            return 0
        fi
        step 'Visual check: what the cluster and the HUD show'
        say 'Stop any route first (the BMW navigation or CarPlay would draw over the test pictures). Each picture stays until you answer.'
        printf '\n  Start the visual check? [Y/n] '
        vc_a=; read -r vc_a < /dev/tty || vc_a=n
        case "$vc_a" in ''|[Yy]*) ;; *) say 'Visual check skipped.'; return 0 ;; esac
        echo '--- visual check (your answers) ---' >> "$WORK/out"
        for vc in '6wa:6WA layout (BMW F-series old cluster) on 82/00' 'mini:MINI F54 picture box on 82/00' \
                  '6wb:6WB layout (BMW multifunction cluster) on 82/00' '6wa-hud:6WA layout on the HUD terminal 82/02' \
                  '6wb-hud:6WB layout on the HUD terminal 82/02'; do
            vc_n=${vc%%:*}
            say "$vc_n: ${vc#*:}"
            vc_r=$(omap "/bin/sh $STAGE/carplay-advanced.sh hud-probe --probe-step $vc_n:draw" < /dev/null 2>&1) || :
            case "$vc_r" in
                *'PROBE-STEP'*' taken'*) ;;
                *) note "the plugin did not take the test picture ($vc_r)"; echo "$vc_n: not drawn ($vc_r)" >> "$WORK/out"; continue ;;
            esac
            vc_c=$(ask_seen cluster)
            vc_h=$(ask_seen HUD)
            omap "/bin/sh $STAGE/carplay-advanced.sh hud-probe --probe-step $vc_n:clear" < /dev/null > /dev/null 2>&1 || :
            echo "$vc_n: cluster $vc_c, HUD $vc_h" >> "$WORK/out"
            ok "$vc_n: cluster $vc_c, HUD $vc_h"
        done
    }
    # The restart runs this script from the upload folder (as "restart" does), not from stdin: after an install on a BMW F-series unit
    # "/bin/sh -s" through on -f gave no line in 40 s, while "restart" from the upload answered at once. libcpreboot.so goes along: a
    # head unit without the plugin (or OnOffDSICommander) has nothing else to reboot with.
    restart_staged() {
        omap "/bin/ksh -c 'mkdir -p $STAGE'" < /dev/null || die "Cannot create $STAGE on hu-omap."
        send "$0" "$STAGE/carplay-advanced.sh"
        if [ -f "$SRC/libcpreboot.so" ]; then send "$SRC/libcpreboot.so" "$STAGE/libcpreboot.so"; fi
        remote_reboot "/bin/sh $STAGE/carplay-advanced.sh restart" /dev/null
    }
    remote_reboot() {    # remote_reboot <command on hu-omap> <stdin file>: 0 once the reboot is under way
        rb_out=$WORK/reboot.out; : > "$rb_out"
        omap "$1" < "$2" > "$rb_out" 2>&1 &
        rb_pid=$!; rb_shown=0; rb_t=0
        while :; do
            rb_n=$(wc -l < "$rb_out" | tr -d ' ')
            if [ "$rb_n" -gt "$rb_shown" ]; then sed -n "$((rb_shown + 1)),${rb_n}p" "$rb_out" | show_remote; rb_shown=$rb_n; fi
            if reboot_said "$rb_out"; then sleep 2; break; fi
            kill -0 "$rb_pid" 2>/dev/null || break
            rb_t=$((rb_t + 1)); [ "$rb_t" -lt "$REBOOT_CONFIRM_S" ] || break
            sleep 1
        done
        if reboot_said "$rb_out"; then
            kill "$rb_pid" 2>/dev/null || :
            ok 'The head unit is restarting; it is back in about a minute.'
            return 0
        fi
        if kill -0 "$rb_pid" 2>/dev/null; then
            # no word in time: what ran before (an install, an uninstall) is complete, so a note and exit 3, not an error
            kill "$rb_pid" 2>/dev/null || :
            sed -n "$((rb_shown + 1)),\$p" "$rb_out" | show_remote
            note "The head unit did not confirm the restart in $REBOOT_CONFIRM_S s. What was installed is complete: restart the head unit by"
            note 'hand (ignition off and on), or run: sh carplay-advanced.sh restart'
            return 3
        fi
        rb_rc=0; wait "$rb_pid" || rb_rc=$?
        sed -n "$((rb_shown + 1)),\$p" "$rb_out" | show_remote
        # 255: the connection dropped, which is how a reboot usually ends it
        case "$rb_rc" in 0|255) ok 'The head unit is restarting; it is back in about a minute.'; return 0 ;; *) return "$rb_rc" ;; esac
    }

    # -------- the head unit's software, by hash (read over SSH and hashed here: the head unit has no hash tool)
    remote_sha256() {    # remote_sha256 <path on hu-omap>: its SHA-256, or "missing"
        omap "/bin/ksh -c '[ -f $1 ]'" < /dev/null || { echo missing; return 0; }
        # shellcheck disable=SC2086
        sum=$(omap "/bin/cat $1" < /dev/null | $SHA256) || return 1
        echo "${sum%% *}"
    }
    # iDrive 6 vs iDrive 5, by content not by hash: iDrive 6's CCarPlayActivity::handleControllerInput sends the four knob tilts to
    # CarPlay itself (variant 0x9c71), so HmiMain3D has, for keys 9..12, "isVariantSet(0x9c71); cmp r0,#0; movne <up/down/left/right>,#1"
    # one after another. No iDrive 5 build has it. The installer reads HmiMain3D as the plain ELF (ziploader), so the bytes are searchable.
    ID6_SIG='711c09e308009015003090e50c3093e533ff2fe1000050e3(0150a013|0160a013|01e0a013|01c0a013)'
    is_id6() {    # is_id6 <local copy of HmiMain3D>: true if it has the iDrive 6 native CarPlay tilt handlers
        od -An -v -tx1 "$1" | tr -dc 0-9a-f | grep -qE "$ID6_SIG"
    }
    scan_hash() {    # the SHA-256 of a scan without its hmi line and comments (supported_table "scan" rows)
        # shellcheck disable=SC2086
        sum=$(grep -v -e '^hmi ' -e '^#' "$1" | $SHA256) || return 1
        echo "${sum%% *}"
    }
    known_scan() {    # known_scan <software>: its expected scan hash ("none": mapped by hand, the scan's patterns do not cover it)
        supported_table | while read -r k_name k_file k_sum; do
            if [ "$k_name" = "$1" ] && [ "$k_file" = scan ]; then echo "$k_sum"; fi
        done
    }
    scan_summary() {    # what the scan found, in a few lines
        n_tilt=$(grep -c '^tilt ' "$1" || :)
        n_fs=$(grep -c '^fs ' "$1" || :)
        while read -r s_name s_val; do
            case "$s_name" in touchFn|ctlSetFn|playFn) row "$s_name" "0x$s_val" ;; esac
        done < "$1"
        row 'Knob tilts' "$([ "$n_tilt" -gt 0 ] && echo "found ($n_tilt words)" || echo 'not found: off')"
        row 'Full screen' "$([ "$n_fs" -gt 0 ] && echo "flag code found ($n_fs words)" || echo 'by the layout only (no CARPLAY_FULLSCREEN flag code)')"
    }
    report_scanned() {    # the found build, as a pre-filled issue: the lines that add it to supported_table and kBuilds
        title="Scanned HMI ${hmi_version:-unknown}"
        body="HMI build: ${hmi_version:-unknown}
HmiMain3D sha256: $hash_hmi
libconn_carplay.so sha256: $hash_conn
mm-ipod sha256: $hash_mmipod
scan sha256: $(scan_hash "$WORK/build.tab")
Installer: $VERSION
Result: (please tell us what works: menu, full screen, knob tilts, touchpad)

build.tab:
$(grep -v '^hmi ' "$WORK/build.tab")"
        say 'Please send this build in, so the next version supports it without a scan (the link fills in the hashes and addresses):'
        say "  $ISSUES_URL?title=$(urlencode "$title")&body=$(urlencode "$body")"
    }
    urlencode() {    # percent-encodes what is not safe in a URL query; lines are joined with %0A
        first=1
        printf '%s\n' "$1" | while IFS= read -r line; do
            [ "$first" -eq 1 ] || printf '%%0A'
            first=0
            printf '%s' "$line" | sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/"/%22/g' -e 's/#/%23/g' -e 's/&/%26/g' -e "s/'/%27/g" \
                -e 's/+/%2B/g' -e 's/,/%2C/g' -e 's|/|%2F|g' -e 's/:/%3A/g' -e 's/;/%3B/g' -e 's/</%3C/g' -e 's/=/%3D/g' \
                -e 's/>/%3E/g' -e 's/?/%3F/g' -e 's/@/%40/g' -e 's/\[/%5B/g' -e 's/\\/%5C/g' -e 's/]/%5D/g' -e 's/\^/%5E/g' \
                -e 's/`/%60/g' -e 's/{/%7B/g' -e 's/|/%7C/g' -e 's/}/%7D/g'
        done
    }
    report_unsupported() {
        say ''
        say 'This head unit software is not in the supported list (supported_table in this script):'
        say "  HMI build           ${hmi_version:-unknown}"
        say "  HmiMain3D           $hash_hmi"
        say "  libconn_carplay.so  $hash_conn"
        say "  mm-ipod             $hash_mmipod"
        say 'The plugin patches HmiMain3D at fixed addresses, so every software needs its own entry.'
        say 'To ask for support, open an issue (the link fills in the lines above):'
        title="Support for HMI ${hmi_version:-unknown}"
        body="HMI build: ${hmi_version:-unknown}
HmiMain3D sha256: $hash_hmi
libconn_carplay.so sha256: $hash_conn
mm-ipod sha256: $hash_mmipod
Installer: $VERSION"
        say "  $ISSUES_URL?title=$(urlencode "$title")&body=$(urlencode "$body")"
    }
    if [ "$ACTION" = install ] || [ "$ACTION" = status ]; then
        step 'Checking the head unit software (reads HmiMain3D, about 60 MB)'
        hmi_path=$HMI_BIN
        omap "/bin/ksh -c '[ -f $HMI_BIN ]'" < /dev/null || hmi_path=$HMI_BIN_PLAIN
        hmi_version=$(omap "/bin/cat $HMI_VERSION_FILE" < /dev/null 2>/dev/null | tr -cd '[:print:]') || hmi_version=
        # one read over SSH; the hash, the iDrive 6 check and the scan all use this copy
        omap "/bin/cat $hmi_path" < /dev/null > "$WORK/HmiMain3D" && [ -s "$WORK/HmiMain3D" ] || die "Cannot read $hmi_path on hu-omap."
        if is_id6 "$WORK/HmiMain3D"; then KIND=id6; else KIND=id5; fi
        MODE=${MODE_REQ:-$(default_mode)}; apply_mode
        gen=$([ "$KIND" = id6 ] && echo 'iDrive 6' || echo 'iDrive 5')
        ok "Head unit: $gen, ${hmi_version:-unknown}"
        ok "Install mode: $MODE${MODE_REQ:+ (chosen)}${MODE_REQ:- (recommended for this HMI; --full / --hud to change)}"
        # mm-ipod, in every mode: with the directions installed it is a link to the wrapper, so its kept original copy is hashed
        mm_path=$MMIPOD
        if omap "/bin/ksh -c '[ -L $MMIPOD ] && [ -f $MMIPOD_BIN ]'" < /dev/null; then mm_path=$MMIPOD_BIN; fi
        hash_mmipod=$(remote_sha256 "$mm_path") || die "Cannot read $mm_path on hu-omap."
        mm_known=$(mmipod_known "$hash_mmipod")
        if [ "$hash_mmipod" = missing ]; then
            note "No $MMIPOD on this head unit: the turn-by-turn directions need it."
        elif [ -n "$mm_known" ]; then
            ok "mm-ipod is a known one ($(echo $mm_known | sed 's/ /, /g'))."
        else
            note "mm-ipod is not one we know (sha256 $hash_mmipod). The directions hook reads the iPhone's iAP2 messages, not addresses in mm-ipod, so it should work, but it is untested on this one. Please report how it goes."
        fi
        # The full plugin patches HmiMain3D at fixed addresses, so on iDrive 5 the build must be known (by hash). On iDrive 6 the plugin
        # does not patch (it is an unknown build to it; the native features come from the HMI), so no hash is needed. HUD never patches.
        if [ "$MODE" = full ] && [ "$KIND" = id5 ]; then
            # shellcheck disable=SC2086
            hash_hmi=$($SHA256 < "$WORK/HmiMain3D") || die "Cannot hash $WORK/HmiMain3D."
            hash_hmi=${hash_hmi%% *}
            hash_conn=$(remote_sha256 "$CONN_LIB") || die "Cannot read $CONN_LIB on hu-omap."
            SOFTWARE=$(supported_as "$hash_hmi" "$hash_conn")
            # the scan runs for known builds too: it must find exactly the built-in addresses there (a check of the scanner)
            say 'Searching HmiMain3D for the places the plugin uses (about 20 s)...'
            if hmi_scan "$WORK/HmiMain3D" "$hmi_version" > "$WORK/build.tab" 2> "$WORK/scan.err"; then scan_ok=1; else scan_ok=0; fi
            if [ -n "$SOFTWARE" ]; then
                ok "Supported software: $SOFTWARE (HmiMain3D and libconn_carplay.so match by hash)"
                want=$(known_scan "$SOFTWARE")
                if supported_table | grep "^$SOFTWARE  *fullscreen  *none\$" >/dev/null 2>&1; then
                    note "$SOFTWARE has no Full-screen CarPlay yet (its layouts are an older format): that option stays off."
                fi
                if [ "$want" = none ]; then
                    ok "$SOFTWARE was mapped by hand (the HMI scan's patterns do not cover it); the install uses the built-in table."
                elif [ "$scan_ok" -eq 1 ] && [ -n "$want" ] && [ "$(scan_hash "$WORK/build.tab")" = "$want" ]; then
                    ok 'The HMI scan finds the same addresses as the built-in table.'
                else
                    note "The HMI scan does not agree with the built-in table for $SOFTWARE ($(cat "$WORK/scan.err" 2>/dev/null || :)); the install uses the built-in table. Please report this."
                fi
            elif [ "$scan_ok" -eq 1 ]; then
                SOFTWARE=scanned
                say ''
                say "This HMI build is not supported yet: ${hmi_version:-unknown}"
                say 'but every place the plugin uses was found in its HmiMain3D:'
                scan_summary "$WORK/build.tab"
                say 'The plugin checks every word before it writes it; a wrong address changes nothing. Untested on this build.'
                if [ "$ACTION" = install ] && [ "$DRY_RUN" -eq 0 ]; then
                    if [ "$ASSUME_YES" -eq 1 ]; then
                        say 'Installing with the scanned addresses (--yes).'
                    elif [ -t 0 ] && [ -r /dev/tty ]; then
                        printf '\n  Your HMI is not supported yet; install the full plugin with the scanned addresses? [y/N] '
                        answer=; read -r answer < /dev/tty || answer=n
                        case "$answer" in [Yy]*) ;; *) printf '\n'; report_scanned; say 'Nothing was installed.'; exit 1 ;; esac
                    else
                        report_scanned
                        say 'Nothing was installed: run again on a terminal (or with --yes) to install with the scanned addresses.'
                        exit 1
                    fi
                fi
            else
                report_unsupported
                say "The HMI scan did not find everything the plugin needs ($(cat "$WORK/scan.err" 2>/dev/null || :))."
                say 'The full plugin needs a known build. You can still install the directions and the HUD-only menu with --hud (no HMI patch).'
                if [ "$ACTION" = install ]; then say 'Nothing was installed.'; exit 1; fi
            fi
        elif [ "$MODE" = full ] && [ "$KIND" = id6 ]; then
            say 'Note: on iDrive 6 the menu appears only if the car is coded for apps (CarPlay fullscreen variant 0x9c71); the full'
            say "plugin's HMI patches apply only to builds it knows, so here it adds the menu and the HUD and leaves fullscreen/tilts/touchpad to the HMI."
        else
            say 'Turn-by-turn directions and the HUD-only menu, no HMI patch. The directions apply on the next CarPlay connection (no'
            say 'reboot); the HUD-only menu shows after a restart, where the car is coded for apps (A4A).'
        fi
    fi

    # -------- the plan, confirmed: on a terminal, nothing is uploaded or changed before a yes (--yes, a dry run or no terminal: no question)
    if [ "$ACTION" = install ] && [ "$DRY_RUN" -eq 0 ] && [ "$ASSUME_YES" -eq 0 ] && [ -t 0 ] && [ -r /dev/tty ]; then
        step 'Ready to install'
        if [ "$MODE" = full ]; then
            row 'What' 'the full plugin: menu, HMI patches, full-screen CarPlay, knob and touchpad, HUD directions'
        else
            row 'What' 'turn-by-turn directions (route guidance in mm-ipod) and the HUD-only menu: no HMI patch'
        fi
        row 'Where' "$gen, ${hmi_version:-unknown} at $SSH_HOST"
        row 'Backup' 'every file it replaces is backed up first; "uninstall" puts it all back'
        printf '\n  Proceed with the installation? [Y/n] '
        answer=; read -r answer < /dev/tty || answer=n
        case "$answer" in ''|[Yy]*) ;; *) printf '\n'; say 'Nothing was installed.'; exit 1 ;; esac
    fi

    # -------- a copy here of what the run may change, before anything is sent
    PRE=
    if { [ "$ACTION" = install ] || [ "$ACTION" = uninstall ]; } && [ "$DRY_RUN" -eq 0 ]; then
        step 'Copying the head unit files this run may change to this computer'
        pre_backup || die "The copy to ${PRE:-$LOCAL_BACKUPS} failed; nothing was sent to the head unit or changed."
        ok "Copied to BACKUP/${PRE##*/} ($n_pre files; manifest.txt lists them)."
    fi

    # -------- upload, run on hu-omap, fetch the backup
    [ "$ACTION" != install ] || step 'Uploading the plugin'
    omap "/bin/ksh -c 'rm -rf $STAGE; mkdir -p $STAGE/CarPlayMenu'" < /dev/null || die "Cannot create $STAGE on hu-omap."
    send "$0" "$STAGE/carplay-advanced.sh"
    args=$ACTION
    if [ "$ACTION" = install ]; then
        for item in $FILES; do send "$SRC/$item" "$STAGE/CarPlayMenu/$item"; done
        [ "$SOFTWARE" != scanned ] || send "$WORK/build.tab" "$STAGE/CarPlayMenu/build.tab"
        ok "Uploaded plugin/ to $STAGE on hu-omap and compared every file."
        args="$args --payload $STAGE/CarPlayMenu --kind $KIND --mode $MODE"
        { [ "$MODE" = full ] && [ "$KIND" = id5 ]; } && args="$args --hmi $SOFTWARE" || :
    fi
    [ "$DRY_RUN" -eq 0 ] || args="$args --dry-run"
    [ "$KEEP_CONFIG" -eq 0 ] || args="$args --keep-config"
    [ -z "$CLUSTER_FORCE" ] || args="$args --cluster $CLUSTER_FORCE"
    case "$ACTION" in
        install) if [ "$DRY_RUN" -eq 1 ]; then step 'Checking the installation on the head unit (dry run)'; else step 'Installing on the head unit'; fi ;;
        uninstall) step 'Uninstalling on the head unit' ;;
        status) step 'State on the head unit' ;;
        restart) step 'Restarting the head unit' ;;
        hud-probe) step 'Asking the cluster and the HUD (about 40 s)' ;;
        *) step "$ACTION on the head unit" ;;
    esac
    if [ "$ACTION" = restart ]; then
        restart_staged || exit $?
        exit 0
    fi
    { run_rc=0; omap "/bin/sh $STAGE/carplay-advanced.sh $args" < /dev/null 2>&1 || run_rc=$?; echo "$run_rc" > "$WORK/rc"; } \
        | tee "$WORK/out" | show_remote
    read -r rc < "$WORK/rc" || rc=1
    if [ "$ACTION" = hud-probe ] && [ "$rc" -eq 0 ]; then hud_visual_check; fi
    omap "/bin/ksh -c 'rm -rf $STAGE'" < /dev/null > /dev/null 2>&1 || say "Note: $STAGE is left on hu-omap (removed by the next run)."
    if [ "$ACTION" = hud-probe ]; then
        out=$LOCAL_BACKUPS/hud-probe-$(date +%Y%m%d-%H%M%S).txt
        if mkdir -p "$LOCAL_BACKUPS" && cp "$WORK/out" "$out"; then
            ok "Saved: BACKUP/${out##*/} (attach it to your issue; it holds no VIN or passwords)."
        fi
        exit "$rc"
    fi

    done_line=$(grep '^SUCCESS: .* completed\. Previous files: ' "$WORK/out" || :)
    backup_ok=0
    if [ "$rc" -eq 0 ] && [ -n "$done_line" ]; then
        backup=${done_line#*Previous files: }; backup=${backup%% ;*}
        originals=${done_line##*system originals: }; originals=$(echo "$originals" | tr -d ' \r')
        step 'Copying the backup to this computer'
        if fetch_tree "$backup" "$LOCAL_BACKUPS/${backup##*/}" && fetch_tree "$originals" "$LOCAL_BACKUPS/originals"; then
            cp "$WORK/out" "$LOCAL_BACKUPS/${backup##*/}/installer-output.log" 2>/dev/null || :
            ok "Backup copied to BACKUP/${backup##*/} (full head unit output in installer-output.log there)."
            backup_ok=1
        else
            note "The copy of the backup to $LOCAL_BACKUPS failed; it stays on the head unit in $backup."
            [ "$RESTART" -eq 0 ] || { note 'Restart skipped because of that; run "restart" when you are ready.'; RESTART=0; }
        fi
    elif [ "$rc" -ne 0 ]; then
        die "The head unit reported a failure (exit $rc); nothing is half-done: its backups stay in $BACKUPS there."
    fi

    # -------- summary: what is on the head unit now, and the restart it needs
    if [ "$rc" -eq 0 ] && [ -n "$done_line" ]; then
        cluster_line=$(grep -E '^  (most|kombi|both): ' "$WORK/out" | sed -n 1p | sed -e 's/^  [a-z]*: //' -e 's/\.$//')
        if [ "$ACTION" = install ]; then
            step "${C_G}Done: CarPlay Advanced Settings is installed${C_N}"
            # no case inside $(...): macOS /bin/sh (bash 3.2) misreads its ")" patterns there (issue #9)
            sw_label=
            case "$SOFTWARE" in
                '') ;;
                scanned) sw_label=' (not yet supported: addresses from the HMI scan)' ;;
                *) sw_label=' (supported, checked by hash)' ;;
            esac
            row 'Head unit' "$gen, ${hmi_version:-unknown}$sw_label"
            if [ "$MODE" = full ]; then
                row 'Installed' "the full plugin: menu, HMI patches, full-screen CarPlay, knob and touchpad, HUD directions"
            else
                row 'Installed' 'turn-by-turn directions (route guidance in mm-ipod) and the HUD-only menu: no HMI patch'
            fi
            row 'Plugin' "$PLUGIN ($(echo $FILES | wc -w | tr -d ' ') files)"
            [ "$MODE" != full ] || row 'Boot hook' "$START_SCREEN (one line, removed again by uninstall)"
            [ -z "$cluster_line" ] || row 'Directions' "$cluster_line"
            row 'Settings' "$SETTINGS (kept as they were; new options start off)"
        else
            step "${C_G}Done: CarPlay Advanced Settings is uninstalled${C_N}"
            row 'Removed' "the plugin, the boot hook and the plugin's path-manager links (the system files were never changed)"
            [ "$KEEP_CONFIG" -eq 1 ] && row 'Settings' "kept in $SETTINGS" || row 'Settings' 'moved to the backup'
        fi
        [ -z "$PRE" ] || row 'Before' "BACKUP/${PRE##*/} (the files as they were, copied before the upload)"
        [ "$backup_ok" -eq 0 ] || row 'Backup' "BACKUP/${backup##*/} (and on the head unit: $backup)"
        [ "$backup_ok" -eq 0 ] || row 'Originals' 'BACKUP/originals (the system files the plugin shadows, kept once)'
        if [ "$ACTION" = install ] && [ "$SOFTWARE" = scanned ]; then
            printf '\n'
            report_scanned
        fi
        if [ "$RESTART" -eq 0 ]; then
            if [ "$ACTION" = install ] && [ "$MODE" = hud ]; then
                printf '\n  The directions apply on the next CarPlay connection. %sA restart of the head unit is needed%s for the HUD-only\n' "$C_B" "$C_N"
                printf '  menu (Media/Radio > CarPlay Advanced Settings, on cars coded for apps).\n'
            elif [ "$ACTION" = install ]; then
                printf '\n  %sA restart of the head unit is needed%s to load the plugin. Then: Media/Radio > CarPlay Advanced Settings.\n' "$C_B" "$C_N"
            else
                printf '\n  %sA restart of the head unit is needed%s: the running HMI keeps the plugin until then.\n' "$C_B" "$C_N"
            fi
            if [ "$ASSUME_YES" -eq 0 ] && [ -t 0 ] && [ -r /dev/tty ]; then
                printf '  Restart the head unit now? [Y/n] '
                answer=; read -r answer < /dev/tty || answer=n
                case "$answer" in ''|[Yy]*) RESTART=1 ;; *) say 'Not restarted. Later: sh carplay-advanced.sh restart' ;; esac
            else
                say 'Restart it with: sh carplay-advanced.sh restart'
            fi
        fi
    fi

    if [ "$RESTART" -eq 1 ] && [ "$rc" -eq 0 ] && [ "$DRY_RUN" -eq 0 ]; then
        step 'Restarting the head unit'
        say 'The connection closes with it; the head unit is back in about a minute.'
        restart_staged || exit $?
    fi
    exit "$rc"
fi

# ---------------------------------------------------------------------------------------------------------------- where we run
# On the head unit only the hu-omap node is permitted.
[ "$HOST_SET" -eq 0 ] || die '--host is for runs from a computer.'
case "$(uname -n)" in
    hu-omap|hu-omap.*) ;;
    hu-jacinto|hu-jacinto.*)
        [ "${CPA_NODE_HOP:-0}" = 0 ] || die 'on -f did not reach hu-omap; refusing a forwarding loop.'
        command -v on >/dev/null 2>&1 || die 'QNX on is unavailable; cannot enter hu-omap.'
        [ -d "$REMOTE_NODE/fs/sda0/opt/hmi/ID5" ] || die 'The hu-omap NBT Evo filesystem is not reachable.'
        [ -f "$0" ] && [ -r "$0" ] || die 'Run a saved script file so it can be forwarded to hu-omap.'
        say 'Entering hu-omap automatically (on -f); all file operations will run there.'
        CPA_NODE_HOP=1
        export CPA_NODE_HOP
        # Stream this exact script through stdin; arguments stay separate (no eval of a command string).
        exec on -f "$REMOTE_NODE" /bin/sh -s -- "$@" < "$0"
        ;;
    *) die 'Unexpected QNX node; expected hu-omap or hu-jacinto.' ;;
esac
[ -d "$FS_ROOT/opt/hmi/ID5" ] || die 'The expected NBT Evo ID5/ID6 layout was not found.'
for tool in cat grep pidin; do
    command -v "$tool" >/dev/null 2>&1 || die "Required command is missing: $tool"
done

# Production images may omit id and printf: pidin %a = PID, %W = effective UID. Do not trust USER/LOGNAME.
credentials=$(pidin -p "$$" -F '%a %W') || die 'Cannot read shell credentials with pidin.'
root_found=0
root_invalid=0
while read -r credential_pid credential_uid credential_extra; do
    [ "$credential_pid" = "$$" ] || continue
    root_found=1
    if [ "$credential_uid" != 0 ] || [ -n "$credential_extra" ]; then root_invalid=1; fi
done <<EOF
$credentials
EOF
[ "$root_found" -eq 1 ] && [ "$root_invalid" -eq 0 ] || die 'Root SSH access could not be verified with pidin (effective UID must be 0).'

for target in "$PLUGIN" "$SETTINGS" "$BACKUPS"; do
    [ ! -L "$target" ] || die "Refusing a symbolic-link target: $target"
    if exists "$target"; then [ -d "$target" ] || die "Expected a directory: $target"; fi
done
[ -f "$START_SCREEN" ] || die "$START_SCREEN not found: not an NBT Evo with the expected system image?"
[ ! -L "$START_SCREEN" ] || die "Refusing $START_SCREEN: it is a link, not the system file."

# The plugin patches HMI code at fixed addresses (it checks the words itself, but refuse early): one HMI build only.
hmi_build=
if [ -r "$HMI_VERSION_FILE" ]; then
    { IFS= read -r hmi_build || :; } < "$HMI_VERSION_FILE"
fi

# The iDrive 5 layouts the "Enable fullscreen" switch replaces (same choice as the plugin's boot.sh): MINI's own, or on a BMW the HMI's
# common one (the plugin picks one by the car's brand, HMI_BRAND; both originals are kept)
LAYOUT_PATH=
if exists "$LAYOUT"; then LAYOUT_PATH=$LAYOUT; elif exists "$LAYOUT_ID5"; then LAYOUT_PATH=$LAYOUT_ID5; fi
LAYOUT_COMMON_PATH=
if exists "$LAYOUT_COMMON"; then LAYOUT_COMMON_PATH=$LAYOUT_COMMON
elif exists "$LAYOUT_COMMON_ID5"; then LAYOUT_COMMON_PATH=$LAYOUT_COMMON_ID5; fi

# A shadow link is removed with unshadow. The layout has two links (boot.sh links both spellings), and /opt/hmi/data is itself a link
# to /opt/hmi/ID5/data (car, HMI 1703): with one of them left, the other still shadows the same file. So both go together.
unshadow() {
    rm -f "$1" || return 1
    case "$1" in
        "$LAYOUT"|"$LAYOUT_ID5") for l in "$LAYOUT" "$LAYOUT_ID5"; do if [ -L "$l" ]; then rm -f "$l" || return 1; fi; done ;;
        "$LAYOUT_COMMON"|"$LAYOUT_COMMON_ID5")
            for l in "$LAYOUT_COMMON" "$LAYOUT_COMMON_ID5"; do if [ -L "$l" ]; then rm -f "$l" || return 1; fi; done ;;
    esac
}
layout_linked() { [ -L "$LAYOUT" ] || [ -L "$LAYOUT_ID5" ] || [ -L "$LAYOUT_COMMON" ] || [ -L "$LAYOUT_COMMON_ID5" ]; }

hook_count() { grep -c "$MARK" "$START_SCREEN" 2>/dev/null || :; }
HOOKS=$(hook_count)
case "$HOOKS" in ''|*[!0-9]*) HOOKS=0 ;; esac
# The hook line. In the car /var (-> /fs/sda1) is a path-manager link the launcher makes after /bin/start_screen.sh runs (HMI
# ID5_1703-490): boot.sh is looked up on the disk first. An older hook line (e.g. one that only tried /var) is replaced on install.
LINE="CPM_BOOT=$PLUGIN_DISK/boot.sh; [ -f \$CPM_BOOT ] || CPM_BOOT=$PLUGIN/boot.sh; [ -f \$CPM_BOOT ] && /bin/ksh \$CPM_BOOT > /dev/null 2>&1   # $MARK (carplay-advanced.sh)"
HOOK_OLD=0
if [ "$HOOKS" -eq 1 ]; then
    hook_now=$(grep "$MARK" "$START_SCREEN" 2>/dev/null) || hook_now=
    [ "${hook_now%%#*}" = "${LINE%%#*}" ] || HOOK_OLD=1
fi

# PIDs whose argv[0] is exactly $1 or ends in /$1 (never grep the whole list)
pids_of() {
    listing=$(pidin -F '%a %A') || die 'Cannot read the process list with pidin.'
    seen_pids=' '
    while read -r process_pid process_name process_arguments; do
        case "$process_pid" in ''|0|*[!0-9]*) continue ;; esac
        case "$process_name" in
            "$1"|*/"$1")
                case "$seen_pids" in *" $process_pid "*) continue ;; esac
                echo "$process_pid"
                seen_pids="$seen_pids$process_pid " ;;
        esac
    done <<EOF
$listing
EOF
}
link_state() {
    if [ -z "$1" ]; then echo 'absent'
    elif [ -L "$1" ]; then echo 'shadowed by a link (switch on)'
    elif [ -f "$1" ]; then echo 'original'
    else echo 'absent'; fi
}

# ---------------------------------------------------------------------------------------------------------------- hud-probe
# What the cluster and the HUD have, asked through the plugin (only it has a MOST client): its test channel (one HUDTerminal call per
# line, hud.h runCmds) and "HUD debug log" (hud_debug: the plugin logs every HUDTerminal message, its terminals and its handles).
# Nothing is installed; hud_debug is put back as it was. The answers are read from the plugin log: /dev/shmem/carplaymenu.log, or in
# a HUD-only install (the HUD runs in mm-ipod, during a CarPlay connection) /var/iap/rghook.log.
hud_probe() {
    HP_LOG=$SHMEM/carplaymenu.log
    [ -f "$PLUGIN/hud_only" ] && HP_LOG=${RGHOOK_LOG:-/var/iap/rghook.log}
    HP_CMD=$SHMEM/carplaymenu-hudcmd
    HP_DBG=$SETTINGS/hud_debug
    say "CarPlay Advanced Settings installer $VERSION; hud-probe"
    say "HMI: ${hmi_build:-unknown}"
    say "Plugin: $(if [ -d "$PLUGIN" ]; then echo "installed$([ -f "$PLUGIN/hud_only" ] && echo ' (HUD-only menu: the drawing runs in mm-ipod, during a CarPlay connection)')"; else echo 'not installed'; fi)"
    for f in hud_guidance hud_debug hud_layout hud_inst hud_box hud_box_6wa hud_hide hud_text_handles hud_comma hud_colour_day \
        hud_handles hud_handles_2 cluster; do
        if [ -f "$SETTINGS/$f" ]; then say "  switch $f: on$(v=; read -r v < "$SETTINGS/$f" 2>/dev/null; [ -n "$v" ] && echo " ($v)")"; else say "  switch $f: off"; fi
    done
    say "HmiMain3D PID(s): $(one_line "$(pids_of HmiMain3D)")"
    say "mm-ipod PID(s): $(one_line "$(pids_of mm-ipod)")"
    [ -d "$PLUGIN" ] && [ -d "$SETTINGS" ] || { say 'Nothing to ask: install the plugin (with "CarPlay directions in HUD" on) first.'; return 1; }
    [ -f "$HP_LOG" ] || { say "No plugin log ($HP_LOG): the plugin has not started since the last boot."; return 1; }
    say '--- what the plugin found at its start (MOST client, registry, handles from the set-up, layout, picture box) ---'
    grep -e 'HUD: MOST' -e 'HUD: registry' -e 'HUDTerminal 82' -e 'HUD: layout probe' -e 'HUD: cluster layout' -e 'HUD: picture' \
        -e 'HUD: handles' -e 'HUD: copies' -e 'HUD: other client' -e 'HUD: texts on' -e 'HUD: day' -e 'HUD: night' "$HP_LOG" | sed -n 1,40p
    [ ! -f "$HP_CMD" ] || { say "The test channel is busy ($HP_CMD); try again in a minute."; return 1; }
    hp_start=$(count_lines "$HP_LOG")
    # 1. "HUD debug log" switched on (off and on again if it was on): the plugin logs its terminals and handles, then every message
    HP_WAS=0
    [ -f "$HP_DBG" ] && HP_WAS=1
    if [ "$HP_WAS" -eq 1 ]; then rm -f "$HP_DBG"; sleep "$HUD_PROBE_WAIT"; fi
    echo 1 > "$HP_DBG" || { say "Cannot write $HP_DBG."; return 1; }
    sleep "$HUD_PROBE_WAIT"
    # 2. the surfaces: ImageGetSize (C04, it only reads) for 0x01..0x40, the F54 form [h] and the 6WB form [h][01]
    hp_batch=
    hp_i=1
    while [ "$hp_i" -le 64 ]; do
        hp_h=$(hex2 "$hp_i")
        hp_batch="${hp_batch}c04 00$hp_h
c04 00${hp_h}01
"
        hp_i=$((hp_i + 1))
    done
    echo "$hp_batch" > "$HP_CMD.tmp" && mv "$HP_CMD.tmp" "$HP_CMD"
    hp_t=0
    while [ -f "$HP_CMD" ] && [ "$hp_t" -lt "$HUD_PROBE_WAIT" ]; do sleep 1; hp_t=$((hp_t + 1)); done
    hp_taken=1
    if [ -f "$HP_CMD" ]; then rm -f "$HP_CMD"; hp_taken=0; fi
    sleep "$HUD_PROBE_WAIT"
    [ "$HP_WAS" -eq 1 ] || rm -f "$HP_DBG"
    sed -n "$((hp_start + 1)),\$p" "$HP_LOG" > "$SHMEM/carplaymenu-hudprobe.txt" 2>/dev/null || :
    HP_OUT=$SHMEM/carplaymenu-hudprobe.txt
    say '--- the plugin during the probe ---'
    grep -e 'HUD: debug log' -e 'HUD: handles' -e "HUD: copies' handles" -e 'HUD: cluster layout' "$HP_OUT" | sed -n 1,10p
    if [ "$hp_taken" -eq 0 ]; then
        say "The plugin's HUD did not take the test channel: is \"CarPlay directions in HUD\" on and the HMI running (HUD-only menu:"
        say 'a CarPlay connection running)? Nothing was asked.'
    fi
    say '--- surfaces (ImageGetSize: handle = width x height; 0 x 0 and refusals left out) ---'
    hp_n=0
    while read -r hp_l; do
        case "$hp_l" in *"fkt c04 op d "*) ;; *) continue ;; esac
        set -- ${hp_l##*: }
        if [ "$#" -ge 7 ]; then hp_w=$((16#$4$5)); hp_ht=$((16#$6$7)); hp_form=6WB
        elif [ "$#" -ge 6 ]; then hp_w=$((16#$3$4)); hp_ht=$((16#$5$6)); hp_form=F54
        else continue; fi
        [ "$hp_w" -gt 0 ] && [ "$hp_ht" -gt 0 ] || continue
        say "  0x$1$2: $hp_w x $hp_ht ($hp_form form)"
        hp_n=$((hp_n + 1))
    done < "$HP_OUT"
    [ "$hp_n" -gt 0 ] || say '  none answered with a size'
    say "--- the navigation's set-up seen during the probe (CreateInstance 200 / FrameAddItem 22b answers) ---"
    grep -e 'fkt 200 op d' -e 'fkt 22b op d' "$HP_OUT" | sed -n 1,40p
    say '--- errors from the terminal during the probe (op 9 / f: [code][parameter]) ---'
    grep -e ' op 9 ' -e ' op f ' "$HP_OUT" | grep -v 'fkt c04' | sed -n 1,20p
    say "--- (all $(count_lines "$HP_OUT") log lines of the probe are in $HP_OUT on the head unit) ---"
    say 'SUCCESS: hud-probe completed.'
}
hexdig() { case "$1" in 10) echo a ;; 11) echo b ;; 12) echo c ;; 13) echo d ;; 14) echo e ;; 15) echo f ;; *) echo "$1" ;; esac; }
hex2() {    # hex2 <0..255>: two lowercase hex digits (no printf, cut or awk on the head unit)
    echo "$(hexdig $(($1 / 16)))$(hexdig $(($1 % 16)))"
}
count_lines() { set -- $(wc -l < "$1"); echo "${1:-0}"; }   # wc -l without tr
one_line() { ol=; for ol_w in $1; do ol="$ol$ol_w "; done; echo "$ol"; }
printf_lines() { for pl_l in "$@"; do echo "$pl_l"; done; }
# The visual check's test pictures: a white filled square in the layout's picture area and "TEST" as the distance, shown with that
# layout's visibility calls; "clear" removes it and hides what it showed. "-hud": the same on HUDTerminal 82/02.
probe_pattern() {    # probe_pattern <name> draw|clear: a test-channel batch (generated; white square + "TEST" as distance)
    case "$1:$2" in
        6wa:draw) printf_lines '204 000101' '20e 00050006' '220 0006' '219 000600' '21b 000604' '21d 000600000304005a00280096002800960064005a0064' '204 000301' '210 00030000540045005300540000' ;;
        6wa:clear) printf_lines '220 0006' '210 0003000000' '204 000300' '204 000100' ;;
        6wa-hud:draw) printf_lines '@2' '204 000101' '20e 00050006' '220 0006' '219 000600' '21b 000604' '21d 000600000304005a00280096002800960064005a0064' '204 000301' '210 00030000540045005300540000' ;;
        6wa-hud:clear) printf_lines '@2' '220 0006' '210 0003000000' '204 000300' '204 000100' ;;
        mini:draw) printf_lines '204 000101' '20e 00050006' '220 0006' '219 000600' '21b 000604' '21d 000600000304002d0019004b0019004b0037002d0037' '204 000301' '210 00030000540045005300540000' ;;
        mini:clear) printf_lines '220 0006' '210 0003000000' '204 000300' '204 000100' ;;
        mini-hud:draw) printf_lines '@2' '204 000101' '20e 00050006' '220 0006' '219 000600' '21b 000604' '21d 000600000304002d0019004b0019004b0037002d0037' '204 000301' '210 00030000540045005300540000' ;;
        mini-hud:clear) printf_lines '@2' '220 0006' '210 0003000000' '204 000300' '204 000100' ;;
        6wb:draw) printf_lines '204 001701' '204 002000' '204 001f01' '220 001f' '219 001f06' '21b 001f04' '21d 001f0000030400560050009200500092008c0056008c' '21d 001f0100020500560050009200500092008c0056008c00560050' '204 001b01' '210 001b015445535400' ;;
        6wb:clear) printf_lines '220 001f' '210 001b0100' '204 001b00' '204 001700' ;;
        6wb-hud:draw) printf_lines '@2' '204 001701' '204 002000' '204 001f01' '220 001f' '219 001f06' '21b 001f04' '21d 001f0000030400560050009200500092008c0056008c' '21d 001f0100020500560050009200500092008c0056008c00560050' '204 001b01' '210 001b015445535400' ;;
        6wb-hud:clear) printf_lines '@2' '220 001f' '210 001b0100' '204 001b00' '204 001700' ;;
        *) return 1 ;;
    esac
}

hud_probe_step() {    # one test picture through the plugin's test channel; prints whether the plugin took it
    hs_cmd=$SHMEM/carplaymenu-hudcmd
    hs_batch=$(probe_pattern "${PROBE_STEP%%:*}" "${PROBE_STEP##*:}") || { say "Unknown test picture: $PROBE_STEP"; return 1; }
    [ ! -f "$hs_cmd" ] || { say 'PROBE-STEP busy'; return 1; }
    echo "$hs_batch" > "$hs_cmd.tmp" && mv "$hs_cmd.tmp" "$hs_cmd"
    hs_t=0
    while [ -f "$hs_cmd" ] && [ "$hs_t" -lt "$HUD_PROBE_WAIT" ]; do sleep 1; hs_t=$((hs_t + 1)); done
    if [ -f "$hs_cmd" ]; then rm -f "$hs_cmd"; say "PROBE-STEP $PROBE_STEP not taken"; return 1; fi
    say "PROBE-STEP $PROBE_STEP taken"
}
if [ "$ACTION" = hud-probe ] && [ -n "$PROBE_STEP" ]; then
    hud_probe_step
    exit $?
fi
if [ "$ACTION" = hud-probe ]; then
    hud_probe
    exit $?
fi

# ---------------------------------------------------------------------------------------------------------------- restart
if [ "$ACTION" = restart ]; then
    if [ "$DRY_RUN" -eq 1 ]; then say 'DRY RUN: the head unit would be restarted.'; exit 0; fi
    restart_unit
    exit 0                                          # restart only: never on into the install / uninstall below
fi

# ---------------------------------------------------------------------------------------------------------------- the cluster
# Where CarPlay directions go: the KOMBI's route-guidance service over Ethernet (SOME/IP 0xB006 at 160.48.199.96:30502; G-series, J29)
# or the MOST HUDTerminal (F-series), or both. One of four librghook builds goes in as librghook.so:
#             full (the menu plugin draws on MOST)      HUD-only (without the full plugin: mm-ipod draws)
#   most      librghook.so (no network code)            librghook-most.so (draws on MOST from mm-ipod: the plugin's hud.h)
#   kombi     librghook-kombi.so (sends to the KOMBI)   librghook-kombi.so
#   both      librghook-kombi.so                        librghook-both.so
# The menu plugin draws on MOST unless "cluster" says kombi.
# The default comes from libcpprobe.so (it only opens and closes a TCP connection to the KOMBI; nothing is sent): KOMBI found = kombi,
# else most (also when it gives no answer). It cannot see a MOST HUDTerminal: both only with --cluster both.
# The HUD shows what the cluster's guidance shows, so a fitted HUD gets the arrows either way; whether one is fitted is not visible here.
cluster_probe() {   # cluster_probe <libcpprobe.so>: the probe's line ("kombi=1" / "kombi=0 <reason>"), empty if it gave none
    [ -f "$1" ] || return 0
    line=$(LD_PRELOAD=$1 "$PROBE_HOST" < /dev/null 2>/dev/null) || line=
    case "$line" in kombi=*) echo "$line" ;; esac
}
cluster_kind() { case "$1" in kombi=1*) echo kombi ;; kombi=0*) echo most ;; esac; }
cluster_say() {     # cluster_say <probe line>
    case "$1" in
        kombi=1*) say 'Cluster: Ethernet KOMBI found (160.48.199.96:30502).' ;;
        kombi=0*) say "Cluster: no Ethernet KOMBI (${1#kombi=0 }): MOST HUDTerminal (F-series)." ;;
        *)        say 'Cluster: not detected (the probe gave no answer).' ;;
    esac
}

# ---------------------------------------------------------------------------------------------------------------- status
if [ "$ACTION" = status ]; then
    say "CarPlay Advanced Settings installer $VERSION"
    say "HMI: ${hmi_build:-unknown}"
    if [ -d "$PLUGIN" ]; then
        say "Plugin: installed in $PLUGIN"
        for item in $FILES; do [ -f "$PLUGIN/$item" ] || say "  missing: $item"; done
    else
        say "Plugin: not installed ($PLUGIN)"
    fi
    if [ "$HOOKS" -gt 0 ]; then say "Boot hook: installed in $START_SCREEN"; else say "Boot hook: not installed"; fi
    if [ -d "$SETTINGS" ]; then
        switches=
        for f in "$SETTINGS"/*; do [ -f "$f" ] && [ ! -L "$f" ] || continue; switches="$switches ${f##*/}"; done
        say "Settings files in $SETTINGS:${switches:- none}"
    else
        say "Settings: none ($SETTINGS)"
    fi
    say "Layout ${LAYOUT_PATH:-(no MINI iDrive 5 layout)}: $(link_state "$LAYOUT_PATH")"
    say "Layout ${LAYOUT_COMMON_PATH:-(no common iDrive 5 layout)}: $(link_state "$LAYOUT_COMMON_PATH")"
    say "$MMIPOD: $(link_state "$MMIPOD")"
    say "$CONNECTIVITY: $(link_state "$CONNECTIVITY")"
    say "$CARPLAY_INI: $(link_state "$CARPLAY_INI")"
    if [ -f "$PLUGIN/libcpprobe.so" ]; then cluster_say "$(cluster_probe "$PLUGIN/libcpprobe.so")"; fi
    if [ -f "$SETTINGS/cluster" ]; then say "CarPlay directions (recorded at install): $(cat "$SETTINGS/cluster")"; fi
    for p in HmiMain3D Connectivity mm-ipod; do
        pids=$(pids_of "$p") || exit 1
        say "$p PID(s): $(echo ${pids:-none})"
    done
    if exists "$NBT_AAD_PLUGIN"; then say "NBT-AAD plugin also present: $NBT_AAD_PLUGIN (not touched by this script)"; fi
    if [ -d "$BACKUPS" ]; then
        say "Backups in $BACKUPS:"
        for b in "$BACKUPS"/*; do if [ -d "$b" ]; then say "  ${b##*/}"; fi; done
    else
        say "Backups: none yet"
    fi
    exit 0
fi

# Support is decided by the hashes of HmiMain3D and libconn_carplay.so (supported_table). The head unit has no hash tool: the computer
# hashes them and passes the software it found.
if [ "$ACTION" = install ] && [ "$MODE" = full ] && [ "$KIND" = id5 ]; then
    [ -n "$SOFTWARE" ] || die 'the full plugin on iDrive 5 checks the head unit software by hash, which needs a computer: run this script there (it passes --hmi), or install the HUD only with --hud.'
    if [ "$SOFTWARE" = scanned ]; then
        # the computer's HMI scan (hmi_scan) for a build that is not in supported_table: build.tab must name this HMI build
        [ -f "$PAYLOAD/build.tab" ] && [ -s "$PAYLOAD/build.tab" ] || die "--hmi scanned needs $PAYLOAD/build.tab (made by the computer's HMI scan)."
        tab_hmi=
        { IFS= read -r tab_hmi || :; } < "$PAYLOAD/build.tab"
        hmi_now=
        if [ -r "$HMI_VERSION_FILE" ]; then { IFS= read -r hmi_now || :; } < "$HMI_VERSION_FILE"; fi
        [ "$tab_hmi" = "hmi $hmi_now" ] || die "build.tab is for another HMI build ('${tab_hmi#hmi }', this one is '$hmi_now'); not installed."
    else
        is_software_name "$SOFTWARE" || die "Unknown software '$SOFTWARE' (see supported_table)."
    fi
fi
[ "$HOOKS" -le 1 ] || die "$START_SCREEN has $HOOKS boot-hook lines; expected 0 or 1. Fix it by hand (keep one) before running this."
for tool in chmod cp date df diff ksh mkdir mount mv rm sleep wc; do
    command -v "$tool" >/dev/null 2>&1 || die "Required command is missing: $tool"
done

# ---------------------------------------------------------------------------------------------------------------- the plugin files
complete_payload() {
    for item in $FILES; do [ -f "$1/$item" ] && [ -s "$1/$item" ] || return 1; done
}
if [ "$ACTION" = install ]; then
    [ -n "$PAYLOAD" ] || die 'install needs the plugin files: run this script from a computer (it uploads plugin/), or pass --payload <folder>.'
    PAYLOAD=${PAYLOAD%/}
    [ -d "$PAYLOAD" ] && [ ! -L "$PAYLOAD" ] || die "Not a real directory: $PAYLOAD"
    complete_payload "$PAYLOAD" || die "No complete plugin in $PAYLOAD (needs $FILES)."
fi

# Header checks with dd + diff (production images may lack od/cmp/awk). Small files in the plugin folder, removed on
# every exit; /dev/shmem has no subdirectories. No binary data passes through shell variables.
emit_bytes() {
    if [ "$BYTE_ECHO" = plain ]; then echo "$1\c"; else echo -e "$1\c"; fi
}
compare_chunk() {
    dd if="$1" of="$VALIDATION_DIR/actual" bs=1 skip="$2" count="$3" 2>/dev/null || return 1
    emit_bytes "$4" > "$VALIDATION_DIR/expected" || return 1
    diff "$VALIDATION_DIR/expected" "$VALIDATION_DIR/actual" >/dev/null 2>&1
}
size_of() {
    sz=$(wc -c < "$1") || return 1
    set -- $sz
    [ "$#" -eq 1 ] || return 1
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    echo "$1"
}
check_elf() {
    [ -f "$1" ] && [ -s "$1" ] && [ ! -L "$1" ] || die "Missing/empty/symlink library: $1"
    elf_size=$(size_of "$1") || die "Cannot read the size of $1"
    [ "$elf_size" -ge 52 ] || die "Truncated ELF header: $1"
    compare_chunk "$1" 0 7 '\0177ELF\0001\0001\0001' || die "Not a little-endian ELF32 file: $1"
    compare_chunk "$1" 16 4 '\0003\0000\0050\0000' || die "Not an ARM shared library: $1"
    compare_chunk "$1" 40 2 '\0064\0000' || die "Invalid ELF header size: $1"
}
check_zip() {
    compare_chunk "$1" 0 4 '\0120\0113\0003\0004' || die "Invalid ZIP signature: $1"
}
validate_payload() (
    if [ "$(echo '\0101\c')" = A ]; then
        BYTE_ECHO=plain
    elif [ "$(echo -e '\0101\c')" = A ]; then
        BYTE_ECHO=escapes
    else
        die 'This shell cannot emit the binary header reference bytes.'
    fi
    VALIDATION_DIR=$PAYLOAD/check-$$
    mkdir "$VALIDATION_DIR" || die "Cannot create the temporary header checks in $PAYLOAD. It must be writable."
    trap 'rm -rf "$VALIDATION_DIR"' 0
    trap 'exit 129' 1
    trap 'exit 130' 2
    trap 'exit 143' 15
    for item in $FILES; do
        [ ! -L "$PAYLOAD/$item" ] || die "Refusing a symbolic-link file: $PAYLOAD/$item"
    done
    for item in $LIBS; do check_elf "$PAYLOAD/$item"; done
    check_zip "$PAYLOAD/CarPlayMenu_ALL_Texts.zip"
    grep '<name>CarPlayMenu</name>' "$PAYLOAD/CarPlayMenu.manifest" >/dev/null 2>&1 || die 'CarPlayMenu.manifest does not name the CarPlayMenu plugin.'
    menu_xml=CarPlayMenu.xml; [ "$MODE" = hud ] && menu_xml=CarPlayMenu-hud.xml
    grep 'pluginApp' "$PAYLOAD/$menu_xml" >/dev/null 2>&1 || die "$menu_xml is not an RHMI plugin description."
    for item in boot.sh; do
        ksh -n "$PAYLOAD/$item" >/dev/null 2>&1 || die "$item does not parse with ksh -n."
    done
    [ "$MODE" = full ] && { ksh -n "$PAYLOAD/restart.sh" >/dev/null 2>&1 || die 'restart.sh does not parse with ksh -n.'; } || :
)

if [ "$ACTION" = install ]; then
    command -v dd >/dev/null 2>&1 || die 'Required command is missing: dd'
    validate_payload
fi

# ---------------------------------------------------------------------------------------------------------------- the plan
ORIG=$BACKUPS/originals
# name:system path of every file the plugin shadows; the original is kept once in $ORIG.
# id6 shadows only mm-ipod (route guidance); fullscreen layout, Connectivity and carplay.ini are iDrive 5 / MINI features.
if [ "$MODE" = hud ]; then
    SHADOWED="mm-ipod:$MMIPOD"
else
    SHADOWED="Spec_State_Merged.pbw:$LAYOUT_PATH Spec_State_Merged-common.pbw:$LAYOUT_COMMON_PATH mm-ipod:$MMIPOD"
    SHADOWED="$SHADOWED Connectivity:$CONNECTIVITY carplay.ini:$CARPLAY_INI"
fi

flush_if_available() {
    if command -v sync >/dev/null 2>&1; then sync; fi
}
if ! command -v sync >/dev/null 2>&1; then
    say 'Note: sync is not installed; no explicit filesystem flush will be requested.'
fi

need=0
add_need() { s=$(size_of "$1") || die "Cannot read the size of $1"; need=$((need + s)); }

if [ "$ACTION" = uninstall ] && ! exists "$PLUGIN" && ! exists "$SETTINGS" && [ "$HOOKS" -eq 0 ] \
    && [ ! -L "$MMIPOD" ] && [ ! -L "$CONNECTIVITY" ] && [ ! -L "$CARPLAY_INI" ] && ! layout_linked; then
    say 'Already uninstalled. No changes.'
    exit 0
fi
say "Action: $ACTION; plugin source: ${PAYLOAD:-not needed}"
say "HMI: $hmi_build${SOFTWARE:+ (supported software $SOFTWARE, checked by hash)}"
say 'Backups first (nothing is changed before its backup is written and compared):'
say "  $START_SCREEN -> <backup>/start_screen.sh"
add_need "$START_SCREEN"
if ! exists "$ORIG/start_screen.sh"; then say "  $START_SCREEN without the hook line -> $ORIG/start_screen.sh (kept once)"; add_need "$START_SCREEN"; fi
for entry in $SHADOWED; do
    name=${entry%%:*}; path=${entry#*:}
    if [ -z "$path" ]; then
        case "$name" in
            Spec_State_Merged.pbw) say "  $name: no MINI iDrive 5 layout on this unit (a BMW uses the common one)" ;;
            *) say "  $name: no common iDrive 5 layout on this unit ('Enable fullscreen' on a BMW will have nothing to replace)" ;;
        esac
        continue
    fi
    if exists "$ORIG/$name"; then
        say "  $path: original already kept in $ORIG/$name (compared again)"
    elif [ -L "$path" ]; then
        say "  $path: shadowed by an earlier link; the link is removed to reach the original, which goes to $ORIG/$name (relinked after)"
        # its size: the plugin's own copy of the original (bin/), else the file the link points to
        if [ -f "$SETTINGS/bin/$name" ]; then add_need "$SETTINGS/bin/$name"; elif [ -f "$path" ]; then add_need "$path"; fi
    elif [ -f "$path" ]; then
        say "  $path -> $ORIG/$name (kept once)"; add_need "$path"
    else
        say "  $path: absent, nothing to keep"
    fi
done
if [ -d "$PLUGIN" ]; then say "  $PLUGIN -> <backup>/plugin (moved)"; fi
if [ -d "$SETTINGS" ]; then
    if [ "$ACTION" = install ]; then
        say "  $SETTINGS: switches stay in place; a copy of its files goes to <backup>/settings-copy"
        for f in "$SETTINGS"/*; do if [ -f "$f" ] && [ ! -L "$f" ]; then add_need "$f"; fi; done
    elif [ "$KEEP_CONFIG" -eq 1 ]; then
        say "  $SETTINGS: kept in place (--keep-config)"
    else
        say "  $SETTINGS -> <backup>/settings (moved)"
    fi
fi
say 'Changes:'
if [ "$ACTION" = install ]; then
    for item in $FILES; do add_need "$PAYLOAD/$item"; done
    if [ -d "$PLUGIN" ]; then say "  update $PLUGIN from $PAYLOAD"; else say "  install $PLUGIN from $PAYLOAD"; fi
    if [ "$MODE" = hud ]; then
        add_need "$MMIPOD"
        say "  build the route-guidance wrapper in $SETTINGS (a copy of mm-ipod + the librghook preload); hud_guidance switch on"
    fi
    if [ "$HOOK_OLD" -eq 1 ]; then say "  $START_SCREEN: replace the older boot-hook line with the current one"
    elif [ "$HOOKS" -eq 1 ]; then say "  $START_SCREEN: boot hook already present, left as it is"; else say "  $START_SCREEN: add the boot-hook line after the shebang"; fi
    say '  relink the switched-on system files with the new boot.sh'
else
    if [ -d "$PLUGIN" ]; then say "  remove $PLUGIN (moved to the backup)"; fi
    if [ "$HOOKS" -eq 1 ]; then say "  $START_SCREEN: remove the boot-hook line"; fi
    say '  remove the plugin path-manager links (only links, never a file), so the originals are used again'
fi
if exists "$NBT_AAD_PLUGIN"; then say "Note: NBT-AAD is installed too ($NBT_AAD_PLUGIN); this script does not touch it."; fi
CLUSTER_KIND=
RGHOOK=librghook.so
if [ "$ACTION" = install ]; then
    if [ -n "$CLUSTER_FORCE" ]; then
        CLUSTER_KIND=$CLUSTER_FORCE
        say "Cluster: $CLUSTER_KIND (--cluster)"
    else
        CLUSTER_LINE=$(cluster_probe "$PAYLOAD/libcpprobe.so")
        cluster_say "$CLUSTER_LINE"
        CLUSTER_KIND=$(cluster_kind "$CLUSTER_LINE")
    fi
    if [ -z "$CLUSTER_KIND" ]; then
        CLUSTER_KIND=most
        say '  A car with an Ethernet KOMBI (G-series, J29): run install again with --cluster kombi (or both).'
    fi
    case "$MODE:$CLUSTER_KIND" in
        full:most) RGHOOK=librghook.so ;;
        hud:most)  RGHOOK=librghook-most.so ;;
        hud:both)  RGHOOK=librghook-both.so ;;
        *)         RGHOOK=librghook-kombi.so ;;
    esac
    case "$CLUSTER_KIND" in
        kombi) say "  kombi: CarPlay directions to the KOMBI over SOME/IP from mm-ipod ($RGHOOK; not car-verified with CarPlay yet)." ;;
        both)  say "  both: the KOMBI over SOME/IP and the MOST HUDTerminal ($RGHOOK)." ;;
        *)     say "  most: CarPlay directions on the MOST HUDTerminal ($RGHOOK)." ;;
    esac
    if [ "$MODE" = hud ] && [ "$CLUSTER_KIND" != kombi ]; then
        say '  Drawn on MOST from mm-ipod (without the full plugin): not car-verified yet.'
    fi
    say "  Shown in the cluster, and in the HUD if one is fitted. Recorded in $SETTINGS/cluster."
fi

# free space on /var for the backups and the plugin (moves are renames on the same partition and need none)
avail_kb=
df_out=$(df -k -P "${BACKUPS%/*}" 2>/dev/null) || df_out=
line_no=0
while read -r df_fs df_blocks df_used df_avail df_rest; do
    line_no=$((line_no + 1))
    [ "$line_no" -eq 2 ] || continue
    case "$df_avail" in ''|*[!0-9]*) ;; *) avail_kb=$df_avail ;; esac
done <<EOF
$df_out
EOF
need_kb=$(( (need + 1023) / 1024 + SPACE_MARGIN_KB ))
if [ -n "$avail_kb" ]; then
    say "Space on ${BACKUPS%/*}: ${avail_kb} KiB free, about ${need_kb} KiB needed."
    [ "$avail_kb" -ge "$need_kb" ] || die "Not enough free space on ${BACKUPS%/*} for the backups."
else
    say "Note: free space on ${BACKUPS%/*} could not be read; about ${need_kb} KiB are needed."
fi

if [ "$DRY_RUN" -eq 1 ]; then
    say 'DRY RUN: all checks passed. Nothing was written, linked, remounted or restarted.'
    exit 0
fi

# ---------------------------------------------------------------------------------------------------------------- transaction
copy_verified() {   # copy_verified <from> <to>: copy and compare
    cp "$1" "$2" || die "Cannot copy $1 to $2"
    diff "$1" "$2" >/dev/null 2>&1 || die "Copy verification failed: $2"
}
relink() {          # the plugin's own boot.sh makes the links its switches need (only replaces links)
    if [ -f "$PLUGIN/boot.sh" ]; then
        ksh "$PLUGIN/boot.sh" >/dev/null 2>&1 || say "Warning: $PLUGIN/boot.sh failed; the links are made again at the next boot."
    fi
}
recover_directory() {   # recover_directory <live> <backup> <staged or ''> <failed>
    if exists "$2"; then
        if exists "$1"; then mv "$1" "$4" || rollback_failed=1; fi
        restore_directory "$2" "$1"
    elif [ "$ACTION" = install ] && [ -n "$3" ] && ! exists "$3" && exists "$1"; then
        # fresh installation: the stage was moved into a previously absent target
        mv "$1" "$4" || rollback_failed=1
    fi
}
restore_directory() {
    # mv of a directory onto an existing directory would silently nest it.
    if exists "$2"; then
        say "Cannot restore $1: destination $2 is occupied."
        rollback_failed=1
    else
        mv "$1" "$2" || rollback_failed=1
    fi
}
restore_start_screen() {
    [ -f "$BACKUP/start_screen.sh" ] || return 0
    diff "$BACKUP/start_screen.sh" "$START_SCREEN" >/dev/null 2>&1 && return 0
    r=${START_SCREEN%/*}/.start_screen.sh.carplay-advanced-restore
    if cp "$BACKUP/start_screen.sh" "$r" && chmod 755 "$r" && mv -f "$r" "$START_SCREEN" \
        && diff "$BACKUP/start_screen.sh" "$START_SCREEN" >/dev/null 2>&1; then
        say "$START_SCREEN restored."
    else
        rm -f "$r" 2>/dev/null || :
        rollback_failed=1
    fi
}
rollback() {
    say 'Rolling back file changes. Do not reboot until this finishes.'
    rollback_failed=0
    if [ -f "$BACKUP/COMMITTED" ]; then
        mv "$BACKUP/COMMITTED" "$BACKUP/COMMIT_INTERRUPTED" || rollback_failed=1
    fi
    # Inspect completed renames instead of flags: a signal can arrive between a rename and the next assignment.
    recover_directory "$PLUGIN" "$BACKUP/plugin" "$BACKUP/staged-plugin" "$BACKUP/failed-plugin"
    # id6 stages $SETTINGS too (the route-guidance wrapper); on id5 install it is left in place, so there is no staged copy.
    settings_staged=''; [ "$MODE" = hud ] && [ "$ACTION" = install ] && settings_staged=$BACKUP/staged-settings
    recover_directory "$SETTINGS" "$BACKUP/settings" "$settings_staged" "$BACKUP/failed-settings"
    restore_start_screen
    if [ "$rollback_failed" -eq 0 ]; then
        echo 'Previous files restored.' > "$BACKUP/ROLLED_BACK" || :
        say 'Previous files restored. Restart the head unit.'
    else
        echo 'Manual recovery required.' > "$BACKUP/ROLLBACK_FAILED" || :
        say "ROLLBACK INCOMPLETE. Keep the backups: $BACKUP ; $ORIG"
    fi
    flush_if_available || :
}
release_lock() {
    if [ "$LOCKED" -eq 1 ]; then
        [ -d "$LOCK" ] && [ ! -L "$LOCK" ] || return 1
        lock_owner=
        read -r lock_owner < "$LOCK/pid" || return 1
        [ "$lock_owner" = "$$" ] || return 1
        rm -rf "$LOCK" || return 1
        LOCKED=0
    fi
}
finish() {
    result=$1
    trap - 0 1 2 15
    set +e
    if [ "$ACTIVE" -eq 1 ]; then rollback; fi
    if [ -n "$SS_TEMP" ]; then rm -f "$SS_TEMP" 2>/dev/null; fi
    # links removed to reach an original, and the operation did not commit: make them again
    if [ -n "$BACKUP" ] && [ -f "$BACKUP/unlinked" ] && [ ! -f "$BACKUP/COMMITTED" ]; then relink; fi
    release_lock
    exit "$result"
}
trap 'finish $?' 0
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
mkdir "$LOCK" 2>/dev/null || die "Another run or a stale lock exists: $LOCK (check that no other run is active, then remove it)."
LOCKED=1
echo "$$" > "$LOCK/pid"

mkdir -p "$BACKUPS"
stamp=$(date '+%Y%m%d-%H%M%S')
BACKUP=$BACKUPS/$stamp-$$-$ACTION
mkdir "$BACKUP" || die 'Cannot create a unique backup directory.'
LOG=$BACKUP/operation.log
say "CarPlay Advanced Settings installer $VERSION; $ACTION; backup: $BACKUP"
echo "$ACTION" > "$BACKUP/action"
echo "$hmi_build" > "$BACKUP/hmi-version"

# 1. backups ---------------------------------------------------------------------------------------------------
copy_verified "$START_SCREEN" "$BACKUP/start_screen.sh"
mkdir -p "$ORIG"
if ! exists "$ORIG/start_screen.sh"; then
    if [ "$HOOKS" -eq 0 ]; then
        copy_verified "$START_SCREEN" "$ORIG/start_screen.sh.part"
    else
        grep -v "$MARK" "$START_SCREEN" > "$ORIG/start_screen.sh.part" || die "Cannot write $ORIG/start_screen.sh.part"
        say "Note: $START_SCREEN already had the hook; $ORIG/start_screen.sh is it without the hook line."
    fi
    mv "$ORIG/start_screen.sh.part" "$ORIG/start_screen.sh"
    echo "$START_SCREEN" > "$ORIG/start_screen.sh.path"
fi
for entry in $SHADOWED; do
    name=${entry%%:*}; path=${entry#*:}
    [ -n "$path" ] || continue
    if exists "$ORIG/$name"; then
        if [ -f "$path" ] && [ ! -L "$path" ] && ! diff "$path" "$ORIG/$name" >/dev/null 2>&1; then
            say "Note: $path differs from the kept original $ORIG/$name (firmware updated?); this one is kept in the backup too."
            mkdir -p "$BACKUP/system"
            copy_verified "$path" "$BACKUP/system/$name"
        fi
        continue
    fi
    if [ -L "$path" ]; then
        unshadow "$path" || die "Cannot remove the link $path"
        echo "$path" >> "$BACKUP/unlinked"
        say "Removed the earlier link $path to reach the original."
        [ ! -L "$path" ] || die "$path is still a link after removing it: cannot reach the original."
    fi
    [ -f "$path" ] || { say "$path: absent, nothing to keep."; continue; }
    copy_verified "$path" "$ORIG/$name.part"
    mv "$ORIG/$name.part" "$ORIG/$name"
    echo "$path" > "$ORIG/$name.path"
    say "Original kept: $path -> $ORIG/$name"
done
if [ "$ACTION" = install ] && [ -d "$SETTINGS" ]; then
    mkdir "$BACKUP/settings-copy"
    for f in "$SETTINGS"/*; do
        if [ -f "$f" ] && [ ! -L "$f" ]; then copy_verified "$f" "$BACKUP/settings-copy/${f##*/}"; fi
    done
fi
say 'Backups written and compared.'

# 2. staging -----------------------------------------------------------------------------------------------------
if [ "$ACTION" = install ]; then
    mkdir "$BACKUP/staged-plugin"
    for item in $FILES; do
        copy_verified "$PAYLOAD/$item" "$BACKUP/staged-plugin/$item"
        chmod 644 "$BACKUP/staged-plugin/$item"
    done
    if [ "$SOFTWARE" = scanned ]; then           # the scanned addresses (the plugin uses them for this HMI build only)
        copy_verified "$PAYLOAD/build.tab" "$BACKUP/staged-plugin/build.tab"
        chmod 644 "$BACKUP/staged-plugin/build.tab"
    fi
    if [ "$RGHOOK" != librghook.so ]; then
        copy_verified "$PAYLOAD/$RGHOOK" "$BACKUP/staged-plugin/librghook.so"
    fi
    chmod 755 "$BACKUP/staged-plugin"
    for item in $LIBS; do chmod 755 "$BACKUP/staged-plugin/$item"; done
    if [ "$MODE" = hud ]; then
        # the HUD-only menu under the plugin's names; hud_only: boot.sh skips its bootloop guard (the HMI may never start this menu,
        # on a car not coded for apps, and it patches nothing)
        mv "$BACKUP/staged-plugin/libCarPlayMenu-hud.so" "$BACKUP/staged-plugin/libCarPlayMenu.so" || die 'Cannot stage the HUD-only menu.'
        mv "$BACKUP/staged-plugin/CarPlayMenu-hud.xml" "$BACKUP/staged-plugin/CarPlayMenu.xml" || die 'Cannot stage the HUD-only menu.'
        echo 1 > "$BACKUP/staged-plugin/hud_only" || die 'Cannot stage the HUD-only marker.'
    fi
    say 'Plugin files staged and compared with the originals.'
fi

# HUD-only (the default on iDrive 6): the route-guidance wrapper the menu plugin would build at runtime (the HUD-only menu does not).
# The original mm-ipod is copied in so the wrapper can exec it (the shadow link would otherwise point at the wrapper itself); the
# switch turns boot.sh's relink on. Staged like the plugin so the same transaction / rollback covers it (staged-settings -> $SETTINGS
# on commit). A second HUD-only install on top of one finds mm-ipod as the plugin's link to the wrapper: the wrapper then gets the
# original kept in $ORIG by the first install (compared above).
if [ "$ACTION" = install ] && [ "$MODE" = hud ]; then
    MM_SRC=$MMIPOD
    if [ -L "$MMIPOD" ] && [ -f "$ORIG/mm-ipod" ] && [ ! -L "$ORIG/mm-ipod" ]; then
        MM_SRC=$ORIG/mm-ipod
        say "$MMIPOD is the link of an earlier install: the wrapper gets the kept original ($ORIG/mm-ipod)."
    fi
    [ -f "$MM_SRC" ] && [ ! -L "$MM_SRC" ] || die "$MMIPOD is missing or already a link: cannot copy the original mm-ipod for the wrapper."
    mkdir -p "$BACKUP/staged-settings/bin"
    copy_verified "$MM_SRC" "$BACKUP/staged-settings/bin/mm-ipod"
    chmod 755 "$BACKUP/staged-settings/bin/mm-ipod"
    cat > "$BACKUP/staged-settings/mm-ipod" <<'WRAP'
#!/bin/ksh
# CarPlayMenu: route guidance: mm-ipod with librghook.so (installed by carplay-advanced.sh, HUD-only)
R=/var/opt/hmi/data/rw; [ -d $R ] || R=/fs/sda1/opt/hmi/data/ID5
mkdir -p /var/iap/pps
export LD_PRELOAD=$R/Plugins/CarPlayMenu/librghook.so${LD_PRELOAD:+:$LD_PRELOAD}
exec $R/carplaymenu/bin/mm-ipod "$@"
WRAP
    ksh -n "$BACKUP/staged-settings/mm-ipod" >/dev/null 2>&1 || die 'The generated mm-ipod wrapper does not parse with ksh -n.'
    chmod 755 "$BACKUP/staged-settings/mm-ipod"
    : > "$BACKUP/staged-settings/hud_guidance"
    chmod 755 "$BACKUP/staged-settings"
    say 'Route-guidance wrapper (mm-ipod + librghook) staged.'
fi

NEW_SS=
if [ "$ACTION" = install ] && [ "$HOOKS" -eq 0 ]; then
    NEW_SS=$BACKUP/start_screen.sh.new
    first=
    { IFS= read -r first || :; } < "$START_SCREEN"
    case "$first" in '#!'*) ;; *) die "$START_SCREEN does not start with a shebang line; not changed." ;; esac
    case "$first" in *\\*) die "$START_SCREEN has an unexpected first line; not changed." ;; esac
    # first line stays first, the hook right after it
    { IFS= read -r skipped || :; echo "$first"; echo "$LINE"; cat; } < "$START_SCREEN" > "$NEW_SS"
    grep -v "$MARK" "$NEW_SS" > "$BACKUP/start_screen.sh.check" || :
    diff "$BACKUP/start_screen.sh.check" "$START_SCREEN" >/dev/null 2>&1 || die "The hooked $START_SCREEN would differ by more than the hook line; not changed."
elif [ "$ACTION" = install ] && [ "$HOOK_OLD" -eq 1 ]; then
    NEW_SS=$BACKUP/start_screen.sh.new
    # the older hook line is replaced in place; every other line stays as it is
    while IFS= read -r l || [ -n "$l" ]; do
        case "$l" in *"$MARK"*) echo "$LINE" ;; *) echo "$l" ;; esac
    done < "$START_SCREEN" > "$NEW_SS"
    grep -v "$MARK" "$NEW_SS" > "$BACKUP/start_screen.sh.check" || :
    grep -v "$MARK" "$START_SCREEN" > "$BACKUP/start_screen.sh.was" || :
    diff "$BACKUP/start_screen.sh.check" "$BACKUP/start_screen.sh.was" >/dev/null 2>&1 \
        || die "Replacing the hook line would change more than that line in $START_SCREEN; not changed."
    [ "$(grep -c "$MARK" "$NEW_SS")" = 1 ] || die "The new $START_SCREEN would not have exactly one hook line; not changed."
elif [ "$ACTION" = uninstall ] && [ "$HOOKS" -eq 1 ]; then
    NEW_SS=$BACKUP/start_screen.sh.new
    grep -v "$MARK" "$START_SCREEN" > "$NEW_SS" || :
    before=$(size_of "$START_SCREEN") || die "Cannot read $START_SCREEN"
    after=$(size_of "$NEW_SS") || die "Cannot read $NEW_SS"
    [ "$after" -gt 0 ] && [ "$after" -lt "$before" ] || die "Removing the hook line would leave an unexpected $START_SCREEN; not changed."
    if [ -f "$ORIG/start_screen.sh" ]; then
        if diff "$NEW_SS" "$ORIG/start_screen.sh" >/dev/null 2>&1; then
            say "$START_SCREEN without the hook is identical to the kept original."
        else
            say "Note: $START_SCREEN without the hook differs from $ORIG/start_screen.sh (changed since); only the hook line is removed."
        fi
    fi
fi
if [ -n "$NEW_SS" ]; then
    ksh -n "$NEW_SS" >/dev/null 2>&1 || die "The new $START_SCREEN does not parse with ksh -n; not changed."
    # The new file is placed next to the old one, so the switch is one rename on the same partition.
    SS_TEMP=${START_SCREEN%/*}/.start_screen.sh.carplay-advanced
    if ! { cp "$NEW_SS" "$SS_TEMP"; } 2>/dev/null; then
        rm -f "$SS_TEMP" 2>/dev/null || :
        mp=
        df_out=$(df -P "$START_SCREEN" 2>/dev/null) || df_out=
        line_no=0
        while read -r df_fs df_blocks df_used df_avail df_cap df_mount; do
            line_no=$((line_no + 1))
            if [ "$line_no" -eq 2 ]; then mp=$df_mount; fi   # not "[ ] && ...": a false last command ends QNX sh under set -e
        done <<EOF
$df_out
EOF
        case "$mp" in /*) ;; *) mp=$FS_ROOT ;; esac
        say "Remounting $mp read-write for $START_SCREEN (as the official install guide does)."
        mount -o remount rw "$mp" || die "Cannot remount $mp writable."
        copy_verified "$NEW_SS" "$SS_TEMP"
    fi
    diff "$NEW_SS" "$SS_TEMP" >/dev/null 2>&1 || die "Copy verification failed: $SS_TEMP"
    chmod 755 "$SS_TEMP"
fi

# 3. switch (rollback from here on) ---------------------------------------------------------------------------------------
ACTIVE=1
if exists "$PLUGIN"; then mv "$PLUGIN" "$BACKUP/plugin"; fi
if [ "$ACTION" = install ]; then
    make_dirs "${PLUGIN%/*}"
    mv "$BACKUP/staged-plugin" "$PLUGIN"
    # id6: put the staged route-guidance wrapper in place ($SETTINGS). Any earlier $SETTINGS is moved to the backup (rollback restores it).
    if [ "$MODE" = hud ]; then
        if exists "$SETTINGS"; then mv "$SETTINGS" "$BACKUP/settings"; fi
        make_dirs "${SETTINGS%/*}"
        mv "$BACKUP/staged-settings" "$SETTINGS"
    fi
elif [ "$KEEP_CONFIG" -eq 0 ] && exists "$SETTINGS"; then
    mv "$SETTINGS" "$BACKUP/settings"
fi
if [ -n "$NEW_SS" ]; then
    mv -f "$SS_TEMP" "$START_SCREEN"
    SS_TEMP=
    diff "$NEW_SS" "$START_SCREEN" >/dev/null 2>&1 || die "$START_SCREEN verification failed after the switch."
fi
if [ "$ACTION" = uninstall ]; then
    for entry in $SHADOWED; do
        path=${entry#*:}
        [ -n "$path" ] || continue
        if [ -L "$path" ]; then
            unshadow "$path"
            echo "$path" >> "$BACKUP/unlinked"
            say "Link removed: $path (the original is used from the next start of its process)"
        fi
    done
fi
flush_if_available
echo 'File transaction completed.' > "$BACKUP/COMMITTED"
ACTIVE=0

# 4. after the commit ---------------------------------------------------------------------------------------------
if [ "$ACTION" = install ]; then
    relink
    say 'Links for the switched-on options made with the new boot.sh.'
    { make_dirs "$SETTINGS" && echo "$CLUSTER_KIND" > "$SETTINGS/cluster"; } \
        || say "Note: $SETTINGS/cluster not written: the menu plugin draws on MOST (as with most)."

fi
say "SUCCESS: $ACTION completed. Previous files: $BACKUP ; system originals: $ORIG"
if [ "$ACTION" = install ] && [ "$MODE" = hud ]; then
    say 'Turn-by-turn HUD route guidance installed. It loads into mm-ipod, which is started per CarPlay session: reconnect the phone'
    say '(or restart the head unit) to apply. The HUD-only menu (directions, HUD effects, colour) appears in Media/Radio after a restart'
    say 'where the car is coded for apps (A4A). Fullscreen, knob tilts and the touchpad are native on iDrive 6.'
elif [ "$ACTION" = install ]; then
    say 'The new plugin is loaded when the HMI starts: restart the head unit, then Media/Radio -> CarPlay Advanced Settings.'
else
    say 'Running processes keep the plugin until the head unit restarts.'
fi

if [ "$RESTART" -eq 1 ]; then
    restart_unit
elif [ "$MODE" = hud ]; then
    say 'Reconnect CarPlay for the directions (no reboot needed for them); restart the head unit for the HUD-only menu (--restart-hmi).'
else
    say 'Restart the head unit to apply (or run again with --restart-hmi).'
fi

#!/bin/sh
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Installer for the "CarPlay Advanced Settings" HMI plugin (plugin/) on the NBT Evo, iDrive 5/6, HMI 006_017_006 or
# ID5_1703-490-1650ZD Build 139 (the plugin picks its addresses by build; Full-screen CarPlay is 006_017_006 only).
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

VERSION=1.0.0
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
PROBE_HOST=/bin/cat                                   # runs libcpprobe.so as LD_PRELOAD: its constructor prints one line and exits
# The file set depends on the HMI generation (KIND, set by hash/signature of HmiMain3D; see detect_kind / supported software):
#   id5  the full plugin: menu, HMI patches, fullscreen, knob/touchpad, HUD route guidance.
#   id6  turn-by-turn HUD only. On iDrive 6 the HMI does fullscreen, knob tilts and the touchpad itself (variant 0x9c71,
#        coded on the car), so only the route-guidance hook (librghook in mm-ipod) is added. No menu plugin, no HMI patch,
#        no reboot: mm-ipod is re-spawned per CarPlay session, so it takes effect on the next phone connection.
# KIND is the HMI generation (id5 / id6), found by the computer from HmiMain3D. MODE is what to install and is independent of it:
#   full  the whole plugin (menu, HMI patches, fullscreen, knob/touchpad, HUD). On id6 the menu needs the car coded for apps and the
#         HMI patches apply only to builds the plugin knows; the rest comes from the HMI itself (variant 0x9c71).
#   hud   turn-by-turn HUD route guidance only (librghook in mm-ipod) -- no menu, no HMI patch, no reboot.
# The default follows the generation (id5 -> full, id6 -> hud); --full / --hud choose the other. Both work on both generations.
KIND=id5
MODE=
MODE_REQ=
RGHOOKS='librghook.so librghook-kombi.so librghook-most.so librghook-both.so'   # one goes in as librghook.so (see "the cluster")
FILES_FULL="CarPlayMenu.manifest CarPlayMenu.xml CarPlayMenu_ALL_Texts.zip libCarPlayMenu.so $RGHOOKS libcppinch.so libcpreboot.so libcpprobe.so boot.sh restart.sh"
LIBS_FULL="libCarPlayMenu.so $RGHOOKS libcppinch.so libcpreboot.so libcpprobe.so"
# HUD-only: the route guidance, and the HUD-only menu (libCarPlayMenu-hud.so / CarPlayMenu-hud.xml, installed as libCarPlayMenu.so /
# CarPlayMenu.xml; the HMI shows it where the car is coded for apps). The mm-ipod wrapper is built on the unit.
FILES_HUD="$RGHOOKS libcpprobe.so boot.sh CarPlayMenu.manifest CarPlayMenu-hud.xml CarPlayMenu_ALL_Texts.zip libCarPlayMenu-hud.so"
LIBS_HUD="$RGHOOKS libcpprobe.so libCarPlayMenu-hud.so"
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
ID5_1703-490-1650ZD_Build_139    HmiMain3D           81343a9349384b790f40510bc6ca6b89aa5efb6d3253fad0e590f296b5e348fb
ID5_1703-490-1650ZD_Build_139    libconn_carplay.so  062a20f49fc4f4bab1a7e3b9a5910d12ba8e6b70895a434291c20d83ee0a5f85
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
                *) match=0 ;;
            esac
        done <<EOF
$table
EOF
        if [ "$match" -eq 1 ]; then echo "$name"; return 0; fi
    done
}
is_software_name() {
    supported_table | grep "^$1 " >/dev/null 2>&1
}

ACTION=
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

# Reboots hu-omap (exit 3 when it cannot). HMI ID5_1703-490 has no OnOffDSICommander (and the MINI ignores holding the volume knob):
# then the plugin's libcpreboot.so (sync + sysmgr_reboot, run as LD_PRELOAD of /bin/cat) does it. Stopping launcher packages is no
# restart (restart.sh).
restart_unit() {
    REBOOT_LIB=
    for f in "$PLUGIN/libcpreboot.so" "$BACKUP/plugin/libcpreboot.so"; do if [ -f "$f" ]; then REBOOT_LIB=$f; break; fi; done
    if command -v OnOffDSICommander >/dev/null 2>&1; then how='OnOffDSICommander appreset'
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
    if [ "$how" = 'OnOffDSICommander appreset' ]; then
        OnOffDSICommander appreset || { say 'The restart failed. Reboot manually.'; exit 3; }
    else
        LD_PRELOAD=$REBOOT_LIB /bin/cat < /dev/null
        say 'The reboot did not happen (libcpreboot.so returned). Reboot manually.'; exit 3
    fi
}

usage() {
    cat <<'EOF'
CarPlay Advanced Settings installer 1.0.0 (NBT Evo iDrive 5 / iDrive 6)
Usage: sh carplay-advanced.sh install|uninstall|status|restart [options]
  --host user@address   From a computer: the head unit to connect to (default root@169.254.199.99)
  --full                Install the full plugin (menu, HMI patches, fullscreen, knob/touchpad, HUD). Default on iDrive 5.
  --hud                 Install turn-by-turn HUD route guidance only (no menu, no HMI patch, no reboot). Default on iDrive 6.
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
        install|uninstall|status|restart)
            [ -z "$ACTION" ] || die 'Specify exactly one action.'
            ACTION=$1 ;;
        --host)
            [ "$#" -ge 2 ] || die '--host needs user@address.'
            SSH_HOST=$2; HOST_SET=1; shift ;;
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
    fetch_tree() {    # fetch_tree <folder on hu-omap> <local folder>: every file, compared by size with the original
        walk='walk() { for f in "$1"/*; do if [ -d "$f" ] && [ ! -L "$f" ]; then echo "D $f"; walk "$f"; elif [ -f "$f" ]; then echo "F $f"; fi; done; }; walk '
        listing=$(omap "/bin/ksh -c '$walk$1'" < /dev/null) || return 1
        mkdir -p "$2" || return 1
        while read -r kind path; do
            [ -n "$path" ] || continue
            local_path=$2/${path#"$1"/}
            if [ "$kind" = D ]; then mkdir -p "$local_path" || return 1; continue; fi
            omap "/bin/cat $path" < /dev/null > "$local_path" || return 1
            remote_size=$(omap "/bin/ksh -c 'wc -c < $path'" < /dev/null | tr -d ' \r') || return 1
            [ "$(wc -c < "$local_path" | tr -d ' ')" = "$remote_size" ] || { say "Size of the copy of $path differs."; return 1; }
        done <<EOF
$listing
EOF
    }

    # A reboot never answers: the head unit goes down with the connection open, and ssh would wait for its timeout. So the restart runs
    # in the background, its lines are shown as they come, and once the head unit says it reboots the installer stops waiting.
    reboot_said() { grep 'rebooting the head unit' "$1" >/dev/null 2>&1 || grep 'Restarting the head unit (OnOffDSICommander' "$1" >/dev/null 2>&1; }
    remote_reboot() {    # remote_reboot <command on hu-omap> <stdin file>: 0 once the reboot is under way
        rb_out=$WORK/reboot.out; : > "$rb_out"
        omap "$1" < "$2" > "$rb_out" 2>&1 &
        rb_pid=$!; rb_shown=0; rb_t=0
        while :; do
            rb_n=$(wc -l < "$rb_out" | tr -d ' ')
            if [ "$rb_n" -gt "$rb_shown" ]; then sed -n "$((rb_shown + 1)),${rb_n}p" "$rb_out" | show_remote; rb_shown=$rb_n; fi
            if reboot_said "$rb_out"; then sleep 2; break; fi
            kill -0 "$rb_pid" 2>/dev/null || break
            rb_t=$((rb_t + 1)); [ "$rb_t" -lt 40 ] || break
            sleep 1
        done
        if reboot_said "$rb_out"; then
            kill "$rb_pid" 2>/dev/null || :
            ok 'The head unit is restarting; it is back in about a minute.'
            return 0
        fi
        if kill -0 "$rb_pid" 2>/dev/null; then kill "$rb_pid" 2>/dev/null || :; die 'The head unit did not confirm the restart in 40 s.'; fi
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
    remote_is_id6() {    # remote_is_id6 <path on hu-omap>: true if it has the iDrive 6 native CarPlay tilt handlers
        omap "/bin/cat $1" < /dev/null | od -An -v -tx1 | tr -dc 0-9a-f | grep -qE "$ID6_SIG"
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
        say 'The plugin patches HmiMain3D at fixed addresses, so every software needs its own entry.'
        say 'To ask for support, open an issue (the link fills in the lines above):'
        title="Support for HMI ${hmi_version:-unknown}"
        body="HMI build: ${hmi_version:-unknown}
HmiMain3D sha256: $hash_hmi
libconn_carplay.so sha256: $hash_conn
Installer: $VERSION"
        say "  $ISSUES_URL?title=$(urlencode "$title")&body=$(urlencode "$body")"
    }
    if [ "$ACTION" = install ] || [ "$ACTION" = status ]; then
        step 'Checking the head unit software (reads HmiMain3D, about 60 MB)'
        hmi_path=$HMI_BIN
        omap "/bin/ksh -c '[ -f $HMI_BIN ]'" < /dev/null || hmi_path=$HMI_BIN_PLAIN
        hmi_version=$(omap "/bin/cat $HMI_VERSION_FILE" < /dev/null 2>/dev/null | tr -cd '[:print:]') || hmi_version=
        if remote_is_id6 "$hmi_path"; then KIND=id6; else KIND=id5; fi
        MODE=${MODE_REQ:-$(default_mode)}; apply_mode
        gen=$([ "$KIND" = id6 ] && echo 'iDrive 6' || echo 'iDrive 5')
        ok "Head unit: $gen, ${hmi_version:-unknown}"
        ok "Install mode: $MODE${MODE_REQ:+ (chosen)}${MODE_REQ:- (recommended for this HMI; --full / --hud to change)}"
        # The full plugin patches HmiMain3D at fixed addresses, so on iDrive 5 the build must be known (by hash). On iDrive 6 the plugin
        # does not patch (it is an unknown build to it; the native features come from the HMI), so no hash is needed. HUD never patches.
        if [ "$MODE" = full ] && [ "$KIND" = id5 ]; then

            hash_hmi=$(remote_sha256 "$hmi_path") || die "Cannot read $hmi_path on hu-omap."
            hash_conn=$(remote_sha256 "$CONN_LIB") || die "Cannot read $CONN_LIB on hu-omap."
            SOFTWARE=$(supported_as "$hash_hmi" "$hash_conn")
            if [ -n "$SOFTWARE" ]; then
                ok "Supported software: $SOFTWARE (HmiMain3D and libconn_carplay.so match by hash)"
            else
                report_unsupported
                say 'The full plugin needs a known build. You can still install the turn-by-turn HUD only with --hud (no HMI patch).'
                if [ "$ACTION" = install ]; then say 'Nothing was installed.'; exit 1; fi
            fi
        elif [ "$MODE" = full ] && [ "$KIND" = id6 ]; then
            say 'Note: on iDrive 6 the menu appears only if the car is coded for apps (CarPlay fullscreen variant 0x9c71); the full'
            say "plugin's HMI patches apply only to builds it knows, so here it adds the menu and the HUD and leaves fullscreen/tilts/touchpad to the HMI."
        else
            say 'Turn-by-turn HUD route guidance only: no menu, no HMI patch. It applies on the next CarPlay connection (no reboot).'
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

    # -------- upload, run on hu-omap, fetch the backup
    [ "$ACTION" != install ] || step 'Uploading the plugin'
    omap "/bin/ksh -c 'rm -rf $STAGE; mkdir -p $STAGE/CarPlayMenu'" < /dev/null || die "Cannot create $STAGE on hu-omap."
    send "$0" "$STAGE/carplay-advanced.sh"
    args=$ACTION
    if [ "$ACTION" = install ]; then
        for item in $FILES; do send "$SRC/$item" "$STAGE/CarPlayMenu/$item"; done
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
        *) step "$ACTION on the head unit" ;;
    esac
    if [ "$ACTION" = restart ]; then
        remote_reboot "/bin/sh $STAGE/carplay-advanced.sh restart" /dev/null || exit $?
        exit 0
    fi
    { run_rc=0; omap "/bin/sh $STAGE/carplay-advanced.sh $args" < /dev/null 2>&1 || run_rc=$?; echo "$run_rc" > "$WORK/rc"; } \
        | tee "$WORK/out" | show_remote
    read -r rc < "$WORK/rc" || rc=1
    omap "/bin/ksh -c 'rm -rf $STAGE'" < /dev/null > /dev/null 2>&1 || say "Note: $STAGE is left on hu-omap (removed by the next run)."

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
            row 'Head unit' "$gen, ${hmi_version:-unknown}${SOFTWARE:+ (supported, checked by hash)}"
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
        [ "$backup_ok" -eq 0 ] || row 'Backup' "BACKUP/${backup##*/} (and on the head unit: $backup)"
        [ "$backup_ok" -eq 0 ] || row 'Originals' 'BACKUP/originals (the system files the plugin shadows, kept once)'
        if [ "$MODE" = hud ] && [ "$ACTION" = install ]; then
            printf '\n  It applies on the next CarPlay connection: no restart needed.\n'
        elif [ "$RESTART" -eq 0 ]; then
            if [ "$ACTION" = install ]; then
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
        remote_reboot '/bin/sh -s -- restart' "$0" || exit $?
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

# ---------------------------------------------------------------------------------------------------------------- restart
if [ "$ACTION" = restart ]; then
    if [ "$DRY_RUN" -eq 1 ]; then say 'DRY RUN: the head unit would be restarted.'; exit 0; fi
    restart_unit
fi

# ---------------------------------------------------------------------------------------------------------------- the cluster
# Where CarPlay directions go: the KOMBI's route-guidance service over Ethernet (SOME/IP 0xB006 at 160.48.199.96:30502; G-series, J29)
# or the MOST HUDTerminal (F-series), or both. One of four librghook builds goes in as librghook.so:
#             full (the menu plugin draws on MOST)      HUD-only (no menu plugin)
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
    is_software_name "$SOFTWARE" || die "Unknown software '$SOFTWARE' (see supported_table)."
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
        say '  Drawn on MOST from mm-ipod (no menu plugin): not car-verified yet.'
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

# id6: the route-guidance wrapper the menu plugin would build at runtime (there is no menu plugin on id6). The original mm-ipod is copied
# in so the wrapper can exec it (the shadow link would otherwise point at the wrapper itself); the switch turns boot.sh's relink on.
# Staged like the plugin so the same transaction / rollback covers it (staged-settings -> $SETTINGS on commit).
if [ "$ACTION" = install ] && [ "$MODE" = hud ]; then
    [ -f "$MMIPOD" ] && [ ! -L "$MMIPOD" ] || die "$MMIPOD is missing or already a link: cannot copy the original mm-ipod for the wrapper."
    mkdir -p "$BACKUP/staged-settings/bin"
    copy_verified "$MMIPOD" "$BACKUP/staged-settings/bin/mm-ipod"
    chmod 755 "$BACKUP/staged-settings/bin/mm-ipod"
    cat > "$BACKUP/staged-settings/mm-ipod" <<'WRAP'
#!/bin/ksh
# CarPlayMenu: route guidance: mm-ipod with librghook.so (installed by carplay-advanced.sh for iDrive 6)
R=/var/opt/hmi/data/rw; [ -d $R ] || R=/fs/sda1/opt/hmi/data/ID5
mkdir -p /var/iap/pps
export LD_PRELOAD=$R/Plugins/CarPlayMenu/librghook.so${LD_PRELOAD:+:$LD_PRELOAD}
exec $R/carplaymenu/bin/mm-ipod "$@"
WRAP
    ksh -n "$BACKUP/staged-settings/mm-ipod" >/dev/null 2>&1 || die 'The generated mm-ipod wrapper does not parse with ksh -n.'
    chmod 755 "$BACKUP/staged-settings/mm-ipod"
    : > "$BACKUP/staged-settings/hud_guidance"
    chmod 755 "$BACKUP/staged-settings"
    say 'Route-guidance wrapper (mm-ipod + librghook) staged for iDrive 6.'
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
    mkdir -p "${PLUGIN%/*}"
    mv "$BACKUP/staged-plugin" "$PLUGIN"
    # id6: put the staged route-guidance wrapper in place ($SETTINGS). Any earlier $SETTINGS is moved to the backup (rollback restores it).
    if [ "$MODE" = hud ]; then
        if exists "$SETTINGS"; then mv "$SETTINGS" "$BACKUP/settings"; fi
        mkdir -p "${SETTINGS%/*}"
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
    { mkdir -p "$SETTINGS" && echo "$CLUSTER_KIND" > "$SETTINGS/cluster"; } \
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
    say 'Reconnect CarPlay to apply (no reboot needed), or run again with --restart-hmi to restart the head unit now.'
else
    say 'Restart the head unit to apply (or run again with --restart-hmi).'
fi

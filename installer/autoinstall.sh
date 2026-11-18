#!/bin/sh
# Camera Control installer for SYNC 3.4 (builds 22110 to 26057) with F-Mods. Run by F-Mods from the USB
# stick as SyncMyMod/autoinstall.sh; the stick folder is built by build.sh. A package of its own: it
# shares nothing with the front-tire-guides stick and works with or without it.
#
# Installs:
#   the programs  /fs/images/fmods_apps_data/EnhancedCameraControl/   (bin/camctl, bin/rvcpatch, start.sh, install.conf,
#                 uninstall.sh, hmi_block.sh)
#   the start     FMods Tools 3.0+: /fs/mp/scripts/fmods/enhanced_camera_control.sh (F-Mods' launcher runs it at boot);
#                 older Tools: one line in /fs/mp/scripts/startup_gf.sh
#   the switches  SETTINGS_PAGE=1: four switches added to Ford's Camera Settings page,
#                 /fs/mp/fordhmi/qml/hmisettings/driverassistance/RVCSettings.qml, as one marked block
#                 (hmi_block.sh: Front Tire Guides put a block of their own there; each comes and goes without touching
#                 the other's; refused if the file is anything but Ford's stock one plus such blocks)
#   at run time   /fs/rwdata/camcontrol/ (settings.json, lastview, rvcpatch.state, gate.state, popup.state)
# UNINSTALL=1 in install.conf: plugging the stick in removes all of it. Refuses any other build, and an
# older front-tire-guides install that still has its own camera rule. If a step fails, what was done is undone.

PATH=/fs/rwdata/dev:$PATH
STICK=/fs/usb0/SyncMyMod
DATA=/fs/images/fmods_apps_data/EnhancedCameraControl
STATE=/fs/rwdata/camcontrol
STARTUP=/fs/mp/scripts/startup_gf.sh
LAUNCHER=/fs/mp/scripts/fmods/enhanced_camera_control.sh
SETTINGS_QML=/fs/mp/fordhmi/qml/hmisettings/driverassistance/RVCSettings.qml
SETTINGS_MD5=4fa73e6c92a6a9818e08e2e5488d30a0   # Ford's stock RVCSettings.qml (the same in every supported build)
MARK=enhanced-camera-control
# Where it is started from, earliest first (the same way Front Tire Guides is):
#   early:    our line in Ford's startup script straight after the line that launches SYNC's services (ANCHOR), so the
#             rule is in force when the camera can first be opened, not a minute later. Only when that line is found
#             exactly once.
#   launcher: F-Mods' launcher (Tools 3.0+), the last thing in that script.
#   line:     our line at the end of the script (older Tools).
ANCHOR='^on -p 20  *\$GF_ROOT/apps/SS_LaunchProcesses .*&$'
# Ford's startup script rewritten safely: the new text (a file) must differ from the old by exactly $2 lines and keep
# Ford's launch line; it is written beside the old one and swapped in with one rename, so a power cut leaves either
# the old script or the new one whole, never half of one (SYNC's services and the USB installer start from it).
startup_swap() {   # startup_swap <new text> <lines added: 1, -1...>
    _o=$(wc -l < "$STARTUP") && _n=$(wc -l < "$1") && [ "$((_o + $2))" = "$((_n))" ] || return 1
    [ "$(grep -c "$ANCHOR" "$1")" = "$(grep -c "$ANCHOR" "$STARTUP")" ] || return 1
    cp -p "$STARTUP" "$STARTUP.new_by_mod" && cat "$1" > "$STARTUP.new_by_mod" && sync &&
        [ "$(wc -c < "$1")" = "$(wc -c < "$STARTUP.new_by_mod")" ] && mv -f "$STARTUP.new_by_mod" "$STARTUP" && sync
}
startup_line_out() {   # our line out of Ford's startup script, wherever it is
    [ -f "$STARTUP" ] && grep -q "$MARK" "$STARTUP" || return 0
    grep -v "$MARK" "$STARTUP" > /tmp/enhanced_camera_control_startup && startup_swap /tmp/enhanced_camera_control_startup "-$(grep -c "$MARK" "$STARTUP")"
}
BLOCK=enhanced-camera-control
DISPLAY_FILE=/fs/tmpfs/status
VERSION=$(cat "$STICK/files/version.txt" 2>/dev/null)

. "$STICK/install.conf"
. "$STICK/files/hmi_block.sh"

say() { echo "$1" > "$DISPLAY_FILE" 2>/dev/null; sleep "${2:-1}" 2>/dev/null; }
progress() { echo "PROGRESS $1" > "$DISPLAY_FILE" 2>/dev/null; }
finish() {   # wait for the stick to be pulled, then F-Mods reboots
    sync
    while [ -e /fs/usb0 ]; do sleep 1; done
    say "REBOOT" 3
    exit "${1:-0}"
}
stop_ours() {
    for pid in $( (pidin arg 2>/dev/null || ps -eo pid=,args= 2>/dev/null) |
            awk '/EnhancedCameraControl\/start\.sh|EnhancedCameraControl\/bin\/(camctl|rvcpatch)/ && !/awk/ {print $1}'); do
        kill -9 "$pid" 2>/dev/null
    done
    slay -f camctl > /dev/null 2>&1 || pkill -f EnhancedCameraControl/bin/camctl > /dev/null 2>&1
    rmdir /fs/tmpfs/camcontrol_started 2>/dev/null
    sleep 1
    # SYNC's speed limits back as they were, and the speed-limit program's marks afresh for the new install
    [ -x "$DATA/bin/rvcpatch" ] && "$DATA/bin/rvcpatch" -u > /dev/null 2>&1
    [ -x "$DATA/bin/rvcpatch" ] && [ -r "$DATA/popup.places" ] &&   # ...and the popup's limit (HMI_AL)
        "$DATA/bin/rvcpatch" -u -name HMI_AL -places "$DATA/popup.places" > /dev/null 2>&1
    [ -x "$DATA/bin/camctl" ] && "$DATA/bin/camctl" -r > /dev/null 2>&1
    rm -f "$STATE/rvcpatch.state" "$STATE/gate.state" "$STATE/popup.state"
}
refuse() {   # before anything was changed
    say "ERROR $1"
    finish 1
}
undo() {     # after changes: take everything out again, then stop
    say "ERROR $1 Undoing the install..." 2
    hmi_block_out "$SETTINGS_QML" "$BLOCK" "$SETTINGS_MD5"
    front_menu_out   # the Front Camera entry too, unless Front Tire Guides' row is still on the page
    startup_line_out
    rm -f "$LAUNCHER"
    rm -rf "$DATA"
    remount_ro.sh
    sync
    say "ERROR $1 Enhanced Camera Control is not installed." 2
    finish 1
}

[ -x "$(command -v instutility)" ] && { instutility & sleep 2; }
say "DEV Enhanced Camera Control $VERSION" 2
if [ "${UNINSTALL:-0}" = 1 ]; then
    say "Removing Enhanced Camera Control from SYNC..." 1
    /bin/sh "$STICK/files/uninstall.sh" > /dev/null 2>&1   # (/bin/sh by its path: "sh" need not be on the daemon's PATH)
    if [ -e "$DATA" ] || [ -e "$LAUNCHER" ] || grep -q "$MARK" "$STARTUP" 2>/dev/null ||
        hmi_block_has "$SETTINGS_QML" "$BLOCK"; then
        say "ERROR Enhanced Camera Control could not be removed completely." 2
        finish 1
    fi
    say "Enhanced Camera Control removed. Remove the USB stick to restart SYNC." 2
    finish 0
fi

# -- checks (nothing changed yet) -------------------------------------------------------------------------------
progress 10
# the SYNC 3.4 builds this is made for: the parts of Ford's programs and screen files it touches are the same in all of
# them (only HMI_AL's popup moved: installer/popup/<build>.places)
BUILD=""
for b in 22110 22200 22251 23088 23188 26057; do grep -q "_3.4.${b}_" /fs/mp/Version.inf 2>/dev/null && BUILD=$b; done
[ -n "$BUILD" ] || refuse "Made for SYNC 3.4 builds 22110, 22200, 22251, 23088, 23188 and 26057 only."
LINE=$(grep FMODS_TOOLS /fs/rwdata/dev/mods_tools.txt 2>/dev/null)
[ -n "$LINE" ] || refuse "FMods Tools not found."
TOOLS=$(echo "$LINE" | awk -F_ '{print $NF}')
awk 'BEGIN { exit !('"$TOOLS"' >= 2.8) }' 2>/dev/null || refuse "FMods Tools 2.8 or newer needed."
[ -f "$STICK/files/bin/camctl" ] && [ -f "$STICK/files/bin/rvcpatch" ] ||
    refuse "The programs are missing from the stick."
if [ "${SETTINGS_PAGE:-1}" = 1 ] || hmi_block_has "$SETTINGS_QML" "$BLOCK"; then
    hmi_block_stock "$SETTINGS_QML" "$SETTINGS_MD5" ||
        refuse "The Camera Settings page isn't the stock file (another mod?). Set SETTINGS_PAGE=0 to install without the switches."
fi
# FMods Tools 3.0+ start every script in /fs/mp/scripts/fmods at boot: use that, and leave Ford's startup_gf.sh alone
USE_LAUNCHER=0
awk 'BEGIN { exit !('"$TOOLS"' >= 3.0) }' && grep -q launch_fmodslauncher.sh "$STARTUP" 2>/dev/null && USE_LAUNCHER=1
EARLY=0
if [ "${EARLY_START:-0}" = 1 ]; then
    if [ "$(grep -c "$ANCHOR" "$STARTUP" 2>/dev/null)" = 1 ]; then EARLY=1; USE_LAUNCHER=0; fi
fi

# -- install ----------------------------------------------------------------------------------------------------
progress 30
say "Setting the file system read-write..." 1
remount_rw.sh || refuse "Couldn't make the file system writable."
[ -d "$DATA" ] && { say "Stopping what is installed..." 1; stop_ours; }

progress 45
say "Copying the programs..." 1
rm -rf "$DATA"
mkdir -p "$DATA" && cp -R "$STICK/files/." "$DATA/" && cp "$STICK/install.conf" "$DATA/install.conf" &&
    cp "$DATA/popup/$BUILD.places" "$DATA/popup.places" || undo "Copying the programs failed."
chmod +x "$DATA/bin/camctl" "$DATA/bin/rvcpatch" "$DATA/start.sh" "$DATA/uninstall.sh"
# the switches' file: made on a first install from install.conf; what was chosen on the screen since is kept
mkdir -p "$STATE"
if [ ! -f "$STATE/settings.json" ]; then
    tf() { [ "$1" = 1 ] && echo true || echo false; }
    printf '{"control":%s,"speed":%s,"auto":%s,"last":%s}' "$(tf "${CONTROL:-1}")" "$(tf "${SPEED:-1}")" \
        "$(tf "${AUTO_OPEN:-0}")" "$(tf "${LAST_VIEW:-1}")" > "$STATE/settings.json" || undo "Writing the settings failed."
fi

progress 60
say "Starting it with SYNC..." 1
OURLINE="[ -x $DATA/start.sh ] && $DATA/start.sh &  # $MARK"
if [ "$EARLY" = 1 ]; then
    # one line, after Ford's launch line; an earlier install's line (wherever it was) goes first
    rm -f "$LAUNCHER"
    startup_line_out || undo "Updating startup_gf.sh failed."
    at=$(grep -n "$ANCHOR" "$STARTUP" | awk -F: '{ print $1 }')
    awk -v at="$at" -v l="$OURLINE" '{ print } NR == at { print l }' "$STARTUP" > /tmp/enhanced_camera_control_startup &&
        [ "$(grep -c "$MARK" /tmp/enhanced_camera_control_startup)" = 1 ] && startup_swap /tmp/enhanced_camera_control_startup 1 || undo "Updating startup_gf.sh failed."
elif [ "$USE_LAUNCHER" = 1 ]; then
    startup_line_out || undo "Updating startup_gf.sh failed."   # an earlier install's line: keep one way of starting
    mkdir -p "${LAUNCHER%/*}" &&
        printf '#!/bin/sh\n# %s: starts Enhanced Camera Control with SYNC (run by the F-Mods launcher)\n[ -x %s/start.sh ] && %s/start.sh\n' \
            "$MARK" "$DATA" "$DATA" > "$LAUNCHER" && chmod +x "$LAUNCHER" || undo "Adding Enhanced Camera Control to the F-Mods launcher failed."
else
    rm -f "$LAUNCHER"
    if ! tail -1 "$STARTUP" | grep -q "$MARK"; then
        startup_line_out || undo "Updating startup_gf.sh failed."
        { cat "$STARTUP"; printf '%s\n' "$OURLINE"; } > /tmp/enhanced_camera_control_startup && startup_swap /tmp/enhanced_camera_control_startup 1 ||
            undo "Updating startup_gf.sh failed."
    fi
fi

progress 75
if [ "${SETTINGS_PAGE:-1}" = 1 ]; then   # (an earlier install's block is replaced by this stick's)
    say "Adding the switches to Camera Settings..." 1
    hmi_block_in "$SETTINGS_QML" "$STICK/files/switches.qml.in" "$BLOCK" "$SETTINGS_MD5" first ||
        undo "Adding the switches to RVCSettings.qml failed."
    # the switches' own place: "Front Camera" in Driver Assistance, shared with Front Tire Guides (there already when
    # they are installed). If the menu isn't Ford's stock file the switches stay under Rear View Camera.
    if front_menu_in "$STICK/files"; then say "Adding Front Camera to Driver Assistance..." 1; fi
else
    hmi_block_out "$SETTINGS_QML" "$BLOCK" "$SETTINGS_MD5" || undo "Taking the switches out of RVCSettings.qml failed."
    front_menu_out   # (stays while Front Tire Guides' row is on the page)
fi

progress 90
say "Setting the file system read-only..." 1
remount_ro.sh
sync
# SYNC's update overlay images are back on top now: each change must still be what SYNC sees
hidden=""
[ -x "$DATA/bin/camctl" ] || hidden="$hidden the programs,"
if [ "$USE_LAUNCHER" = 1 ]; then
    [ -x "$LAUNCHER" ] || hidden="$hidden the launcher script,"
else
    grep -q "$MARK" "$STARTUP" || hidden="$hidden startup_gf.sh,"
fi
[ "${SETTINGS_PAGE:-1}" = 1 ] && ! hmi_block_has "$SETTINGS_QML" "$BLOCK" && hidden="$hidden RVCSettings.qml,"
if [ -n "$hidden" ]; then
    remount_rw.sh
    undo "SYNC shows another copy of${hidden%,} (an update overlay image covers it)."
fi
progress 100
say "Enhanced Camera Control $VERSION installed. Remove the USB stick to restart SYNC." 2
finish 0

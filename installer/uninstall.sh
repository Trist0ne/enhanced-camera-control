#!/bin/sh
# Camera Control uninstaller: takes out exactly what autoinstall.sh added and takes its own rows out of Ford's Camera Settings page.
# SYNC's speed limits are put back at once (and a SYNC restart is stock anyway). Safe to run twice.
PATH=/fs/rwdata/dev:$PATH
DATA=/fs/images/fmods_apps_data/EnhancedCameraControl
STATE=/fs/rwdata/camcontrol
STARTUP=/fs/mp/scripts/startup_gf.sh
LAUNCHER=/fs/mp/scripts/fmods/enhanced_camera_control.sh
SETTINGS_QML=/fs/mp/fordhmi/qml/hmisettings/driverassistance/RVCSettings.qml
MARK=enhanced-camera-control
SETTINGS_MD5=4fa73e6c92a6a9818e08e2e5488d30a0   # Ford's stock RVCSettings.qml (the same in every supported build)
here=$(dirname "$0")
. "$here/hmi_block.sh"

command -v remount_rw.sh > /dev/null && remount_rw.sh
# the restart loops first (a shell script: found by its arguments), then the programs
for pid in $( (pidin arg 2>/dev/null || ps -eo pid=,args= 2>/dev/null) |
        awk '/EnhancedCameraControl\/start\.sh|EnhancedCameraControl\/bin\/(camctl|rvcpatch)/ && !/awk/ {print $1}'); do
    kill -9 "$pid" 2>/dev/null
done
slay -f camctl > /dev/null 2>&1 || pkill -f EnhancedCameraControl/bin/camctl > /dev/null 2>&1
rmdir /fs/tmpfs/camcontrol_started 2>/dev/null
[ -x "$DATA/bin/rvcpatch" ] && "$DATA/bin/rvcpatch" -u > /dev/null 2>&1
[ -x "$DATA/bin/rvcpatch" ] && [ -r "$DATA/popup.places" ] &&   # ...and the popup's limit (HMI_AL)
    "$DATA/bin/rvcpatch" -u -name HMI_AL -places "$DATA/popup.places" > /dev/null 2>&1
# ...and the place where it refuses front views nobody asked for (camctl was stopped where it stood)
[ -x "$DATA/bin/camctl" ] && "$DATA/bin/camctl" -r > /dev/null 2>&1
hmi_block_out "$SETTINGS_QML" enhanced-camera-control "$SETTINGS_MD5"   # only our block: Front Tire Guides' row, if there, stays
front_menu_out   # the Front Camera entry of Driver Assistance goes with the last of our rows: Front Tire Guides' keep it
# our line out of Ford's startup script: the new text beside the old, checked (only our lines gone), swapped in with
# one rename so a power cut leaves one script or the other whole
if [ -f "$STARTUP" ] && grep -q "$MARK" "$STARTUP"; then
    grep -v "$MARK" "$STARTUP" > /tmp/enhanced_camera_control_startup &&
        [ "$(($(wc -l < /tmp/enhanced_camera_control_startup) + $(grep -c "$MARK" "$STARTUP")))" = "$(($(wc -l < "$STARTUP")))" ] &&
        cp -p "$STARTUP" "$STARTUP.new_by_mod" && cat /tmp/enhanced_camera_control_startup > "$STARTUP.new_by_mod" && sync &&
        [ "$(wc -c < /tmp/enhanced_camera_control_startup)" = "$(wc -c < "$STARTUP.new_by_mod")" ] && mv -f "$STARTUP.new_by_mod" "$STARTUP" && sync
    rm -f "$STARTUP.new_by_mod"
fi
rm -f "$LAUNCHER"
rm -rf "$DATA" "$STATE"
command -v remount_ro.sh > /dev/null && remount_ro.sh
sync
exit 0

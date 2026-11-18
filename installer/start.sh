#!/bin/sh
# Starts Camera Control with SYNC: run once at boot by the F-Mods launcher, or from
# startup_gf.sh in the background. camctl is restarted if it ever ends, with a pause so a fault can never spin.
# The speed-limit program (rvcpatch) follows the "Extended Speed Range" switch, and the package's own switch above
# it ("Enhanced Camera Control": off = stock in every respect): looked at every 5 s.
T=/fs/images/fmods_apps_data/EnhancedCameraControl
D=/fs/rwdata/camcontrol
. "$T/install.conf"
mkdir -p "$D"
# once a start: a second run (the launcher and a startup line both there, say) would double every loop.
# /fs/tmpfs is empty after every SYNC restart; the installer and the uninstaller take the mark away with the programs.
mkdir /fs/tmpfs/camcontrol_started 2>/dev/null || exit 0
# The rule's program at QNX's ordinary priority (10, as Ford's own background jobs): it is started with SYNC's services
# and has to act within a moment of a press. The speed-limit program below them (8).
P=8
ARGS="-d $D"
# SYNC's camera program is stock again after a restart: "lifted" from the last start must not be believed.
# Only the speed-limit program's own safety marks are kept (an unfinished search, refused).
case "$(cat "$D/rvcpatch.state" 2>/dev/null)" in
    searching*|refused*) ;;
    *) rm -f "$D/rvcpatch.state" ;;
esac
case "$(cat "$D/gate.state" 2>/dev/null)" in   # the same for camctl's gate (SYNC refusing unasked front views)
    searching*|refused*) ;;
    *) rm -f "$D/gate.state" ;;
esac
case "$(cat "$D/popup.state" 2>/dev/null)" in   # ...and for the popup's limit (HMI_AL, popup.places)
    searching*|refused*) ;;
    *) rm -f "$D/popup.state" ;;
esac
W=20   # the speed-limit program: let SYNC's camera program settle first (this script returns at once)
(
    # no wait for the rule's program (it was 20 s): it waits for SYNC's services itself and asks again for the truck's
    # messages until they come, so the rule is in force as soon as the camera can be opened
    while :; do
        # shellcheck disable=SC2086
        on -p 10 "$T/bin/camctl" $ARGS > /dev/null 2>&1
        sleep 10
    done
) &
speed_on() {   # the speed switch, under the package's own switch: both on. No file or no key (the page is writing
    # it) = as it was, at first the install's values. Read by the shell itself: no program is started for it, every
    # 5 s for as long as SYNC is up.
    _s=""
    [ -r "$D/settings.json" ] && read -r _s < "$D/settings.json"
    case "$_s" in
        *'"speed":false'*|*'"speed": false'*) want_speed=0 ;;
        *'"speed":true'*|*'"speed": true'*) want_speed=1 ;;
    esac
    case "$_s" in
        *'"control":false'*|*'"control": false'*) want_control=0 ;;
        *'"control":true'*|*'"control": true'*) want_control=1 ;;
    esac
    [ "$want_speed" = 1 ] && [ "$want_control" = 1 ]
}
(
    sleep "$W"
    pid=""
    pid2=""
    want_speed=${SPEED:-1}
    want_control=${CONTROL:-1}
    lifted_by_us=0
    while :; do
        if speed_on; then
            if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
                on -p $P "$T/bin/rvcpatch" -loop -state "$D/rvcpatch.state" > /dev/null 2>&1 &
                pid=$!
            fi
            # Ford's safety popup for a press: only above the camera module's limit, where nothing can be shown
            if [ -r "$T/popup.places" ] && { [ -z "$pid2" ] || ! kill -0 "$pid2" 2>/dev/null; }; then
                on -p $P "$T/bin/rvcpatch" -loop -name HMI_AL -places "$T/popup.places" -state "$D/popup.state" > /dev/null 2>&1 &
                pid2=$!
            fi
            lifted_by_us=1
        elif [ "$lifted_by_us" = 1 ]; then
            # stopped wherever it is, then SYNC's bytes put back whatever its state file says (it may have been in
            # the middle of a search or a write); its unfinished-search mark goes too, this stop being ours
            if [ -n "$pid" ]; then kill "$pid" 2>/dev/null; sleep 1; kill -9 "$pid" 2>/dev/null; pid=""; fi
            case "$(cat "$D/rvcpatch.state" 2>/dev/null)" in searching*) rm -f "$D/rvcpatch.state" ;; esac
            "$T/bin/rvcpatch" -u -state "$D/rvcpatch.state" > /dev/null 2>&1
            if [ -n "$pid2" ]; then kill "$pid2" 2>/dev/null; sleep 1; kill -9 "$pid2" 2>/dev/null; pid2=""; fi
            case "$(cat "$D/popup.state" 2>/dev/null)" in searching*) rm -f "$D/popup.state" ;; esac
            [ -r "$T/popup.places" ] && "$T/bin/rvcpatch" -u -name HMI_AL -places "$T/popup.places" -state "$D/popup.state" > /dev/null 2>&1
            lifted_by_us=0
        fi
        sleep 5
    done
) &

#!/bin/sh
# Safe boot supervisor for the C1Max replacement desktop.
# It is a one-shot init service: a failed custom desktop always falls back to
# the vendor smartUI, and the vendor service definition remains intact.
set -u
# init services may start without an interactive shell PATH.
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
BASE=/storage/apps
STATE="$BASE/data/launcher"
LOG="$STATE/desktop-boot.log"
HEARTBEAT="$STATE/desktop.heartbeat"
mkdir -p "$STATE"
exec >>"$LOG" 2>&1
log() { printf '[desktop-boot] %s\n' "$*"; }

BOOT_ID=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || echo unknown)
BOOT_MARKER="$STATE/desktop-started-$BOOT_ID"
if [ -e "$BOOT_MARKER" ]; then
    log "Duplicate c1desktop trigger for boot $BOOT_ID; ignoring"
    exit 0
fi
: > "$BOOT_MARKER"

if [ -f "$STATE/desktop.disabled" ]; then
    log 'Replacement disabled; starting stock UI'
    setprop ctl.start smartUI || true
    exit 0
fi

if [ ! -x "$BASE/current/launcher/run.sh" ] ||
   [ ! -x "$BASE/current/launcher/desktop-service.sh" ] ||
   [ ! -x "$BASE/current/launcher/c1max-launcher" ] ||
   [ ! -x "$BASE/current/shared/c1max-volume" ]; then
    log 'Runtime is incomplete; starting stock UI'
    setprop ctl.start smartUI || true
    exit 0
fi

# userboot starts smartUI and c1desktop in the same trigger. Wait until the
# stock service reaches a settled state; run.sh intentionally refuses the
# transient "starting" state to prevent two framebuffer owners racing.
state=''
n=0
while [ "$n" -lt 80 ]; do
    state=$(getprop init.svc.smartUI)
    case "$state" in
        running|stopped) break ;;
    esac
    sleep 0.25
    n=$((n+1))
done
if [ "$state" != running ] && [ "$state" != stopped ]; then
    log "smartUI did not settle (state=$state); starting stock UI"
    setprop ctl.start smartUI || true
    exit 0
fi
log "smartUI settled state=$state"

if [ -f "$STATE/adb.onboot" ]; then
    log 'adb.onboot present; enabling ADB'
    setprop service.adb.tcp.port 5555 || true
    /usr/bin/enable_adb.sh true || true
fi

# BEGIN screenoff preference: independent of ADB; absent/invalid keeps stock policy.
screenoff=/storage/apps/data/settings/screenoff
if [ -f "$screenoff" ]; then
    saved_lock='' saved_timer='' valid=1
    while IFS='=' read -r k v || [ -n "$k" ]; do
        case "$k:$v" in
            lock:0|lock:1) [ -z "$saved_lock" ] || valid=0; saved_lock=$v ;;
            timer:0|timer:30000|timer:60000|timer:120000|timer:300000|timer:600000|timer:1200000|timer:1800000)
                [ -z "$saved_timer" ] || valid=0; saved_timer=$v ;;
            *) valid=0 ;;
        esac
    done < "$screenoff"
    [ -n "$saved_lock" ] && [ -n "$saved_timer" ] || valid=0
    [ "$saved_lock:$saved_timer" != 0:0 ] || valid=0
    if [ "$valid" = 1 ]; then
        # Never write timer=0: unlocking later must not instantly blank the panel.
        applied=1
        if [ "$saved_timer" != 0 ]; then setprop sys.backlight.timer "$saved_timer" || applied=0; fi
        if [ "$applied" = 1 ]; then setprop sys.backlight.lock "$saved_lock" || applied=0; fi
        setprop sys.backlight.timer.reset 1 || applied=0
        log "Screen-off preference applied=$applied"
    else
        log 'Invalid screen-off preference; retaining stock policy'
    fi
fi
# END screenoff preference

rm -f "$HEARTBEAT"
log "Starting custom desktop release=$(readlink "$BASE/current" 2>/dev/null || echo unknown)"
export C1L_DEFAULT_DESKTOP=1
export C1L_HEARTBEAT="$HEARTBEAT"
/bin/sh "$BASE/current/launcher/run.sh" >>"$LOG" 2>&1 &
SUPERVISOR=$!

# Give the launcher a bounded startup window. A process existing is not enough:
# the launcher must open the framebuffer and publish a fresh heartbeat.
ready=0
n=0
while kill -0 "$SUPERVISOR" 2>/dev/null && [ "$n" -lt 60 ]; do
    if [ -f "$HEARTBEAT" ]; then ready=1; break; fi
    sleep 0.25
    n=$((n+1))
done
if [ "$ready" != 1 ]; then
    log 'Custom desktop did not publish a framebuffer heartbeat; rolling back to stock UI'
    kill -TERM "$SUPERVISOR" 2>/dev/null || true
    wait "$SUPERVISOR" 2>/dev/null || true
    rm -f "$HEARTBEAT"
    setprop ctl.start smartUI || true
    exit 0
fi

log 'Custom desktop heartbeat received; keeping it as the foreground'
wait "$SUPERVISOR"
status=$?
log "Custom desktop supervisor exited status=$status"
rm -f "$HEARTBEAT"

# run.sh normally restores smartUI itself when it took over a running service.
# This second check also covers a missing/failed handoff before smartUI stopped.
if [ "$(getprop init.svc.smartUI)" != running ]; then
    log 'Ensuring stock UI is running after custom desktop exit'
    setprop ctl.start smartUI || true
fi
exit 0

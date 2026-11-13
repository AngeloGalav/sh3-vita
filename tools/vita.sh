#!/bin/bash
# Puts a build on a PS Vita over Wi-Fi and gets its logs back (adapted from sh2-vita's tools/console/vita.sh).
#
#   tools/vita.sh ip <address>      remember the console's address (or set VITA_IP)
#   tools/vita.sh vpk               upload the whole VPK to ux0:data/sh3.vpk, to install it in VitaShell
#   tools/vita.sh install           replace the installed game's eboot.bin with the one just built
#                                   (the game must have been installed once from the VPK)
#   tools/vita.sh pull [dir]        download the logs, env.txt, sh3port.ini, screenshots
#                                   (default build/console/vita-<date-time>; build/console/vita-latest points at it)
#                                   and list crash dumps left in ux0:data
#   tools/vita.sh put <file> [path] upload a file (default ux0:/data/sh3/, e.g. an env.txt)
#   tools/vita.sh ls [path]         list a folder (default ux0:/data/sh3/)
#   tools/vita.sh get <path> [dir]  download one file, e.g. ux0:/data/psp2core-x.psp2dmp
#   tools/vita.sh diag on|off       upload tools/env-diag.txt as ux0:/data/sh3/env.txt (crash-proof log,
#                                   stats every 2 s, big VRAM drops per draw; slower), or delete env.txt
#   tools/vita.sh rmdisp            delete ux0:/data/sh3/disp.ini (the game's "crashed last time" marker)
#   tools/vita.sh run | stop        start or close the game (vitacompanion only)
#
# The FTP server is VitaShell's (press SELECT once START > Main settings > SELECT button is set to FTP,
# and stay in it) or the vitacompanion plugin's, which stays on even in game. Both use port 1337
# (VITA_FTP_PORT). With vitacompanion, install also closes the game first (no save) and starts it after.
# VITA_BUILD picks the build folder (default build/vita-sh3-clean, the clean candidate).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TITLE=SILH00003
DATA=ux0:/data/sh3
BUILD=${VITA_BUILD:-$ROOT/build/vita-sh3-clean}
IP_FILE=$ROOT/build/console/vita_ip
PORT=${VITA_FTP_PORT:-1337}

usage() { sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
case "$1" in
ip|install|vpk|put|pull|ls|get|diag|rmdisp|run|stop) ;;
*) usage ;;
esac

if [ "$1" = ip ]; then
	[ -n "$2" ] || { echo "usage: $0 ip <address>" >&2; exit 2; }
	mkdir -p "$(dirname "$IP_FILE")"
	echo "$2" > "$IP_FILE"
	echo "console address: $2"
	exit
fi

IP=${VITA_IP:-$(cat "$IP_FILE" 2>/dev/null || true)}
[ -n "$IP" ] || { echo "no address yet: $0 ip <address> (VitaShell shows it after SELECT)" >&2; exit 2; }
URL=ftp://$IP:$PORT

# nocwd: the path goes whole in one command, as vitacompanion expects
ftp() { curl -sS --connect-timeout 5 --ftp-method nocwd "$@"; }

need_ftp() {
	curl -s --connect-timeout 5 -o /dev/null "$URL/" && return
	echo "no FTP at $IP:$PORT: open VitaShell and press SELECT (SELECT button set to FTP), or check vitacompanion" >&2
	exit 1
}

# vitacompanion's command port; fails quietly when there is no vitacompanion
send() { timeout 3 bash -c 'printf "%s\n" "$2" > "/dev/tcp/$1/1338"' _ "$IP" "$1" 2>/dev/null; }

case "$1" in
install)
	[ -f "$BUILD/eboot.bin" ] || { echo "no $BUILD/eboot.bin: build first" >&2; exit 1; }
	need_ftp
	curl -sS --connect-timeout 5 "$URL/ux0:/app/" | grep -q "$TITLE" || {
		echo "$TITLE is not installed: the first time needs the VPK ($0 vpk)" >&2
		exit 1
	}
	if send destroy; then
		companion=1
		sleep 2   # the game has to let go of its eboot
	fi
	ftp -T "$BUILD/eboot.bin" "$URL/ux0:/app/$TITLE/eboot.bin"
	# a cut-off eboot would leave the game unable to start, so read it back
	back=$(mktemp)
	trap 'rm -f "$back"' EXIT
	ftp "$URL/ux0:/app/$TITLE/eboot.bin" -o "$back"
	cmp -s "$BUILD/eboot.bin" "$back" || { echo "eboot.bin on the console differs: run install again" >&2; exit 1; }
	echo "eboot.bin installed and read back: sha256 $(sha256sum "$BUILD/eboot.bin" | cut -c1-16)…"
	if [ -n "$companion" ]; then
		send "launch $TITLE"
		echo "game started"
	fi
	;;
vpk)
	VPK=$BUILD/sh3.vpk
	[ -f "$VPK" ] || { echo "no $VPK: build first" >&2; exit 1; }
	need_ftp
	ftp -T "$VPK" "$URL/ux0:/data/sh3.vpk"
	echo "in ux0:data/sh3.vpk (sha256 $(sha256sum "$VPK" | cut -c1-16)…): open it in VitaShell to install it"
	;;
put)
	[ -f "$2" ] || { echo "no such file: $2" >&2; exit 2; }
	need_ftp
	dest=${3:-$DATA/}
	ftp -T "$2" "$URL/${dest%/}/$(basename "$2")"
	echo "sent $(basename "$2") to ${dest%/}/"
	;;
pull)
	need_ftp
	OUT=${2:-$ROOT/build/console/vita-$(date +%Y%m%d-%H%M%S)}
	mkdir -p "$OUT"
	got=0
	for f in $(curl -sS --connect-timeout 5 --list-only "$URL/$DATA/"); do
		case "$f" in
		sh4log*|env.txt|sh3port.ini|disp.ini|shot_*.tga|ctl.txt|*.log)
			if ftp -f "$URL/$DATA/$f" -o "$OUT/$f" 2>/dev/null; then echo "got $f"; got=$((got + 1)); else rm -f "$OUT/$f"; fi ;;
		esac
	done
	if [ -z "$2" ]; then
		ln -sfn "$(basename "$OUT")" "$ROOT/build/console/vita-latest"
	fi
	echo "$got files in $OUT"
	dumps=$(curl -sS --connect-timeout 5 --list-only "$URL/ux0:/data/" | grep -E '\.(psp2dmp|sgx)(\.gz)?$' || true)
	if [ -n "$dumps" ]; then
		echo "crash dumps on the console (fetch one with: $0 get ux0:/data/<name>):"
		echo "$dumps" | sed 's/^/  /'
	fi
	;;
ls)
	need_ftp
	path=${2:-$DATA/}
	curl -sS --connect-timeout 5 "$URL/${path%/}/"
	;;
get)
	[ -n "$2" ] || { echo "usage: $0 get <path> [dir]" >&2; exit 2; }
	need_ftp
	OUT=${3:-.}
	mkdir -p "$OUT"
	ftp -f "$URL/$2" -o "$OUT/$(basename "$2")"
	echo "got $OUT/$(basename "$2")"
	;;
diag)
	need_ftp
	case "$2" in
	on)
		ftp -T "$ROOT/tools/env-diag.txt" "$URL/$DATA/env.txt"
		echo "env.txt on the console is now:"; ftp "$URL/$DATA/env.txt" | sed 's/^/  /' ;;
	off)
		curl -sS --connect-timeout 5 --ftp-method nocwd "$URL/" -Q "DELE $DATA/env.txt" -o /dev/null && echo "removed $DATA/env.txt" || echo "no env.txt to remove" ;;
	*) echo "usage: $0 diag on|off" >&2; exit 2 ;;
	esac
	;;
rmdisp)
	need_ftp
	curl -sS --connect-timeout 5 --ftp-method nocwd "$URL/" -Q "DELE $DATA/disp.ini" -o /dev/null && echo "removed $DATA/disp.ini" || echo "no disp.ini to remove"
	;;
run)
	send "launch $TITLE" || { echo "no vitacompanion at $IP: start the game on the console" >&2; exit 1; }
	;;
stop)
	send destroy || { echo "no vitacompanion at $IP: close the game on the console" >&2; exit 1; }
	;;
esac

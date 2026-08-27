#!/bin/sh

# Collect a read-only framebuffer/display baseline on the RV1106 board.
# The script is intentionally BusyBox/POSIX-sh compatible and writes only to
# the selected output directory.

set -u

COLLECTOR_VERSION=1
DEFAULT_SAMPLES=60
DEFAULT_INTERVAL=1

usage()
{
	cat <<'EOF'
Usage:
  DISPLAY_SCENARIO=<name> sh collect_fb_display_baseline.sh [OUTPUT_DIR] [SAMPLES] [INTERVAL_S]

Examples:
  DISPLAY_SCENARIO=home sh collect_fb_display_baseline.sh /userdata/display-baseline/home 60 1
  DISPLAY_SCENARIO=yolo sh collect_fb_display_baseline.sh /userdata/display-baseline/yolo 120 1

The collector does not stop/restart DeskBot, change debugfs controls, write the
framebuffer, or modify the running device tree. Switch to the requested UI
scenario before starting it.
EOF
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
esac

BOARD_STAMP=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf 'unknown-time')
OUTPUT_DIR=${1:-/tmp/fb-display-baseline-$BOARD_STAMP}
SAMPLES=${2:-$DEFAULT_SAMPLES}
INTERVAL_S=${3:-$DEFAULT_INTERVAL}
SCENARIO=${DISPLAY_SCENARIO:-unspecified}

case "$SAMPLES" in
	''|*[!0-9]*)
		echo "SAMPLES must be a positive integer" >&2
		exit 2
		;;
esac
case "$INTERVAL_S" in
	''|*[!0-9]*)
		echo "INTERVAL_S must be a positive integer" >&2
		exit 2
		;;
esac
if [ "$SAMPLES" -le 0 ] || [ "$INTERVAL_S" -le 0 ]; then
	echo "SAMPLES and INTERVAL_S must be greater than zero" >&2
	exit 2
fi

umask 022
mkdir -p "$OUTPUT_DIR" || exit 1

command_exists()
{
	command -v "$1" >/dev/null 2>&1
}

read_value()
{
	label=$1
	path=$2
	printf '%s=' "$label"
	if [ -r "$path" ]; then
		tr '\000\n' '  ' < "$path"
		printf '\n'
	else
		printf '<unavailable>\n'
	fi
}

find_deskbot_pid()
{
	candidate=
	if [ -r /var/run/deskbot.pid ]; then
		candidate=$(sed -n '1p' /var/run/deskbot.pid 2>/dev/null)
		if [ -n "$candidate" ] && [ -d "/proc/$candidate" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	fi

	if command_exists pidof; then
		for candidate in $(pidof main 2>/dev/null); do
			if [ -d "/proc/$candidate" ]; then
				printf '%s\n' "$candidate"
				return 0
			fi
		done
	fi

	candidate=$(ps 2>/dev/null | awk '$5 == "./main" || $5 ~ /\/deskbot\/main$/ { print $1; exit }')
	if [ -n "$candidate" ] && [ -d "/proc/$candidate" ]; then
		printf '%s\n' "$candidate"
		return 0
	fi

	return 1
}

{
	echo "collector_version=$COLLECTOR_VERSION"
	echo "scenario=$SCENARIO"
	echo "samples=$SAMPLES"
	echo "interval_s=$INTERVAL_S"
	echo "board_date_local=$(date 2>/dev/null || true)"
	echo "board_date_utc=$(date -u 2>/dev/null || true)"
	echo "output_dir=$OUTPUT_DIR"
	echo "note=Board RTC may be inaccurate; preserve the host-side capture time when copying this directory."
	echo
	echo '[uname]'
	uname -a 2>&1 || true
	echo
	echo '[uptime]'
	cat /proc/uptime 2>&1 || true
	echo
	echo '[cmdline]'
	cat /proc/cmdline 2>&1 || true
	echo
	echo '[mounts]'
	mount 2>&1 || true
} > "$OUTPUT_DIR/manifest.txt" 2>&1

{
	echo '[device nodes]'
	ls -l /dev/fb* 2>&1 || true
	echo
	echo '[graphics sysfs]'
	for fb in /sys/class/graphics/fb*; do
		[ -e "$fb" ] || continue
		echo "path=$fb"
		read_value name "$fb/name"
		read_value virtual_size "$fb/virtual_size"
		read_value bits_per_pixel "$fb/bits_per_pixel"
		read_value stride "$fb/stride"
		read_value rotate "$fb/rotate"
		read_value state "$fb/state"
		read_value blank "$fb/blank"
		read_value modes "$fb/modes"
		printf 'device_link='
		readlink "$fb/device" 2>/dev/null || printf '<unavailable>\n'
		echo
	done
	if command_exists fbset; then
		echo '[fbset]'
		fbset -i 2>&1 || true
	else
		echo 'fbset=<not installed>'
	fi
} > "$OUTPUT_DIR/fb_sysfs.txt" 2>&1

{
	echo '[device nodes]'
	ls -l /dev/dri /dev/dri/* 2>&1 || true
	echo
	echo '[drm sysfs]'
	for node in /sys/class/drm/*; do
		[ -e "$node" ] || continue
		echo "path=$node"
		read_value status "$node/status"
		read_value enabled "$node/enabled"
		read_value modes "$node/modes"
		read_value dpms "$node/dpms"
		printf 'device_link='
		readlink "$node/device" 2>/dev/null || printf '<unavailable>\n'
		echo
	done
} > "$OUTPUT_DIR/drm_sysfs.txt" 2>&1

{
	echo '[SPI devices]'
	for dev in /sys/bus/spi/devices/spi*; do
		[ -e "$dev" ] || continue
		echo "path=$dev"
		read_value modalias "$dev/modalias"
		read_value driver_override "$dev/driver_override"
		read_value uevent "$dev/uevent"
		printf 'driver_link='
		readlink "$dev/driver" 2>/dev/null || printf '<unbound>\n'
		printf 'of_node_link='
		readlink "$dev/of_node" 2>/dev/null || printf '<unavailable>\n'
		echo
	done
	echo '[SPI masters]'
	for master in /sys/class/spi_master/spi*; do
		[ -e "$master" ] || continue
		ls -ld "$master" 2>&1 || true
	done
	echo
	echo '[loaded display/SPI modules]'
	cat /proc/modules 2>/dev/null | grep -i -E 'spi|st7789|fbtft|fb_|drm|mipi|panel' || true
} > "$OUTPUT_DIR/spi_devices.txt" 2>&1

{
	echo '[matching runtime device-tree nodes]'
	if [ -d /proc/device-tree ]; then
		for compatible_file in $(find /proc/device-tree -name compatible 2>/dev/null); do
			compatible=$(tr '\000' ' ' < "$compatible_file" 2>/dev/null)
			case "$compatible" in
				*st7789*|*fbtft*|*spidev*)
					node=$(dirname "$compatible_file")
					echo "node=$node"
					echo "compatible=$compatible"
					for property in status reg spi-max-frequency fps buswidth debug rotate rotation dc-gpios reset-gpios led-gpios backlight power-supply; do
						if [ -r "$node/$property" ]; then
							echo "property=$property"
							hexdump -C "$node/$property" 2>&1 || true
						fi
					done
					echo
					;;
			esac
		done
	else
		echo '/proc/device-tree=<unavailable>'
	fi

	if command_exists dtc && [ -d /proc/device-tree ]; then
		echo '[runtime device tree decompile]'
		dtc -I fs -O dts /proc/device-tree 2>&1 || true
	else
		echo 'dtc=<not installed; matching nodes above remain authoritative>'
	fi
} > "$OUTPUT_DIR/runtime_display_dt.txt" 2>&1

{
	if [ -r /proc/config.gz ] && command_exists zcat; then
		zcat /proc/config.gz 2>&1
	elif [ -r /boot/config-$(uname -r 2>/dev/null) ]; then
		cat "/boot/config-$(uname -r)" 2>&1
	else
		echo 'kernel_config=<unavailable>'
	fi
} > "$OUTPUT_DIR/kernel_config.txt" 2>&1

{
	echo '[GPIO debugfs]'
	cat /sys/kernel/debug/gpio 2>&1 || true
	echo
	echo '[pinctrl selections containing display/SPI signals]'
	for pin_file in /sys/kernel/debug/pinctrl/*/pinmux-pins /sys/kernel/debug/pinctrl/*/pinconf-pins; do
		[ -r "$pin_file" ] || continue
		echo "file=$pin_file"
		grep -i -E 'spi0|st7789|fbtft|gpio1-2[048]|gpio1_pc[04]|gpio1_pd0' "$pin_file" 2>/dev/null || true
	done
} > "$OUTPUT_DIR/gpio_pinctrl.txt" 2>&1

{
	if [ -r /sys/kernel/debug/clk/clk_summary ]; then
		cat /sys/kernel/debug/clk/clk_summary
	else
		echo 'clk_summary=<unavailable; debugfs may not be mounted>'
	fi
} > "$OUTPUT_DIR/clk_summary.txt" 2>&1

{
	echo '[interrupts before samples]'
	cat /proc/interrupts 2>&1 || true
} > "$OUTPUT_DIR/interrupts_before.txt" 2>&1

dmesg > "$OUTPUT_DIR/dmesg_before.txt" 2>&1 || true

DESKBOT_PID=$(find_deskbot_pid 2>/dev/null || true)
{
	echo "pid=${DESKBOT_PID:-<not found>}"
	if [ -n "$DESKBOT_PID" ] && [ -d "/proc/$DESKBOT_PID" ]; then
		readlink "/proc/$DESKBOT_PID/exe" 2>&1 || true
		tr '\000' ' ' < "/proc/$DESKBOT_PID/cmdline" 2>/dev/null || true
		echo
		cat "/proc/$DESKBOT_PID/status" 2>&1 || true
		echo '[maps]'
		cat "/proc/$DESKBOT_PID/maps" 2>&1 || true
		echo '[file descriptors]'
		ls -l "/proc/$DESKBOT_PID/fd" 2>&1 || true
	else
		echo 'DeskBot is not running; process samples will contain NA values.'
	fi
} > "$OUTPUT_DIR/deskbot_process.txt" 2>&1

{
	echo '[checksums]'
	if command_exists sha256sum; then
		for artifact in \
			/oem/usr/share/deskbot/main \
			/oem/usr/share/deskbot/model/yolov5.rknn \
			/oem/usr/lib/libdrm.so \
			/oem/usr/lib/libdrm.so.2; do
			[ -r "$artifact" ] || continue
			sha256sum "$artifact" 2>&1 || true
		done
	else
		echo 'sha256sum=<not installed>'
	fi
} > "$OUTPUT_DIR/artifacts.txt" 2>&1

SAMPLE_FILE="$OUTPUT_DIR/process_samples.tsv"
NA_CPU_VALUES=$(printf 'NA\tNA\tNA\tNA\tNA\tNA\tNA\tNA')
NA_PROC_VALUES=$(printf 'NA\tNA\tNA\tNA')
printf 'sample\telapsed_s\tuptime_s\tpid\tcpu_user\tcpu_nice\tcpu_system\tcpu_idle\tcpu_iowait\tcpu_irq\tcpu_softirq\tcpu_steal\tproc_utime\tproc_stime\tproc_vsize_bytes\tproc_rss_pages\tvmrss_kb\tvmhwm_kb\tvoluntary_ctxt\tnonvoluntary_ctxt\tmemfree_kb\tmemavailable_kb\tcached_kb\n' > "$SAMPLE_FILE"

sample=0
while [ "$sample" -lt "$SAMPLES" ]; do
	elapsed=$((sample * INTERVAL_S))
	uptime_s=$(awk '{ print $1 }' /proc/uptime 2>/dev/null)
	cpu_values=$(awk '/^cpu / { print $2 "\t" $3 "\t" $4 "\t" $5 "\t" $6 "\t" $7 "\t" $8 "\t" $9; exit }' /proc/stat 2>/dev/null)
	current_pid=$(find_deskbot_pid 2>/dev/null || true)
	if [ -n "$current_pid" ] && [ -r "/proc/$current_pid/stat" ]; then
		proc_values=$(awk '{ print $14 "\t" $15 "\t" $23 "\t" $24 }' "/proc/$current_pid/stat" 2>/dev/null)
		status_values=$(awk '
			BEGIN { rss="NA"; hwm="NA"; voluntary="NA"; nonvoluntary="NA" }
			/^VmRSS:/ { rss=$2 }
			/^VmHWM:/ { hwm=$2 }
			/^voluntary_ctxt_switches:/ { voluntary=$2 }
			/^nonvoluntary_ctxt_switches:/ { nonvoluntary=$2 }
			END { print rss "\t" hwm "\t" voluntary "\t" nonvoluntary }
		' "/proc/$current_pid/status" 2>/dev/null)
	else
		current_pid=NA
		proc_values=$NA_PROC_VALUES
		status_values=$NA_PROC_VALUES
	fi
	mem_values=$(awk '
		BEGIN { free="NA"; available="NA"; cached="NA" }
		/^MemFree:/ { free=$2 }
		/^MemAvailable:/ { available=$2 }
		/^Cached:/ { cached=$2 }
		END { print free "\t" available "\t" cached }
	' /proc/meminfo 2>/dev/null)
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$sample" "$elapsed" "${uptime_s:-NA}" "$current_pid" \
		"${cpu_values:-$NA_CPU_VALUES}" \
		"$proc_values" "$status_values" "$mem_values" >> "$SAMPLE_FILE"
	sample=$((sample + 1))
	if [ "$sample" -lt "$SAMPLES" ]; then
		sleep "$INTERVAL_S"
	fi
done

{
	echo '[interrupts after samples]'
	cat /proc/interrupts 2>&1 || true
} > "$OUTPUT_DIR/interrupts_after.txt" 2>&1

dmesg > "$OUTPUT_DIR/dmesg_after.txt" 2>&1 || true

{
	echo '[DeskBot log tail]'
	tail -n 1000 /userdata/deskbot/deskbot.log 2>&1 || true
} > "$OUTPUT_DIR/deskbot_log_tail.txt" 2>&1

cat > "$OUTPUT_DIR/operator_notes.txt" <<EOF
scenario=$SCENARIO
screen_color_correct=TODO
screen_orientation_correct=TODO
screen_offset_correct=TODO
visible_tearing=TODO
visible_flicker=TODO
transient_corruption=TODO
operator_notes=TODO
EOF

echo "Display baseline collected in: $OUTPUT_DIR"
echo "Scenario: $SCENARIO; samples: $SAMPLES; interval: ${INTERVAL_S}s"
echo "Copy the complete directory back to docs/logs/st7789-drm/<host-date>/ for analysis."

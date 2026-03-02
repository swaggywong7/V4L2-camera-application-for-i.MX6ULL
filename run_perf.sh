#!/bin/sh
# run_perf.sh — 优化后性能对比测试
# 场景：640x480 YUYV(降级MJPEG) × mode0/1/2/6 + 320x240 mode0基线 + 3分钟内存稳定

PROG=/home/swaggywong7/v4l2_camera
DEV=/dev/video1
LOG=/tmp/perf_test
PIPE=/tmp/cam_ctrl

mkdir -p $LOG
rm -f $LOG/*.txt $LOG/*.log $LOG/done.flag

# ---- 启动程序 ----
rm -f $PIPE && mkfifo $PIPE
$PROG $DEV < $PIPE > $LOG/camera.log 2>&1 &
CAM_PID=$!
echo "CAM_PID=$CAM_PID" | tee $LOG/summary.log
exec 3>$PIPE
sleep 3

# ---- 函数：切换模式后采 60s top ----
test_mode() {
    MODE=$1; LABEL=$2; SAMPLES=30
    printf "\n=== %s ===\n" "$LABEL" | tee -a $LOG/summary.log
    echo "mode $MODE" >&3
    sleep 3
    top -b -n $SAMPLES -d 2 > $LOG/top_${3}.txt 2>&1
    echo "  [完成]" | tee -a $LOG/summary.log
}

# ======== 场景A：640x480（程序默认，YUYV优先） ========
printf "\n====== 场景A: 640x480 ======\n" | tee -a $LOG/summary.log
echo "open" >&3
echo "[*] 等待摄像头启动..."
sleep 6

test_mode 0 "A-mode0 (基线)" A_mode0
test_mode 1 "A-mode1 (高斯)" A_mode1
test_mode 2 "A-mode2 (Canny)" A_mode2
test_mode 6 "A-mode6 (卡通)" A_mode6

echo "close" >&3
sleep 3

# ======== 场景B：320x240 mode0 基线对比 ========
printf "\n====== 场景B: 320x240 ======\n" | tee -a $LOG/summary.log
echo "res 320" >&3
sleep 1
echo "open" >&3
sleep 6

test_mode 0 "B-mode0 320x240基线" B_mode0
test_mode 6 "B-mode6 320x240卡通" B_mode6

echo "close" >&3
sleep 3

# ======== 场景C：mode6 内存稳定性（3分钟） ========
printf "\n====== 场景C: 内存稳定性 mode6 (3分钟) ======\n" | tee -a $LOG/summary.log
echo "res 640" >&3
sleep 1
echo "open" >&3
sleep 6
echo "mode 6" >&3
sleep 2

rm -f $LOG/mem_trend.txt
i=0
while [ $i -lt 36 ]; do
    MEM=$(grep VmRSS /proc/$CAM_PID/status 2>/dev/null | awk '{print $2}')
    [ -z "$MEM" ] && MEM="0"
    printf "%s  %3ds  %s kB\n" "$(date +%H:%M:%S)" "$((i*5))" "$MEM" \
        | tee -a $LOG/mem_trend.txt
    i=$((i+1)); sleep 5
done

echo "quit" >&3
exec 3>&-
wait $CAM_PID 2>/dev/null
printf "\n=== 测试完成 ===\n" | tee -a $LOG/summary.log
echo "DONE" > $LOG/done.flag

#!/bin/sh
# run_perf.sh — v4l2_camera 三项指标自动化测试脚本
# 测试：mode 0/1/2/6 各60秒，再做3分钟内存稳定性测试

PROG=/home/swaggywong7/v4l2_camera
DEV=/dev/video1
LOG=/tmp/perf_test
PIPE=/tmp/cam_ctrl

mkdir -p $LOG
rm -f $LOG/*.txt $LOG/*.log $LOG/done.flag

# ---- 启动相机应用 ----
rm -f $PIPE
mkfifo $PIPE

echo "[*] 启动程序..."
$PROG $DEV < $PIPE > $LOG/camera.log 2>&1 &
CAM_PID=$!
echo "CAM_PID=$CAM_PID" | tee $LOG/summary.log

# 保持管道写端常开，防止程序收到 EOF 退出
exec 3>$PIPE
sleep 3

# 打开摄像头并等待稳定
echo "open" >&3
echo "[*] 等待摄像头启动..."
sleep 6

# ---- 函数：测试某个处理模式 ----
# 参数：$1=模式号  $2=标签
test_mode() {
    MODE=$1
    LABEL=$2
    SAMPLES=30      # 30次 × 2秒 = 60秒采样窗口

    printf "\n=== %s ===\n" "$LABEL" | tee -a $LOG/summary.log
    echo "mode $MODE" >&3
    sleep 3         # 等待模式切换生效

    # top -b 批处理，列顺序：PID PPID USER STAT VSZ %VSZ %CPU CMD
    top -b -n $SAMPLES -d 2 > $LOG/top_${MODE}.txt 2>&1

    echo "  [完成]" | tee -a $LOG/summary.log
}

# ---- 各 mode 测试 ----
test_mode 0 "mode0 (基线, 不处理)"
test_mode 1 "mode1 (高斯模糊)"
test_mode 2 "mode2 (Canny边缘检测)"
test_mode 6 "mode6 (卡通化)"

# ---- 内存稳定性测试：mode6 持续3分钟 ----
printf "\n=== 内存稳定性 (mode6, 3分钟) ===\n" | tee -a $LOG/summary.log
echo "mode 6" >&3
sleep 2

rm -f $LOG/mem_trend.txt
i=0
while [ $i -lt 36 ]; do
    MEM=$(grep VmRSS /proc/$CAM_PID/status 2>/dev/null | awk '{print $2}')
    [ -z "$MEM" ] && MEM="0"
    printf "%s  %3ds  %s kB\n" "$(date +%H:%M:%S)" "$((i * 5))" "$MEM" \
        | tee -a $LOG/mem_trend.txt
    i=$((i + 1))
    sleep 5
done

# ---- 退出 ----
echo "quit" >&3
exec 3>&-
wait $CAM_PID 2>/dev/null

printf "\n=== 全部测试完成 ===\n" | tee -a $LOG/summary.log
echo "DONE" > $LOG/done.flag

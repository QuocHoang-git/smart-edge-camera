#!/bin/sh
# ==============================================================================
# Script: run_all_tests.sh
# Purpose: Execute all unit and hardware integration tests for smart-edge-camera.
# ==============================================================================

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "${TEST_DIR}"

echo "======================================================="
echo " Starting Smart Edge Camera Test Suite"
echo " Date: $(date)"
echo " Device: $(uname -a)"
echo "======================================================="

TOTAL=0
PASSED=0
FAILED=0

run_test() {
    TEST_BIN="$1"
    if [ ! -x "./${TEST_BIN}" ]; then
        echo "[SKIP] ${TEST_BIN} not found or not executable"
        return
    fi

    TOTAL=$((TOTAL + 1))
    echo ""
    echo ">>> Running ${TEST_BIN}..."
    ./"${TEST_BIN}"
    RET=$?
    if [ $RET -eq 0 ]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
}

# Run standalone logic tests first
run_test "test_bounded_queue"
run_test "test_pipeline_logic"
run_test "test_rtsp_server"

# Run hardware-dependent tests (require stopping the main service if it locks hardware)
WAS_RUNNING=0
if /etc/init.d/S99smart_edge_camera status 2>/dev/null | grep -q "running"; then
    echo "Pausing smart_edge_camera service to grant test exclusive hardware access..."
    /etc/init.d/S99smart_edge_camera stop
    WAS_RUNNING=1
    sleep 1
fi

run_test "test_dma_allocator"
run_test "test_rga_converter"
run_test "test_mpp_encoder"
run_test "test_rknn_engine"
run_test "test_v4l2_camera"

if [ $WAS_RUNNING -eq 1 ]; then
    echo ""
    echo "Resuming smart_edge_camera background service..."
    /etc/init.d/S99smart_edge_camera start
fi

echo ""
echo "======================================================="
echo " FINAL TEST REPORT"
echo " Total Test Suites: ${TOTAL}"
echo " Passed: ${PASSED}"
echo " Failed: ${FAILED}"
echo "======================================================="

if [ $FAILED -eq 0 ]; then
    echo "ALL TESTS PASSED!"
    exit 0
else
    echo "SOME TESTS FAILED!"
    exit 1
fi

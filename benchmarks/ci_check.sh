#!/usr/bin/env bash
set -euo pipefail

# --- CONFIGURATION ---
# Define your baseline throughput for GET requests (requests per second)
BASELINE_GET_RPS="${CI_BASELINE_RPS:-50000}" 
THRESHOLD_PCT=80

# Calculate the minimum acceptable throughput
MIN_ACCEPTABLE_RPS=$(( BASELINE_GET_RPS * THRESHOLD_PCT / 100 ))

echo "=== Starting Redis CI Performance Check ==="
echo "Baseline Throughput: $BASELINE_GET_RPS rps"
echo "Minimum acceptable threshold (${THRESHOLD_PCT}%): $MIN_ACCEPTABLE_RPS rps"
echo "------------------------------------"

# --- RUN BENCHMARK ---
echo "Running redis-benchmark..."
# Execute the benchmark. Note: -t set,get runs both if you want to test both.
# We map stderr to stdout so we can catch connection issues in the same variable.
CURRENT_OUTPUT=$(redis-benchmark -h 127.0.0.1 -p 6379 -n 10000 -c 10 -t set,get -r 20000 --csv 2>&1)

echo "$CURRENT_OUTPUT"
echo "------------------------------------"

# --- PARSE & VALIDATE ---
# The CSV format looks exactly like: "GET","87235.12"
# This extracts the number after "GET", drops quotes, and truncates decimals for integer comparison.
CURRENT_RPS=$(echo "$CURRENT_OUTPUT" | grep -i '"GET"' | cut -d',' -f2 | tr -d '"' | cut -d'.' -f1)

if [ -z "$CURRENT_RPS" ]; then
    echo "ERROR: Failed to parse GET throughput. Is Redis running on 127.0.0.1:6379?"
    exit 1
fi

echo "Current GET Throughput: $CURRENT_RPS rps"

if [ "$CURRENT_RPS" -lt "$MIN_ACCEPTABLE_RPS" ]; then
    echo "❌ REGRESSION DETECTED!"
    echo "Performance dropped below the ${THRESHOLD_PCT}% threshold ($MIN_ACCEPTABLE_RPS rps)."
    exit 1
else
    echo "✅ PERFORMANCE CHECK PASSED!"
    exit 0
fi

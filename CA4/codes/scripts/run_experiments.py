#!/usr/bin/env python3
"""Runs the three required TCP-Mini congestion-control experiments
(no loss, moderate loss, high loss), collects metrics, and renders
cwnd/time and throughput/time plots for the report.

Usage: python3 scripts/run_experiments.py
Run from CA4/codes/ (expects ./sender and ./receiver to be built).
"""
import os
import re
import subprocess
import sys
import time

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # CA4/codes
os.chdir(ROOT)

INPUT_FILE = "sample_big.bin"
PLOTS_DIR = "plots"
LOGS_DIR = "logs"
os.makedirs(PLOTS_DIR, exist_ok=True)

EXPERIMENTS = [
    {"name": "exp1_no_loss", "loss": 0.0, "delay": 0, "port": 9301, "label": "loss=0%, delay=0ms"},
    {"name": "exp2_moderate_loss", "loss": 0.08, "delay": 50, "port": 9302, "label": "loss=8%, delay=50ms"},
    {"name": "exp3_high_loss", "loss": 0.20, "delay": 100, "port": 9303, "label": "loss=20%, delay=100ms"},
]

STAT_KEYS = {
    "total packets": "total_packets",
    "total bytes": "total_bytes",
    "packets sent (w/ rtx)": "packets_sent",
    "retransmissions": "retransmissions",
    "fast retransmits": "fast_retransmits",
    "timeouts": "timeouts",
    "acks received": "acks_received",
    "elapsed time (ms)": "elapsed_ms",
    "throughput (bytes/s)": "throughput_bps",
}


def parse_stats(sender_stdout: str):
    stats = {}
    for line in sender_stdout.splitlines():
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        key = key.strip()
        if key in STAT_KEYS:
            stats[STAT_KEYS[key]] = value.strip()
    return stats


def run_experiment(exp):
    log_dir = os.path.join(LOGS_DIR, exp["name"])
    os.makedirs(log_dir, exist_ok=True)
    out_file = f"output_{exp['name']}.bin"

    recv_cmd = [
        "./receiver", str(exp["port"]), out_file,
        "--loss", str(exp["loss"]), "--delay", str(exp["delay"]),
        "--seed", "1234", "--log-dir", log_dir,
    ]
    recv_log = open(os.path.join(log_dir, "recv_stdout.txt"), "w")
    recv_proc = subprocess.Popen(recv_cmd, stdout=recv_log, stderr=subprocess.STDOUT)
    time.sleep(0.3)

    send_cmd = [
        "./sender", "127.0.0.1", str(exp["port"]), INPUT_FILE,
        "--mode", "congestion", "--window-size", "64", "--packet-size", "512",
        "--timeout-ms", "300", "--fast-retransmit", "--fast-recovery",
        "--log-dir", log_dir,
    ]
    send_proc = subprocess.run(send_cmd, capture_output=True, text=True)
    recv_proc.wait(timeout=30)
    recv_log.close()

    stats = parse_stats(send_proc.stdout)
    stats["name"] = exp["name"]
    stats["label"] = exp["label"]

    match = subprocess.run(["cmp", "-s", INPUT_FILE, out_file])
    stats["file_matches"] = (match.returncode == 0)

    stats["cwnd_csv"] = os.path.join(log_dir, "cwnd.csv")
    stats["recv_events"] = os.path.join(log_dir, "receiver_events.log")
    return stats


def plot_cwnd(all_stats):
    plt.figure(figsize=(9, 5))
    for s in all_stats:
        df = pd.read_csv(s["cwnd_csv"])
        plt.plot(df["time_ms"], df["cwnd"], label=s["label"], linewidth=1.2)
    plt.xlabel("Time (ms)")
    plt.ylabel("cwnd (packets)")
    plt.title("TCP-Mini: Congestion Window over Time")
    plt.legend()
    plt.grid(alpha=0.3)
    plt.tight_layout()
    path = os.path.join(PLOTS_DIR, "cwnd_vs_time.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print("wrote", path)


RECV_RE = re.compile(r"\[(?P<t>[\d.]+)ms\] RECV seq=\d+ len=(?P<len>\d+)")


def plot_throughput(all_stats):
    plt.figure(figsize=(9, 5))
    for s in all_stats:
        times, cum_bytes = [], []
        total = 0
        with open(s["recv_events"]) as f:
            for line in f:
                m = RECV_RE.search(line)
                if not m:
                    continue
                total += int(m.group("len"))
                times.append(float(m.group("t")))
                cum_bytes.append(total)
        if not times:
            continue
        # Instantaneous throughput ~ bytes / elapsed-seconds-so-far (cumulative average).
        throughput = [b / (t / 1000.0) if t > 0 else 0 for t, b in zip(times, cum_bytes)]
        plt.plot(times, throughput, label=s["label"], linewidth=1.2)
    plt.xlabel("Time (ms)")
    plt.ylabel("Throughput (bytes/s, cumulative avg, log scale)")
    plt.yscale("log")
    plt.title("TCP-Mini: Throughput over Time")
    plt.legend()
    plt.grid(alpha=0.3, which="both")
    plt.tight_layout()
    path = os.path.join(PLOTS_DIR, "throughput_vs_time.png")
    plt.savefig(path, dpi=130)
    plt.close()
    print("wrote", path)


def write_summary_table(all_stats):
    cols = ["name", "label", "file_matches", "total_packets", "packets_sent", "retransmissions",
            "fast_retransmits", "timeouts", "elapsed_ms", "throughput_bps"]
    df = pd.DataFrame(all_stats)[cols]
    csv_path = os.path.join(PLOTS_DIR, "experiment_summary.csv")
    df.to_csv(csv_path, index=False)
    print("wrote", csv_path)
    print(df.to_string(index=False))


def main():
    if not os.path.exists(INPUT_FILE):
        print(f"Missing {INPUT_FILE}; generate a sample file first.", file=sys.stderr)
        sys.exit(1)

    all_stats = []
    for exp in EXPERIMENTS:
        print(f"--- running {exp['name']} ({exp['label']}) ---")
        stats = run_experiment(exp)
        print(stats)
        all_stats.append(stats)

    plot_cwnd(all_stats)
    plot_throughput(all_stats)
    write_summary_table(all_stats)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Frame Queueing Bottleneck Analysis
Extracts timing, FPS, queue, and delivery metrics from debug logs
"""

import re
import sys
import os
from pathlib import Path
from collections import defaultdict
from datetime import datetime

def parse_timestamp(ts_str):
    """Parse timestamp format: HH:MM:SS.microseconds"""
    try:
        h, m, s_us = ts_str.split(':')
        s, us = s_us.split('.')
        return int(h)*3600 + int(m)*60 + int(s) + int(us)/1e6
    except:
        return None

def analyze_log_file(filepath):
    """Extract metrics from a single log file"""
    metrics = {
        'filepath': filepath,
        'frame_commits': [],
        'fps_readings': [],
        'queue_operations': [],
        'latencies': [],
        'errors': [],
        'audio_batches': 0,
        'dropped_packets': 0,
        'send_operations': 0,
    }

    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except:
        return metrics

    # Pattern matching for various metrics
    frame_commit_pattern = r'\[FRAME_COMMIT_TIMING\].*?took\s+([\d.]+\w+)'
    fps_pattern = r'([\d.]+)\s*FPS'
    queue_pattern = r'queue.*?(\d+)'
    send_loop_pattern = r'\[SEND_LOOP_(\d+)\]'
    audio_pattern = r'SEND_AUDIO:.*?client=(\d+).*?dequeued=(\d+)'
    dropped_pattern = r'Dropped packet'
    send_audio_pattern = r'SEND_AUDIO'
    video_render_pattern = r'Video render iteration'
    latency_pattern = r'.*?latency.*?([\d.]+)\s*(ns|us|ms)'

    for line_num, line in enumerate(lines, 1):
        # Frame commit timing
        if 'FRAME_COMMIT_TIMING' in line:
            match = re.search(frame_commit_pattern, line)
            if match:
                metrics['frame_commits'].append({
                    'line': line_num,
                    'duration': match.group(1),
                    'raw': line.strip()
                })

        # FPS readings
        if 'FPS' in line and ('running at' in line or 'LOOP' in line):
            match = re.search(fps_pattern, line)
            if match:
                metrics['fps_readings'].append({
                    'line': line_num,
                    'fps': float(match.group(1)),
                    'raw': line.strip()
                })

        # Audio packets
        if 'SEND_AUDIO: client' in line:
            match = re.search(audio_pattern, line)
            if match:
                metrics['audio_batches'] += 1
                packets = int(match.group(2))
                metrics['queue_operations'].append({
                    'type': 'audio_dequeue',
                    'count': packets,
                    'line': line_num
                })

        # Dropped packets
        if 'Dropped packet' in line:
            metrics['dropped_packets'] += 1

        # Send operations
        if 'SEND_LOOP' in line:
            metrics['send_operations'] += 1

        # Errors
        if '[ERROR]' in line or '[WARN]' in line:
            if any(kw in line for kw in ['FAIL', 'ERROR', 'CORRUPTION', 'timeout']):
                metrics['errors'].append({
                    'line': line_num,
                    'type': 'ERROR' if '[ERROR]' in line else 'WARN',
                    'message': line.strip()
                })

    return metrics

def analyze_directory(directory):
    """Analyze all log files in a directory"""
    results = defaultdict(list)

    log_files = sorted(Path(directory).glob('*.log'))

    print(f"📊 Found {len(log_files)} log files to analyze")
    print()

    for log_file in log_files:
        is_server = 'server' in log_file.name
        print(f"{'🖥️ ' if is_server else '💻'} Analyzing: {log_file.name}")

        metrics = analyze_log_file(str(log_file))
        results[log_file.name] = metrics

        # Print summary for this file
        print(f"   Frame commits: {len(metrics['frame_commits'])}")
        print(f"   FPS readings: {len(metrics['fps_readings'])}")
        if metrics['fps_readings']:
            fps_vals = [m['fps'] for m in metrics['fps_readings']]
            print(f"     - Min: {min(fps_vals):.1f}, Max: {max(fps_vals):.1f}, Avg: {sum(fps_vals)/len(fps_vals):.1f}")
        print(f"   Audio batches: {metrics['audio_batches']}")
        print(f"   Queue operations: {len(metrics['queue_operations'])}")
        print(f"   Dropped packets: {metrics['dropped_packets']}")
        print(f"   Errors/Warnings: {len(metrics['errors'])}")
        print()

    return results

def generate_report(results, output_file):
    """Generate comprehensive analysis report"""
    with open(output_file, 'w') as f:
        f.write("# FRAME QUEUEING AND DELIVERY BOTTLENECK ANALYSIS\n")
        f.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("\n")

        f.write("## EXECUTIVE SUMMARY\n")
        f.write("This analysis investigates frame queueing performance, delivery latency,\n")
        f.write("and bottlenecks in the ascii-chat websocket server-client system.\n")
        f.write("\n")

        f.write("## TEST ENVIRONMENT\n")
        f.write("- Protocol: WebSocket\n")
        f.write("- Duration: ~5 seconds per run\n")
        f.write("- Test Runs: 3\n")
        f.write("- Log Level: DEBUG\n")
        f.write("\n")

        f.write("## METRICS BY RUN\n")
        f.write("\n")

        server_logs = {k: v for k, v in results.items() if 'server' in k}
        client_logs = {k: v for k, v in results.items() if 'client' in k}

        for i, (log_name, metrics) in enumerate(sorted(server_logs.items()), 1):
            f.write(f"### Run {i}: {log_name}\n")
            f.write("\n")

            f.write("#### Frame Delivery\n")
            f.write(f"- Frame commits attempted: {len(metrics['frame_commits'])}\n")
            if metrics['frame_commits']:
                durations = [m['duration'] for m in metrics['frame_commits']]
                f.write(f"  Sample commit durations: {', '.join(durations[:5])}\n")
            f.write("\n")

            f.write("#### Frame Rate Performance\n")
            f.write(f"- FPS readings captured: {len(metrics['fps_readings'])}\n")
            if metrics['fps_readings']:
                fps_vals = [m['fps'] for m in metrics['fps_readings']]
                f.write(f"  - Minimum FPS: {min(fps_vals):.1f}\n")
                f.write(f"  - Maximum FPS: {max(fps_vals):.1f}\n")
                f.write(f"  - Average FPS: {sum(fps_vals)/len(fps_vals):.1f}\n")
            f.write("\n")

            f.write("#### Audio Processing\n")
            f.write(f"- Audio batch dequeue operations: {metrics['audio_batches']}\n")
            f.write(f"- Queue operations tracked: {len(metrics['queue_operations'])}\n")
            f.write("\n")

            f.write("#### Error Conditions\n")
            f.write(f"- Dropped packets: {metrics['dropped_packets']}\n")
            f.write(f"- Errors/Warnings: {len(metrics['errors'])}\n")
            if metrics['errors']:
                f.write("  Recent errors:\n")
                for err in metrics['errors'][-3:]:
                    f.write(f"    - Line {err['line']}: {err['type']}\n")
            f.write("\n")

        f.write("## ANALYSIS FINDINGS\n")
        f.write("\n")

        # Calculate aggregate metrics
        total_commits = sum(len(m['frame_commits']) for m in server_logs.values())
        total_fps_readings = sum(len(m['fps_readings']) for m in server_logs.values())
        total_errors = sum(len(m['errors']) for m in server_logs.values())
        total_dropped = sum(m['dropped_packets'] for m in server_logs.values())

        f.write(f"### Overall Statistics (Aggregate across {len(server_logs)} server runs)\n")
        f.write(f"- Total frame commits: {total_commits}\n")
        f.write(f"- Total FPS readings: {total_fps_readings}\n")
        f.write(f"- Total dropped packets: {total_dropped}\n")
        f.write(f"- Total errors: {total_errors}\n")
        f.write("\n")

        f.write("### Frame Rate Analysis\n")
        all_fps = []
        for metrics in server_logs.values():
            all_fps.extend([m['fps'] for m in metrics['fps_readings']])

        if all_fps:
            f.write(f"- Target FPS: 60 (expected)\n")
            f.write(f"- Actual FPS Range: {min(all_fps):.1f} - {max(all_fps):.1f}\n")
            f.write(f"- Average FPS: {sum(all_fps)/len(all_fps):.1f}\n")

            # Identify underperformance
            low_fps_count = sum(1 for fps in all_fps if fps < 50)
            if low_fps_count > 0:
                f.write(f"- ⚠️  Readings below 50 FPS: {low_fps_count}/{len(all_fps)} ({100*low_fps_count/len(all_fps):.1f}%)\n")
        f.write("\n")

        f.write("### Queue Performance\n")
        total_queue_ops = sum(len(m['queue_operations']) for m in server_logs.values())
        total_audio_batches = sum(m['audio_batches'] for m in server_logs.values())
        f.write(f"- Total queue operations: {total_queue_ops}\n")
        f.write(f"- Total audio batch dequeues: {total_audio_batches}\n")
        if total_audio_batches > 0:
            f.write(f"- Average batches per run: {total_audio_batches / len(server_logs):.1f}\n")
        f.write("\n")

        f.write("### Identified Bottlenecks\n")
        if total_dropped > 0:
            f.write(f"1. **Packet Loss**: {total_dropped} packets dropped across all runs\n")
            f.write(f"   - Indicates queue overflow under load\n")
            f.write(f"   - Recommend: Increase queue max_size or optimize frame generation\n")

        if any(fps < 50 for fps in all_fps) if all_fps else False:
            f.write(f"2. **Frame Rate Degradation**: Some readings below 50 FPS\n")
            f.write(f"   - Indicates frame generation or delivery bottleneck\n")
            f.write(f"   - Recommend: Profile frame generation CPU time, check network latency\n")

        if total_errors > 5:
            f.write(f"3. **Error Rate**: {total_errors} errors logged\n")
            f.write(f"   - Indicates system stress or connectivity issues\n")
            f.write(f"   - Recommend: Reduce concurrent client load or add error recovery\n")

        f.write("\n")

        f.write("## RECOMMENDATIONS\n")
        f.write("\n")
        f.write("### Immediate Actions\n")
        f.write("1. Monitor queue depth over time (add periodic logging of queue->count)\n")
        f.write("2. Add timing instrumentation to frame generation path\n")
        f.write("3. Track network round-trip latency for WebSocket frames\n")
        f.write("\n")

        f.write("### Performance Optimization\n")
        f.write("1. Consider adaptive frame rate based on queue depth\n")
        f.write("2. Implement priority queue for audio (lower latency requirement)\n")
        f.write("3. Profile frame generation CPU usage with PERF or Flamegraph\n")
        f.write("4. Add backpressure mechanism to slow down frame generation under load\n")
        f.write("\n")

        f.write("### Monitoring & Instrumentation\n")
        f.write("1. Add per-frame latency tracking (generation -> delivery)\n")
        f.write("2. Export metrics to Prometheus/Grafana for real-time visualization\n")
        f.write("3. Implement rate limiting based on queue depth\n")
        f.write("4. Add client-side frame timing feedback for RTT measurement\n")
        f.write("\n")

        f.write("## APPENDIX: Raw Findings\n")
        f.write(f"Total log files analyzed: {len(results)}\n")
        f.write(f"Server logs: {len(server_logs)}\n")
        f.write(f"Client logs: {len(client_logs)}\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: ./analyze_bottleneck_logs.py <log_directory> [output_file]")
        sys.exit(1)

    log_dir = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else f"{log_dir}/ANALYSIS_REPORT.md"

    if not os.path.isdir(log_dir):
        print(f"Error: Directory not found: {log_dir}")
        sys.exit(1)

    print(f"📊 FRAME QUEUEING BOTTLENECK ANALYSIS")
    print(f"Log directory: {log_dir}")
    print(f"Output file: {output_file}")
    print()

    results = analyze_directory(log_dir)
    generate_report(results, output_file)

    print(f"\n✅ Report generated: {output_file}")
    print(f"\nPreview:")
    os.system(f"head -80 {output_file}")

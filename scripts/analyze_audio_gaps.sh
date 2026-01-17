#!/bin/bash
# Analyze audio gaps using ffmpeg and shell tools

WAV_FILE="$1"

if [[ ! -f "$WAV_FILE" ]]; then
    echo "Usage: $0 <wav_file>"
    exit 1
fi

echo "Converting WAV to raw samples for analysis..."
ffmpeg -hide_banner -loglevel error -i "$WAV_FILE" -f f32le -acodec pcm_f32le -ac 1 /tmp/raw_audio.f32

# Get duration and sample count
DURATION=$(ffprobe -hide_banner -loglevel error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$WAV_FILE")
SAMPLE_RATE=48000

echo "================================================================================
"
echo "Audio File: $WAV_FILE"
echo "Duration: $DURATION seconds"
echo "Sample Rate: $SAMPLE_RATE Hz"
echo ""

# Use Python to analyze the raw float32 samples
python3 << 'PYEOF'
import struct
import sys

# Read float32 samples
with open('/tmp/raw_audio.f32', 'rb') as f:
    raw = f.read()

n_samples = len(raw) // 4
print(f"Total samples: {n_samples:,}")
print()

# Parse all samples
samples = []
for i in range(0, len(raw), 4):
    sample = struct.unpack('<f', raw[i:i+4])[0]
    samples.append(sample)

# Analyze gaps at different thresholds
for threshold_name, threshold in [("Very Quiet (-60dB)", 0.001), 
                                   ("Quiet (-40dB)", 0.01),
                                   ("Noticeable (-26dB)", 0.05)]:
    print(f"="*80)
    print(f"GAP ANALYSIS: {threshold_name}, threshold={threshold:.4f}")
    print(f"="*80)
    
    # Find gaps
    gaps = []
    in_gap = False
    gap_start = 0
    
    for i, sample in enumerate(samples):
        if abs(sample) < threshold:
            if not in_gap:
                gap_start = i
                in_gap = True
        else:
            if in_gap:
                gap_len = i - gap_start
                if gap_len >= 10:  # Min 10 samples
                    gaps.append((gap_start, gap_len))
                in_gap = False
    
    # Close final gap
    if in_gap:
        gap_len = len(samples) - gap_start
        if gap_len >= 10:
            gaps.append((gap_start, gap_len))
    
    # Statistics
    total_gap_samples = sum(g[1] for g in gaps)
    total_gap_ms = (total_gap_samples / 48000) * 1000
    gap_percentage = (total_gap_samples / len(samples)) * 100
    
    print(f"Total gaps: {len(gaps):,}")
    print(f"Total gap duration: {total_gap_ms:.1f} ms ({gap_percentage:.1f}%)")
    
    if gaps:
        gap_lengths = [g[1] for g in gaps]
        avg_gap = sum(gap_lengths) / len(gap_lengths)
        print(f"Average gap: {avg_gap:.1f} samples ({(avg_gap/48000*1000):.2f} ms)")
        print(f"Max gap: {max(gap_lengths)} samples ({(max(gap_lengths)/48000*1000):.1f} ms)")
        print(f"Min gap: {min(gap_lengths)} samples ({(min(gap_lengths)/48000*1000):.2f} ms)")
        
        # Show first 30 gaps
        print(f"\nFirst 30 gaps:")
        for i, (start, length) in enumerate(gaps[:30]):
            time_ms = (start / 48000) * 1000
            length_ms = (length / 48000) * 1000
            print(f"  {i+1:3d}. {time_ms:8.1f} ms: {length_ms:6.2f} ms ({length:6d} samples)")
        
        if len(gaps) > 30:
            print(f"  ... and {len(gaps)-30} more gaps")
        
        # Inter-gap intervals
        if len(gaps) > 1:
            intervals = []
            for i in range(len(gaps) - 1):
                gap_end = gaps[i][0] + gaps[i][1]
                next_start = gaps[i+1][0]
                interval = next_start - gap_end
                intervals.append(interval)
            
            intervals_ms = [(iv / 48000) * 1000 for iv in intervals]
            avg_interval = sum(intervals_ms) / len(intervals_ms)
            
            rapid_stutter = [iv for iv in intervals_ms if iv < 20]
            
            print(f"\nInter-gap intervals (time between gaps):")
            print(f"  Average: {avg_interval:.2f} ms")
            print(f"  Min: {min(intervals_ms):.2f} ms")
            print(f"  Max: {max(intervals_ms):.2f} ms")
            
            if rapid_stutter:
                print(f"\n  🔴 RAPID STUTTERING: {len(rapid_stutter)} intervals < 20ms!")
                print(f"     This creates the 'thousands of tiny interrupts' effect")
    
    print()

PYEOF

rm -f /tmp/raw_audio.f32

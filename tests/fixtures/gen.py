#!/usr/bin/env python3
import math
import struct
import wave
import os

def generate_sine_wave():
    output_path = os.path.join(os.path.dirname(__file__), "sounds", "sine440.wav")
    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    sample_rate = 22050
    duration = 0.1  # 100 ms
    frequency = 440.0
    num_samples = int(duration * sample_rate)

    with wave.open(output_path, "wb") as wav_file:
        wav_file.setnchannels(1)       # Mono
        wav_file.setsampwidth(2)      # 16-bit
        wav_file.setframerate(sample_rate)

        for i in range(num_samples):
            t = float(i) / sample_rate
            val = math.sin(2.0 * math.pi * frequency * t)
            sample = int(val * 32767.0)
            wav_file.writeframes(struct.pack("<h", sample))

    print(f"Generated {output_path} ({os.path.getsize(output_path)} bytes)")

if __name__ == "__main__":
    generate_sine_wave()

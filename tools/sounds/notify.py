#!/usr/bin/env python3
# The notification chime (ui/assets/rnds/sounds/notify.wav), made from scratch: two soft bell notes, a fifth apart
# (E6 then B6), each a sine with a quiet octave and twelfth over it, decaying. Mono, 16-bit, 44.1 kHz like the other
# sounds. Usage: python3 tools/sounds/notify.py ui/assets/rnds/sounds/notify.wav
import math, struct, sys, wave

RATE, LENGTH, PEAK = 44100, 0.42, 0.45
NOTES = [(0.00, 1318.5), (0.09, 1975.5)]	# (start s, Hz)
PARTS = [(1.0, 1.0), (2.0, 0.18), (3.0, 0.06)]	# (times the note, loudness)

out = []
for i in range(int(RATE * LENGTH)):
	t, v = i / RATE, 0.0
	for start, f in NOTES:
		u = t - start
		if u < 0: continue
		env = min(1.0, u / 0.004) * math.exp(-u * 11)	# a 4 ms attack (no click), then the ring dying away
		v += env * sum(a * math.sin(2 * math.pi * f * k * u) for k, a in PARTS)
	fade = min(1.0, (LENGTH - t) / 0.03)	# the last 30 ms faded out
	out.append(v * fade)
top = max(abs(x) for x in out)
w = wave.open(sys.argv[1], "wb")
w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
w.writeframes(b"".join(struct.pack("<h", int(x / top * PEAK * 32767)) for x in out))
w.close()

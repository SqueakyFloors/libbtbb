#!/usr/bin/env python3

# BARKER_DISTANCE[] and barker_correct[] in lib/src/bluetooth_packet.c.
#
# The last 7 bits of a sync word are the 6-bit barker sequence with the LAP's
# most significant bit in front of it: 0001101 where that bit is 0, and the
# complemented sequence 1110010 where it is 1 (Core specification Vol 2,
# Part B, Section 6.3.3.3). Only those two patterns are ever transmitted, so a
# received pattern is corrected to whichever of the two it is closer to, and
# the distance to it says how many bits were wrong.
#
# The two codes below are written in transmission order, first bit transmitted
# first, as the specification writes them. The library reads those 7 bits off
# the air through air_to_host8(), which puts the first symbol in bit 0, so the
# index of both tables is the same 7 bits the other way round. The correction
# goes back into the top 7 bits of the 64-bit sync word.
barkers = (0x0d, 0x72)
distances = [0] * 128
corrections = [0] * 128

def count_bits(x):
    i = 0
    while x:
        i += 1
        x &= x - 1
    return i

def reverse7(x):
    """The same 7 bits the other way round."""
    r = 0
    for i in range(7):
        r = (r << 1) | ((x >> i) & 1)
    return r

for i in range(128):
    diffs = [(count_bits(barkers[0] ^ i), barkers[0]),
             (count_bits(barkers[1] ^ i), barkers[1])]
    diffs.sort()
    distances[reverse7(i)] = diffs[0][0]
    corrections[reverse7(i)] = reverse7(diffs[0][1]) << 57

print("Barker distance:", distances)
print("Barker correct:", ["0x%016xULL" % c for c in corrections])

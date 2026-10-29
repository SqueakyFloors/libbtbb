#!/usr/bin/env python3

# The LE whitening sequence (Core specification Vol 6, Part B, Section 3.2).
#
# Whitening is a 7-bit shift register with the polynomial x^7 + x^4 + 1, loaded
# with the channel index, so every channel's sequence is a window into the one
# 127-bit run the register produces. This prints that run, and then the offset
# into it at which each of the 40 channels starts.
#
# The same register, loaded from the clock instead, whitens BR/EDR (Vol 2,
# Part B, Section 7.2): WHITENING_DATA[] in lib/src/bluetooth_packet.c is this
# run starting at a different offset, and INDICES[] there is the same idea as
# the channel offsets below.

num_channels = 40
bits = []
channels = {}
reg = 0x57

while not (len(bits) and reg==0x57):
    if reg & 0x3f < num_channels and reg & 0x40:
        channels[reg & 0x3f] = len(bits)
    bit = (reg & 1)
    bits.append(bit)
    reg >>= 1
    reg |= (bit << 6)
    reg ^= (bit << 2)

print(bits)

print("\nArray index:")
for k in sorted(channels.keys()):
    print(channels[k])

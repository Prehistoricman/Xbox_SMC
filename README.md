# Xbox_SMC
PIC16 dumps from the original Xbox and IDA databases with some reverse engineering done.

# Dumps
The .bin file for each dump has the bytes swapped for correct loading into IDA. So the first word of PIC memory is byte_0 + byte_1 << 8.

## P01
Dumped from a v1.0 Xbox.

# Dumping methodology
I don't want to reveal all the details yet, so here is the vague outline on how I dumped the PIC.

The PIC has its code protection enabled so all the data reads as 0000. I used some trick to get scrambled data output instead, discovered the scrambling algorithm using a sacrificial chip, and then used a kind of exploit to change the ROM data to fix some bits to known values. Then I can extract the required information from the multiple scrambled dumps to rebuild the original data.

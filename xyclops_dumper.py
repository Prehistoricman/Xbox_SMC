import serial

port = serial.Serial("COM4", 9600, timeout=0.1)

cmd = 0x15 #0x14 for BIOS, 0x15 for SMC program

#sync using a dummy command
synced = False
for i in range(5):
    port.write(b"o")
    if port.read(2):
        synced = True
        break
if not synced:
    print("Couldn't sync")
    exit(1)
print("Serial synced")
   
with open("dump.bin", "wb") as outfile:
    for i in range(0, 0x2000, 64):
        #Send command and address
        port.write([cmd, i >> 8, i & 0xFF, 0])
        
        response = port.read(1 + 64) #1 byte of command echo, 64 bytes of data
        if response[0] != cmd:
            print("Cmd echo = 0x%X (expected 0x%X)" % (response[0], cmd))
            exit(1)
        payload = response[1:]
        outfile.write(payload)
        outfile.flush()
        print("0x%X - 0x%X dumped" % (i, i+63))
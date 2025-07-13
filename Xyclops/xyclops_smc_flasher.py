try:
    import serial
except ModuleNotFoundError:
    print("Error: pyserial not installed. install by executing: pip install pyserial")
    exit(1)
import serial.tools.list_ports
import time
import math

class Xyclops:
    def __init__(self, port):
        self.port = port
        
    def sync(self):
        self.port.reset_input_buffer()
        synced = False
        for i in range(5):
            self.port.write([0])
            time.sleep(0.03)
            if self.port.in_waiting >= 2:
                self.port.read(2)
                synced = True
                break
        if self.port.in_waiting != 0:
            synced = False
        return synced

    def read_RAM(self, addr):
        self.port.write([1, 0, addr, 0])
        return self.port.read(2)[1]
    def write_RAM(self, addr, value):
        self.port.write([0xB, 0, addr, value])
        self.port.read(2)

    def read_register(self, addr):
        self.port.write([0x28, 0, addr, 0])
        return self.port.read(2)[1]
    def write_register(self, addr, value, skipread=False):
        self.port.write([0x2B, 0, addr, value])
        if not skipread:
            self.port.read(2)

    def exit_debug(self):
        self.port.write(b"B...")
        self.port.read(4)

    def enable_prog(self): #Enable programming and erasing
        self.port.write(b"C...")
        self.port.read(2)

    def erase_BIOS(self):
        self.port.write([0x84, 0, 0, 0])
        starttime = time.time()
        attempts = 0
        while self.port.in_waiting != 2:
            time.sleep(0.1)
            attempts += 1
            if attempts > 15: #1.5s timeout. Erase takes only 300ms on my box
                print("Erase timeout")
                return False
        print("Erase took %.3f seconds" % (time.time() - starttime))
        response = self.port.read(2)
        return response[0] == 0x84

    def erase_SMC(self):
        self.port.write([0x85, 0, 0, 0])
        starttime = time.time()
        attempts = 0
        while self.port.in_waiting != 2:
            time.sleep(0.1)
            attempts += 1
            if attempts > 15: #1.5s timeout. Erase takes only 300ms on my box
                print("Erase timeout")
                return False
        print("Erase took %.3f seconds" % (time.time() - starttime))
        response = self.port.read(2)
        return response[0] == 0x85
    
    def read_BIOS(self, addr):
        self.port.write([0x14, (addr >> 8) & 0xFF, addr & 0xFF, 0])
        response = self.port.read(65)
        return response[1:]
    def read_SMC(self, addr):
        self.port.write([0x15, (addr >> 8) & 0xFF, addr & 0xFF, 0])
        response = self.port.read(65)
        return response[1:]
    
    def high_speed(self):
        #Set serial to 38400 baud
        self.write_register(0xE9, 0xEC, skipread=True) #Register E9 controls serial baud
        time.sleep(0.03)
        self.port.baudrate = 38400
        self.port.reset_input_buffer()
        return self.sync() #Check comms
    def low_speed(self):
        #Set serial to 9600 baud (xyclops default)
        self.write_register(0xE9, 0xB0, skipread=True) #Register E9 controls serial baud
        time.sleep(0.03)
        self.port.baudrate = 9600
        self.port.reset_input_buffer()
        return self.sync() #Check comms


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Process a file with serial port communication.")
    parser.add_argument("filepath", type=str, help="Path to the input file")
    parser.add_argument("serial_port", type=str, help="Name of the serial port (e.g., COM1 or /dev/ttyUSB0)")
    parser.add_argument("-O", "--override", action="store_true", help="Overrides SMC size checks")
    parser.add_argument("-s", "--speed", action="store_true", help="Run serial at 9600 (may improve stability but it's 2x slower)")
    parser.add_argument("-v", "--verify", action="store_true", help="Just verify, don't erase or program")
    args = parser.parse_args()
    
    #Formats a list of bytes into a string of hex bytes separated by spaces
    def format_list(l):
        return " ".join(["%02X" % x for x in l])
    
    try:
        port = serial.Serial(args.serial_port, 9600, timeout=0.15)
    except serial.serialutil.SerialException as e:
        print("Serial error:", e)
        if args.serial_port in [x.device for x in serial.tools.list_ports.comports()]:
            print("Port may be in use")
        else:
            print("Port doesn't exist")
            print("Ports available:", ", ".join([x.device for x in serial.tools.list_ports.comports()]))
        exit(1)

    xy = Xyclops(port)
    
    filedata = b""
    try:
        with open(args.filepath, "rb") as infile:
            filedata = infile.read()
    except FileNotFoundError:
        print("Error: File", args.filepath, "not found")
        exit(1)
    
    #Pad filedata to multiple of 64 bytes
    padding = (math.ceil(len(filedata) / 64) * 64) - len(filedata)
    if padding != 0:
        filedata = filedata + b"\xff" * padding
        print("Padded input file to 0x%X bytes" % len(filedata))

    if not args.override:
        if len(filedata) < 0x1000:
            print("Error: SMC file is very small! (disable this check with -O flag)")
            exit(1)
        if len(filedata) > 0x10000:
            print("Error: SMC file is too big! Should be 64KiB max. (disable this check with -O flag)")
            exit(1)
    #Truncate file data to 16KiB
    if len(filedata) > 0x4000:
        filedata = filedata[0:0x4000]

    synced = xy.sync()
    if not synced: #TODO try comms at 38400 too
        print("Sync failed... trying 38400 baud")
        port.baudrate = 38400
        synced = xy.sync()
        if not synced:
            print("Error: No communication from Xyclops. Check connections or try unplugging your Xbox for 10 seconds.")
            exit(1)
        else:
            xy.low_speed()
            print("Xyclops communication established (was running at 38400 baud)")
    else:
        print("Xyclops communication established")

    if not args.verify:
        print("Ready to erase? (y/n)")
        if input() != "y":
            print("Stopped")
            exit(0)
        xy.enable_prog()
        print("Erasing...")
        if not xy.erase_SMC():
            print("Error: erase failed")
            exit(1)
        print("Done")
        
        #Erase check on first 64 bytes
        erase_check = xy.read_SMC(0)
        if erase_check != (b"\xff" * 64):
            print("Error: Erase check failed!")
            print("Data:", format_list(erase_check))
            print("All data should be FF")
            exit(1)

        print("Ready to program? (y/n)")
        if input() != "y":
            print("Stopped")
            exit(0)

    if not args.speed:
        if not xy.high_speed():
            print("Error: failed to set baud rate to faster speed. Unplug your Xbox for 10 seconds, plug it back in and run this script at the slow speed (use -s flag)")
            exit(1)

    try:
        if not args.verify:
            for i in range(0, len(filedata), 64):
                port.write([0x17, (i >> 8) & 0xFF, i & 0xFF])
                port.write(filedata[i:][:64])
                response = port.read(2)
                if response[0] != 0x17:
                    raise Exception("bad response at 0x%X: 0x%X 0x%X" % (i, response[0], response[1]))
                if (i % 0x800) == 0:
                    print("Programmed 0x%X out of 0x%X" % (i, len(filedata)))
    
        print("Do you want to verify? (y/n)")
        if input() != "y":
            print("Good luck :)")
        else:
            #Read back whole SMC and compare to the file
            verif_failed = False
            for i in range(0, len(filedata), 64):
                read = xy.read_SMC(i)
                if read != filedata[i:][:64]:
                    print("Verify failure at 0x%X" % i)
                    print("Expected data:", format_list(filedata[i:][:64]))
                    print("Read data    :", format_list(read))
                    verif_failed = True
                    break
                if (i % 0x800) == 0:
                    print("Verified 0x%X out of 0x%X" % (i, len(filedata)))
            if not verif_failed:
                print("Verification passed")
    except Exception:
        raise #Still stop the script and throw the exception
    finally:
        #Always try to reset the speed, even if an error occurs
        xy.sync() #could have gotten desynced
        if not args.speed:
            xy.low_speed()
        xy.exit_debug()
    
    print("Done")
import serial
import serial.tools.list_ports
import subprocess, os
import sys
import time



def serial_ports():
    ports = serial.tools.list_ports.comports()
    Flasherport = []
    for port, desc, hwid in sorted(ports):
            if hwid[:4] == "USB ":
                devices = hwid[4:].split(" ")
                if len(devices) > 1 and len(devices[0]) > 9:
                    if devices[0][-9:] == "1A86:55D4":
                        Flasherport.append(port)

    return Flasherport



if __name__ == '__main__':

    files = os.listdir("../tmp/")
    if not 'ambit-1.ino.bin' in files:
        print("FW NOT found")
        sys.exit() 
    if not 'ambit-1.ino.bootloader.bin' in files:
        print("Bootloader NOT found")
        sys.exit()

    if not 'ambit-1.ino.partitions.bin' in files:
        print("Partitions NOT found")
        sys.exit()

    if not 'boot_app0.bin' in files:
        print("Boot app NOT found")
        sys.exit()
    
    if not 'esptool.exe' in files:
        print("esptool NOT found")
        sys.exit()
    
    ports = serial_ports()
    if len(ports) == 1:
        port = ports[0]
    else:
        print("more than one flasher found")
        sys.exit()



    while True:
        flash_new_ambit = False

        with serial.Serial(port, 115200) as ser:
            t0 = time.time()
            s = ""
            counter = 0
            while time.time() - t0 < 5:
                while (ser.in_waiting > 0) and (counter < 300):
                    c = ser.read()
                    if c > bytes([9]) and c < bytes([127]):
                        counter += 1
                        s += c.decode()

                    if (counter > 20):
                        if "invalid header" in s:
                            flash_new_ambit = True
                            break
                        else:
                            if c.isascii():
                                print(c.decode(), end="")
                if (counter >= 49): break
                if flash_new_ambit: break
                time.sleep(.25)
                print(".", end="")

        if flash_new_ambit:
            subprocess.call('../tmp/esptool.exe --chip esp32c3 --baud 921600 --port "' + port + '" --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x0 "../tmp/ambit-1.ino.bootloader.bin" 0x8000 "../tmp/ambit-1.ino.partitions.bin" 0xe000 "../tmp/boot_app0.bin" 0x10000 "../tmp/ambit-1.ino.bin" ')
            #print("Flash")
        



    while False:
        print(f'Upload to {port}:\t Y/N')
        c = input()
        if c in ["y", "Y"] or c == "":
            print("upload")

            subprocess.call('../tmp/esptool.exe --chip esp32c3 --baud 921600 --port "' + port + '" --before default_reset --after hard_reset write_flash  -z --flash_mode keep --flash_freq keep --flash_size keep 0x0 "../tmp/ambit-1.ino.bootloader.bin" 0x8000 "../tmp/ambit-1.ino.partitions.bin" 0xe000 "../tmp/boot_app0.bin" 0x10000 "../tmp/ambit-1.ino.bin" ')


        else:
            print("skip")
            sys.exit()




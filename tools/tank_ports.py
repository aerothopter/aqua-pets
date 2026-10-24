"""tank_ports.py - the serial ports an ESP32-S3 board is on (2026-10-23).

A board on USB is the S3's own USB serial: Espressif's vendor id, 0x303A.
macOS names it /dev/cu.usbmodem*, Windows COMn; asking pyserial for the
vendor id finds it on both (flash.sh and preflight.py used to glob the Mac's
names). Which board it is - the tank, or another on USB too - is still
preflight's question: the tank's director answers, another board does not.

  python tools/tank_ports.py      # one port a line
"""
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A


def tank_ports():
    return sorted(p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID)


if __name__ == "__main__":
    for port in tank_ports():
        print(port)

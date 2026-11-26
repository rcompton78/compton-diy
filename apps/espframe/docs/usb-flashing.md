---
title: USB Flashing Help for Guition ESP32-P4
description: Fix common Espframe web installer and USB flashing issues on the Guition ESP32-P4 display, including port choice, browser support, and Web Serial.
---

# USB Flashing Help for Guition ESP32-P4

Use this guide when the Espframe web installer cannot find the display, flashing fails, or the browser does not show a serial port.

## Use the Correct USB-C Port

The Guition ESP32-P4 display has two USB-C ports. For flashing Espframe, use the **bottom USB-C port** near the edge of the board and next to the USB-A connector. The upper USB-C connector is for the screen ribbon cable and will not work for flashing.

<img src="/usb-plug.png" alt="USB-C cable plugged into the bottom USB-C flashing port on the Guition ESP32-P4 display" style="max-width: 100%; border-radius: 8px; margin: 1rem 0;" />

## Browser Requirements

The [Espframe installer](/install) uses Web Serial, which means you need:

- Chrome or Edge on a desktop computer.
- A USB-C data cable, not a charge-only cable.
- Permission to access the serial device when the browser asks.

Safari and Firefox do not support the required browser flashing flow.

## If No Serial Device Appears

- Check that the cable is plugged into the bottom USB-C port.
- Try a different USB-C data cable.
- Try another USB port on the computer.
- Close other tools that may already be connected to the device.
- Refresh the install page and click the install button again.

## If Flashing Starts but Fails

- Keep the browser tab open until the install completes.
- Avoid moving the cable or display during flashing.
- Disconnect and reconnect the display, then start the installer again.
- If WiFi setup does not appear after flashing, look for the setup hotspot named **immich-frame-10inch** (**immich-frame-10inch-v3** on the v3 chip build), connect to it, then visit `http://192.168.4.1`.

## If the Installer Says "Failed to Initialize"

Install pages built before this fix loaded an older flasher (ESP Web Tools 10.4.0) that gave up when the display sent a little serial noise while it was being identified: it stopped with **Failed to initialize**, and every later try failed with `Failed to open serial port` or "The port is already open" until the tab was closed. The current install page retries past that noise. If you see this error:

- Close the install tab (that frees the port), open the install page again and reload it, then retry.
- Press **F12**, open **Console** and click the install button: a line reading `ESP Web Tools <version>` names the flasher. Anything older than 10.5.2 is a stale page.

If it still fails, Windows itself may be refusing the port. To check whether Windows can open the port at all, outside the browser, run this in PowerShell (use the COM number Device Manager shows under **Ports**):

```powershell
$p = New-Object System.IO.Ports.SerialPort COM6,115200
$p.Open(); $p.IsOpen; $p.Close()
```

If it prints `True`, Windows can use the port: close every Chrome window and try the installer again. If it fails with "Access to the path 'COM6' is denied", no browser installer will get through on that PC. We've seen this on one PC even straight after a reboot, on a fresh driver install and on a different USB port, with no program holding the port, while the same display flashed fine from another PC and from WSL2.

Otherwise, flash with [esptool](https://docs.espressif.com/projects/esptool/en/latest/) from a terminal instead. Download the full firmware image for your board (the same file the installer uses, not the `-ota.bin` update image) from the install page's folder or the latest GitHub release:

- `espframe-guition-esp32-p4-jc8012p4a1-v3.bin` for a board with a v3 chip (late 2026 onwards; see [Troubleshooting](/troubleshooting)).
- `espframe-guition-esp32-p4-jc8012p4a1.bin` for an older board.

Close the installer's browser tab first, so it doesn't hold the port. In every command below, use your board's file name.

### From WSL2 (known to work)

This is the route known to flash this board when the browser can't. In WSL2, install esptool (`pip install esptool`). Then, in an administrator PowerShell, pass the CH340 (USB ID `1a86:7523`) through to WSL with [usbipd-win](https://github.com/dorssel/usbipd-win):

```powershell
usbipd list                      # find the CH340's BUSID
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

If `attach` fails with `Device busy` and "The device appears to be used by Windows", bind it with `--force`, which swaps Windows' CH340 driver for usbipd's own, then unplug the display, plug it back into the same USB port, and attach again:

```powershell
usbipd bind --busid <BUSID> --force
# unplug and replug the display
usbipd attach --wsl --busid <BUSID>
```

In WSL:

```bash
esptool --chip esp32p4 -p /dev/ttyUSB0 chip-id
esptool --chip esp32p4 -p /dev/ttyUSB0 -b 460800 erase-flash
esptool --chip esp32p4 -p /dev/ttyUSB0 -b 460800 write-flash 0x0 espframe-guition-esp32-p4-jc8012p4a1-v3.bin
```

Then give the port back to Windows with `usbipd detach --busid <BUSID>`, and `usbipd unbind --busid <BUSID>` if you used `--force`. A Linux PC works the same way, without usbipd.

### From Windows (untested)

Windows esptool resets the board the same way the browser does, so it may fail the same way, but it needs no Python or WSL. Download `esptool-<version>-windows-amd64.zip` from the [esptool releases](https://github.com/espressif/esptool/releases/latest), unzip it, and in PowerShell, in that folder (replace `COM6` with the display's port from Device Manager):

```powershell
.\esptool.exe --chip esp32p4 -p COM6 chip-id
.\esptool.exe --chip esp32p4 -p COM6 -b 460800 erase-flash
.\esptool.exe --chip esp32p4 -p COM6 -b 460800 write-flash 0x0 espframe-guition-esp32-p4-jc8012p4a1-v3.bin
```

### Either way

`chip-id` just checks the connection. If it fails straight away, the port is probably still stuck from the browser: reboot the PC and try again. Erasing removes any saved WiFi and Immich settings, as a browser install with erase does.

When esptool finishes, press RESET (or replug the display) and continue with [After Flashing](#after-flashing).

## After Flashing

When Espframe boots, connect it to WiFi and open the IP address shown on the display. Enter your Immich server URL and [API key](/api-key), then choose a [photo source](/photo-sources).

For broader setup issues, see [Troubleshooting Espframe](/troubleshooting).

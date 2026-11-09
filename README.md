# Rocket Glance

An ESP32 desk display for Dutch bus departures and a clock. OVapi updates every minute. The OLED marks departures as live or scheduled.

## Hardware

- ESP32, 128 × 64 SH1106 I²C OLED, USB power.
- OLED wiring: SDA → GPIO21, SCL → GPIO22, VCC → 3.3 V, GND → GND.
- Print one [front](cad/rocket-oled-tall-bezel-front.stl), one [rear](cad/rocket-oled-stock-rear.stl), and two [spacers](cad/rocket-oled-stock-spacer.stl). [Fusion model](cad/rocket-oled-tall-bezel-case.f3d)
- Use eight M2 screws, 6–8 mm under the head. Maximum head size is 4 mm diameter × 2 mm tall.
- Mount the OLED through the spacers with four screws. Route the cable through the relief, seat the ESP32, then fasten the rear with four screws.

## Setup

1. Edit [slides.json](firmware/config/slides.json) with the OVapi platform codes, bus lines, and slide order. Use up to eight slides.
2. Activate ESP-IDF 5.5.2, then build and flash.

   ```sh
   cd firmware
   idf.py set-target esp32
   idf.py -p PORT flash monitor
   ```

3. Join the Glance Wi-Fi network shown on the OLED. Open `http://192.168.4.1` and enter your Wi-Fi details.
4. Find `rocket-glance` in your router or the serial log to open its settings page. The device reconnects automatically on startup.

Run `firmware/tests/run.sh` with ESP-IDF activated. See [MIT license](LICENSE).

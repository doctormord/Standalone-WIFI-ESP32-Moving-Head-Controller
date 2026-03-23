# Standalone-WIFI-ESP32-Moving-Head-Controller
Standalone WIFI ESP32 Moving Head Controller

---

![BSW moving head](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/moving_head._sm.jpg)

# ESP32 Pro Fixture Console (i.e. Sheds, U'King, Fieryzeal)

A highly advanced, standalone, web-based lighting console and Art-Net node built on the ESP32. Originally tailored for 18-channel moving heads (like the SHEHDS 160W Pro), this project brings professional-grade console features—such as LFO modulators, BPM synchronization, executor playbacks, and a dedicated followspot engine—into a compact, zero-install embedded web application.

## Other names/brands
* Sheds 160W Pro
* Rudderstar 200W 3in1 LED Moving Head
* Fieryzeal 200W LED Moving Head
* Datewink 200W LED Moving Head
* U'King 200W LED Moving Head 


## 🚀 Key Features

* **Zero-Install Web Interface:** The entire UI is served directly from the ESP32's LittleFS flash memory. Accessible via any modern web browser (iOS, Android, Windows, Mac) with a highly responsive CSS-grid layout.
* **Dual Operation Modes:** Acts as a seamless Art-Net node (Universe 0) that automatically yields to internal standalone effects when activated.
* **Advanced FX Engine:** Generative, math-based movement and parameter modulation (LFOs) running at high refresh rates.
* **Global BPM Synchronization:** Unified timing engine with Tap Tempo (moving average) and real-time Audio/Mic Sync via Web Audio API (FFT analysis).
* **Non-Volatile Scene Memory:** Save up to 10 complete fixture states—including all active FX, modulators, and custom labels—directly to the ESP32's NVRAM.
* **Over-The-Air (OTA) Updates:** Flash new firmware directly via the web interface without USB cables.

---

## 🎛️ User Interface Architecture

The web application is divided into three main operational tabs, allowing for flexible live performance and deep programming.

### 1. LIVE Tab (Performance Mode)
Designed for high-stress live environments, focusing on rapid access and execution.
* **Executor Pads:** 10 visual preset buttons for instant scene recall. Displays custom preset names dynamically.
* **Bump & Flash Controls:** High-priority momentary overrides (`BLACKOUT`, `FAST STROBE`, `50% STROBE`, `BLINDER`).
* **Master Panic Button:** `KILL ALL FX / STOP SHOW` safely halts all generative modulations and returns the fixture to a static state.
* **Show Auto-Chaser:** Automated sequential or random looping through stored presets. Includes adjustable crossfade times, hold times, and BPM sync multipliers (1/1 to 1/32 beats).
* **Global Sync Hub:** Tap-tempo button with 8-beat sliding average and a `SYNC PHASE` button to manually hard-reset all LFO and chaser phases.
* **Live Joystick:** A condensed Pan/Tilt XY-pad with axis inversion and instant `CENTER` recall.

![Live Tab GUI](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_1.png)

### 2. FOLLOWSPOT Tab (Tracking Mode)
Transforms the moving head into a manually operated tracking spot with advanced physics.
* **Tracking Joystick Engine:** Features a customizable response curve (Linear to Exponential) and adjustable maximum speed for smooth, cinematic panning.
* **Axis Constraints:** Hard DMX limits (Min/Max) for Pan and Tilt to constrain the beam specifically to the stage area.
* **Smart Dimmer & Damping:** Adjustable "Smoothing" algorithm interpolates raw touch inputs into buttery-smooth DMX fades.
* **One-Shot Auto Fade:** Triggers automated fade-ins and fade-outs with configurable durations and transition curves (Linear, Sine, Quadratic).
* **Direct Beam Controls:** Quick access to Focus, Zoom, Color, Frost, and a dedicated `OPEN WHITE` panic button to clear all gobos/prisms instantly.

![Followspot Gui](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_2.png)

### 3. PROGRAMMER Tab (Setup & FX Mode)
The deep-dive configuration layer for building scenes and tweaking modulators.
* **Smart DMX Labels:** Automatically translates raw integer values into human-readable strings for complex channels (e.g., rotating gobos translate `135-255` into `[FWD]`, `[STOP]`, `[REV]` with percentage speeds).
* **Compound Dropdowns:** Merges base index selections (e.g., "Gobo 2") with continuous offset sliders (e.g., "Gobo Shake") into single UI elements.

![Programmer tab GUI](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_3.png)

---

## 🧠 Advanced FX & Modulator Engine

The core of the console relies on an object-oriented C++ backend calculating DMX values in real-time based on sine, quadratic, cubic, and gaussian algorithms.

### Movement FX
* **7 Algorithmic Shapes:** Circle, Figure 8, Clover, Square, Star, Waterwave, Lissajous (Chaos).
* **Phase Rotation:** 0–360° continuous rotation of the geometric shape.
* **Dynamic Modulators:** LFO-driven scaling of both shape *Size* and *Speed* in real-time (e.g., to create pulsing or accelerating movement paths).

### Parameter Modulators (Dimmer, Gobo Rot, Prism Rot)
* **Waveforms:** Sine (Theater soft), Linear (Even), Quadratic (Fast end), Cubic (Very fast), Gauss (Lighthouse flash), Random (Flicker).
* **Modes:** Forward (Sawtooth) or Up/Down (Ping-Pong).
* **Ranges:** Configurable Start and End DMX boundaries.
* **Timing:** Free-running manual speed or locked to Global BPM Sync with beat multipliers.

### Step-Chasers (Color & Gobos)
* **Independent Engines:** Separate chasers for the Color Wheel, Static Gobo Wheel, and Rotating Gobo Wheel.
* **Range Selection:** Configurable start and end indexes (e.g., loop only from Color 2 to Color 5).
* **FX Overlays:** Injectable physical DMX offsets, such as overlaying a "Shake/Wobble" effect while the chaser steps through the gobos.

![FX GUI](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_4.png)

![FX GUI](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_5.png)

![FX GUI](https://github.com/doctormord/Standalone-WIFI-ESP32-Moving-Head-Controller/blob/main/images/gui_v1_6.png)


---

## ⚙️ Technical Specifications

* **Microcontroller:** ESP32 (e.g., WROOM-32, ESP32-S3), actually developent on a ESP32-C3 Super mini
* **DMX Output:** Hardware UART (Serial1) via MAX485 TTL-to-RS485 transceiver.
* **Baud Rate:** 250,000 bps (Standard DMX512 protocol).
* **Storage:** * UI Assets: LittleFS (Flash memory).
    * Scene Data: ESP32 Non-Volatile Storage (Preferences API).
* **Network:** mDNS support (`http://movinghead.local`), dynamic AP fallback (SSID: `Moving_Head_Ctrl`).
* **Frontend Stack:** Vanilla HTML5, CSS3 (CSS Grid/Flexbox), Vanilla ES6 JavaScript (No external frameworks/CDNs required).

---

## 🔌 Default Fixture Profile (18-Channel Mode)
*Currently configured for SHEHDS 160W Pro. Easily adaptable via the configuration block in the source code.*

| Channel | Function | Channel | Function |
| :--- | :--- | :--- | :--- |
| **CH 1** | Dimmer | **CH 10** | Prism Insert |
| **CH 2** | Strobe | **CH 11** | Prism Rotation |
| **CH 3** | Pan | **CH 12** | Frost |
| **CH 4** | Tilt | **CH 13** | Focus |
| **CH 5** | Motor Speed | **CH 14** | Zoom |
| **CH 6** | Color Wheel | **CH 15** | Pan Fine |
| **CH 7** | Static Gobo | **CH 16** | Tilt Fine |
| **CH 8** | Rotating Gobo | **CH 17** | Auto Macros |
| **CH 9** | Gobo Index/Rot | **CH 18** | System Reset |

---

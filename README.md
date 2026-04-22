ESP32 Lottery Miner v1.1

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Language: C++](https://img.shields.io/badge/language-C%2B%2B-green.svg)](https://isocpp.org/)
[![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-red.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Build: Arduino IDE](https://img.shields.io/badge/build-Arduino%20IDE-brightgreen)](https://www.arduino.cc/en/software)

A "lottery" Bitcoin miner that turns your ESP32 into a 24/7 lottery ticket machine.  
**Dual-core SHA-256 mining** with WiFi Manager, Web Dashboard, and Auto Pool Failover.

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| ⛏️ **Dual-Core Mining** | Utilizes both ESP32 cores for parallel SHA-256 computation |
| 🔒 **TLS/SSL Support** | Supports encrypted WSS (port 4333) and plain WS (port 3333) |
| 🔄 **Auto Pool Failover** | Automatically switches to backup pools on connection loss (4 pools) |
| 🎛️ **Web Dashboard** | Real-time web interface showing hashrate, nonce, and status |
| ⚙️ **WiFi Manager** | Auto-launches AP `ESP32-Miner-Setup` (192.168.4.1) for WiFi configuration |
| 📡 **mDNS Support** | Access dashboard via `http://esp-miner.local` |
| 📦 **OTA Updates** | Upload new firmware over WiFi without USB cable |
| 🛡️ **Watchdog Timer** | Auto-reset if system hangs for more than 30 seconds |
| 💾 **Persistent Config** | Saves configuration to NVS, persists across power cycles |
| 📊 **Real-time Stats** | Live hashrate and pool status display |

---

## 📋 Prerequisites

### Required Hardware
- ESP32 development board (DevKit, NodeMCU-32S, TTGO, or any variant)
- Stable 5V power supply (1A or higher recommended)

### Required Libraries
Install via **Arduino Library Manager**:

| Library | Author |
|---------|--------|
| WiFiManager | tzapu |
| WebSocketsClient | Markus Sattler |
| ArduinoJson | Benoit Blanchon |
| ESPAsyncWebServer | me-no-dev |
| AsyncTCP | me-no-dev |
| AsyncElegantOTA | Ayush Sharma |

---

## 🚀 Quick Start

### 1. Clone the Repository
```bash
git clone https://github.com/nam348tnh3gp/ESP32-Lottery.git
cd ESP32-Lottery
```

2. Configure the Miner

Default settings (or configure via Web Dashboard):

```ini
pool_host=public-pool.io
pool_port=3333
btc_address=YourBitcoinAddress
worker_name=ESP32
```

Configuration Options

Option Description Example
pool_host Stratum pool hostname public-pool.io
pool_port Pool port (3333=TCP, 4333=TLS) 3333
btc_address Your Bitcoin address 1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa
worker_name Name for your mining rig ESP32

3. Upload to ESP32

· Open ESP_Code.ino in Arduino IDE
· Select board: ESP32 Dev Module
· Click Upload

4. First Run - WiFi Setup

· ESP32 creates AP ESP32-Miner-Setup
· Connect to this WiFi (no password)
· Open 192.168.4.1 in browser
· Enter home WiFi credentials and save

5. Run the Miner

· ESP32 reboots and connects to WiFi
· Access dashboard at http://esp-miner.local or ESP32's IP address
· Configure BTC address and start mining

---

📊 Performance Examples

Device Cores Difficulty Hashrate
ESP32-WROOM-32 2 Solo ~150 kH/s
ESP32-S3 2 Solo ~200 kH/s
ESP32-C3 1 Solo ~80 kH/s

---

🛠️ Building with Arduino IDE

Available Commands

Command Action
Ctrl+U Upload to ESP32
Ctrl+Shift+M Open Serial Monitor
Ctrl+R Verify/Compile

Custom Build Flags

Add to platformio.ini or Arduino IDE board settings:

```ini
build_flags = 
    -O3
    -flto
    -DCORE_DEBUG_LEVEL=0
```

---

📁 Project Structure

```
esp32-lottery-miner/
├── ESP_Code.ino          # Main miner implementation
├── DSHA2.h               # SHA-256 algorithm implementation
├── README.md             # This file
└── LICENSE               # MIT License
```

---

🔧 Troubleshooting

<details>
<summary><b>🔌 Connection Issues</b></summary>· Test pool connectivity: ping public-pool.io
· Check internet connection
· Verify WiFi credentials in AP mode (192.168.4.1)
· Try switching between TCP (3333) and TLS (4333)

</details><details>
<summary><b>📛 Compilation Errors</b></summary>· Verify all libraries are installed in Arduino Library Manager
· Check board selection: ESP32 Dev Module
· Ensure DSHA2.h is in the same folder as .ino file

</details><details>
<summary><b>🐌 Low Hashrate</b></summary>· WiFi.setSleep(false) is already set in code
· Increase CPU frequency to 240 MHz in board settings
· Close other network clients
· Check for thermal throttling

</details><details>
<summary><b>💥 ESP32 crashes/reboots</b></summary>· Ensure stable 5V/1A power supply
· Watchdog auto-resets after 30 seconds
· Check Serial Monitor for stack overflow errors

</details>---

📝 How It Works

```mermaid
graph LR
    A[WiFi Connect] --> B[Get Pool Config]
    B --> C[WebSocket Connect]
    C --> D[Subscribe & Auth]
    D --> E[Receive Job]
    E --> F[Dual-Core SHA-256]
    F --> G{Solution Found?}
    G -->|No| F
    G -->|Yes| H[Submit Nonce]
    H --> I[Get Feedback]
    I --> E
```

1. WiFi Connect - Connects to saved WiFi or launches AP for configuration
2. Get Pool Config - Loads pool settings from NVS preferences
3. WebSocket Connect - Establishes WebSocket connection to Stratum pool
4. Subscribe & Auth - Authenticates with <BTC_ADDRESS>.<WORKER> format
5. Receive Job - Gets block header and target from pool
6. Dual-Core SHA-256 - Both cores search nonce space in parallel
7. Submit Solution - Sends valid nonce back to pool if found
8. Repeat - Continuous mining loop with auto-failover

---

🤝 Contributing

Contributions are welcome! Here's how you can help:

1. 🍴 Fork the repository
2. 🌿 Create your feature branch (git checkout -b feature/AmazingFeature)
3. 💾 Commit your changes (git commit -m 'Add some AmazingFeature')
4. 📤 Push to the branch (git push origin feature/AmazingFeature)
5. 🔍 Open a Pull Request

---

📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

```
MIT License

Copyright (c) 2024 ESP32 Lottery Miner Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

🙏 Acknowledgments

· Bitcoin - The original cryptocurrency
· DSHA2 Implementation - SHA-256 algorithm in C++ for ESP32
· ESP32 Community - For extensive documentation and libraries
· public-pool.io - Open-source solo mining pool
· Stratum Protocol - Standardized mining protocol

---

📧 Contact & Support

· Issues: GitHub Issues
· Discussions: GitHub Discussions

---

<div align="center">⭐ Star this repository if you find it useful!

Report Bug · Request Feature

---

🍀 Good luck, and happy (lottery) mining! 🍀

May the nonce be with you!

</div>

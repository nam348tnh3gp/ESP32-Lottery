/*
 * ESP32 Lottery Miner - WiFi Manager + Stratum Client + Web Dashboard
 * Dùng DSHA256 thuần C++ để đào "xổ số" Bitcoin
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "DSHA2.h"

// ==================== CONFIG ====================
#define PREF_NAMESPACE "miner"
#define PREF_POOL_HOST "pool_host"
#define PREF_POOL_PORT "pool_port"
#define PREF_BTC_ADDR  "btc_addr"
#define PREF_WALLET    "wallet"

// Mặc định ban đầu
String poolHost = "public-pool.io";
uint16_t poolPort = 21496;
String btcAddress = "";
String walletName = "ESP32";

// ==================== GLOBALS ====================
Preferences prefs;
AsyncWebServer webServer(80);
WebSocketsClient stratumWS;
DSHA256 sha;

// Mining state
bool jobReceived = false;
uint8_t header[80];
uint8_t target[32];
String jobId;
uint32_t nonce = 0;
unsigned long hashes = 0;
unsigned long lastReport = 0;
float currentHashrate = 0.0f;
bool miningActive = false;

// Thời gian nhận job cuối
unsigned long lastJobTime = 0;

// ==================== HTML DASHBOARD ====================
const char dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Lottery Miner</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        body { font-family: Arial; background: #1a1a2e; color: #eee; padding: 20px; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin: 10px 0; }
        .value { font-size: 2em; color: #0f0; font-family: monospace; }
        .label { color: #aaa; }
        input, button { padding: 10px; margin: 5px; border-radius: 5px; border: none; }
        button { background: #0f3460; color: white; cursor: pointer; }
        button:hover { background: #e94560; }
        .status { color: #0f0; }
        .config-panel { display: none; }
    </style>
</head>
<body>
    <h1>🎰 ESP32 Lottery Miner</h1>
    
    <div class='card'>
        <span class='label'>Status:</span> 
        <span id='status' class='status'>Connecting...</span>
    </div>
    
    <div class='card'>
        <span class='label'>Hashrate:</span>
        <div id='hashrate' class='value'>0.00 H/s</div>
    </div>
    
    <div class='card'>
        <span class='label'>Total Hashes:</span>
        <div id='totalHashes' class='value'>0</div>
    </div>
    
    <div class='card'>
        <span class='label'>Current Nonce:</span>
        <div id='nonce' class='value'>0</div>
    </div>
    
    <div class='card'>
        <span class='label'>Pool:</span>
        <div id='poolInfo'>-</div>
    </div>
    
    <div class='card'>
        <span class='label'>BTC Address:</span>
        <div id='btcInfo'>-</div>
    </div>
    
    <button onclick='toggleConfig()'>⚙️ Configure</button>
    <button onclick='restartMiner()'>🔄 Restart</button>
    <button onclick='resetWiFi()'>📡 Reset WiFi</button>
    
    <div id='configPanel' class='config-panel card'>
        <h3>Miner Configuration</h3>
        <input type='text' id='poolHost' placeholder='Pool Host' value='public-pool.io'>
        <input type='number' id='poolPort' placeholder='Port' value='21496'>
        <input type='text' id='btcAddr' placeholder='BTC Address' value=''>
        <input type='text' id='walletName' placeholder='Wallet Name' value='ESP32'>
        <button onclick='saveConfig()'>💾 Save & Restart</button>
    </div>

    <script>
        function toggleConfig() {
            var p = document.getElementById('configPanel');
            p.style.display = p.style.display == 'block' ? 'none' : 'block';
        }
        
        function saveConfig() {
            var data = {
                poolHost: document.getElementById('poolHost').value,
                poolPort: document.getElementById('poolPort').value,
                btcAddr: document.getElementById('btcAddr').value,
                wallet: document.getElementById('walletName').value
            };
            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            }).then(r => r.json()).then(d => {
                alert('Saved! Restarting miner...');
                setTimeout(() => location.reload(), 2000);
            });
        }
        
        function restartMiner() {
            fetch('/api/restart');
            setTimeout(() => location.reload(), 3000);
        }
        
        function resetWiFi() {
            if(confirm('Reset WiFi settings? Device will reboot to AP mode.')) {
                fetch('/api/resetwifi');
            }
        }
        
        function fetchStats() {
            fetch('/api/stats').then(r => r.json()).then(d => {
                document.getElementById('status').innerHTML = d.status;
                document.getElementById('hashrate').innerHTML = d.hashrate + ' H/s';
                document.getElementById('totalHashes').innerHTML = d.totalHashes;
                document.getElementById('nonce').innerHTML = d.nonce;
                document.getElementById('poolInfo').innerHTML = d.pool;
                document.getElementById('btcInfo').innerHTML = d.btcAddress;
                document.getElementById('poolHost').value = d.poolHost;
                document.getElementById('poolPort').value = d.poolPort;
                document.getElementById('btcAddr').value = d.btcAddress;
                document.getElementById('walletName').value = d.wallet;
            });
        }
        
        setInterval(fetchStats, 2000);
        fetchStats();
    </script>
</body>
</html>
)rawliteral";

// ==================== UTILS ====================
uint8_t hexToByte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void hexToBytes(const char* hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        bytes[i] = (hexToByte(hex[i*2]) << 4) | hexToByte(hex[i*2 + 1]);
    }
}

void reverseBytes(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t tmp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = tmp;
    }
}

bool checkTarget(uint8_t* hash) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return false;
}

// ==================== STRATUM ====================
void stratumSend(const char* json) {
    if (stratumWS.isConnected()) {
        stratumWS.sendTXT(json);
    }
}

void stratumSubscribe() {
    StaticJsonDocument<200> doc;
    doc["id"] = 1;
    doc["method"] = "mining.subscribe";
    JsonArray params = doc.createNestedArray("params");
    params.add(walletName + "/1.0");
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
}

void stratumAuthorize() {
    StaticJsonDocument<200> doc;
    doc["id"] = 2;
    doc["method"] = "mining.authorize";
    JsonArray params = doc.createNestedArray("params");
    params.add(btcAddress);
    params.add("x");
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
}

void stratumSubmit(uint32_t foundNonce) {
    StaticJsonDocument<300> doc;
    doc["id"] = 4;
    doc["method"] = "mining.submit";
    JsonArray params = doc.createNestedArray("params");
    params.add(btcAddress);
    params.add(jobId);
    params.add("");
    params.add("");
    params.add(String(foundNonce, HEX));
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
    
    Serial.println("🎯 SUBMITTED NONCE: 0x" + String(foundNonce, HEX));
}

// ==================== STRATUM EVENT ====================
void stratumEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("❌ Pool disconnected");
            miningActive = false;
            jobReceived = false;
            break;
            
        case WStype_CONNECTED:
            Serial.println("✅ Connected to pool");
            stratumSubscribe();
            break;
            
        case WStype_TEXT:
            {
                StaticJsonDocument<1024> doc;
                DeserializationError err = deserializeJson(doc, payload, length);
                if (err) return;
                
                const char* method = doc["method"];
                if (method && strcmp(method, "mining.notify") == 0) {
                    JsonArray params = doc["params"];
                    jobId = params[0].as<String>();
                    
                    // Prev hash
                    const char* prevHashHex = params[1];
                    hexToBytes(prevHashHex, header + 4, 32);
                    reverseBytes(header + 4, 32);
                    
                    // Merkle root (tạm lấy từ param thứ 2 - pool public-pool.io trả vậy)
                    const char* merkleHex = params[2];
                    hexToBytes(merkleHex, header + 36, 32);
                    reverseBytes(header + 36, 32);
                    
                    // Version
                    const char* versionHex = params[5];
                    hexToBytes(versionHex, header, 4);
                    reverseBytes(header, 4);
                    
                    // nBits
                    const char* nbitsHex = params[6];
                    hexToBytes(nbitsHex, header + 72, 4);
                    reverseBytes(header + 72, 4);
                    
                    // nTime
                    const char* ntimeHex = params[7];
                    hexToBytes(ntimeHex, header + 68, 4);
                    reverseBytes(header + 68, 4);
                    
                    // Build target từ nbits
                    uint32_t bits;
                    memcpy(&bits, header + 72, 4);
                    bits = __builtin_bswap32(bits);
                    
                    uint32_t exp = bits >> 24;
                    uint32_t mant = bits & 0x00FFFFFF;
                    memset(target, 0, 32);
                    if (exp <= 32) {
                        target[31 - exp] = (mant >> 16) & 0xFF;
                        target[30 - exp] = (mant >> 8) & 0xFF;
                        target[29 - exp] = mant & 0xFF;
                    }
                    
                    jobReceived = true;
                    miningActive = true;
                    lastJobTime = millis();
                    Serial.println("📦 New job #" + jobId);
                }
                
                int id = doc["id"];
                if (id == 1) {
                    stratumAuthorize();
                }
                if (id == 2) {
                    bool success = doc["result"];
                    Serial.println(success ? "🔑 Authorized!" : "❌ Auth failed");
                }
                if (id == 4) {
                    bool accepted = doc["result"];
                    Serial.println(accepted ? "✅ Share accepted!" : "❌ Rejected");
                }
            }
            break;
    }
}

void stratumConnect() {
    if (!btcAddress.isEmpty()) {
        stratumWS.begin(poolHost.c_str(), poolPort, "/");
        stratumWS.onEvent(stratumEvent);
        stratumWS.setReconnectInterval(10000);
    }
}

// ==================== WEB SERVER API ====================
void setupWebServer() {
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", dashboard_html);
    });
    
    webServer.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *req) {
        StaticJsonDocument<512> doc;
        doc["status"] = miningActive ? (jobReceived ? "Mining" : "Waiting job") : "Disconnected";
        doc["hashrate"] = String(currentHashrate, 2);
        doc["totalHashes"] = nonce;
        doc["nonce"] = nonce;
        doc["pool"] = poolHost + ":" + String(poolPort);
        doc["btcAddress"] = btcAddress.isEmpty() ? "Not set" : btcAddress;
        doc["poolHost"] = poolHost;
        doc["poolPort"] = poolPort;
        doc["wallet"] = walletName;
        
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });
    
    webServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            StaticJsonDocument<256> doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (!err) {
                poolHost = doc["poolHost"] | "public-pool.io";
                poolPort = doc["poolPort"] | 21496;
                btcAddress = doc["btcAddr"] | "";
                walletName = doc["wallet"] | "ESP32";
                
                prefs.putString(PREF_POOL_HOST, poolHost);
                prefs.putUShort(PREF_POOL_PORT, poolPort);
                prefs.putString(PREF_BTC_ADDR, btcAddress);
                prefs.putString(PREF_WALLET, walletName);
                
                req->send(200, "application/json", "{\"ok\":true}");
                delay(500);
                ESP.restart();
            } else {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            }
        });
    
    webServer.on("/api/restart", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });
    
    webServer.on("/api/resetwifi", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true}");
        WiFiManager wm;
        wm.resetSettings();
        delay(500);
        ESP.restart();
    });
    
    webServer.begin();
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    
    // Load config
    prefs.begin(PREF_NAMESPACE, false);
    poolHost = prefs.getString(PREF_POOL_HOST, "public-pool.io");
    poolPort = prefs.getUShort(PREF_POOL_PORT, 21496);
    btcAddress = prefs.getString(PREF_BTC_ADDR, "");
    walletName = prefs.getString(PREF_WALLET, "ESP32");
    prefs.end();
    
    // WiFi Manager - tự mở AP "ESP32-Miner-Setup" nếu chưa có WiFi
    WiFiManager wm;
    wm.setConfigPortalTimeout(180); // 3 phút
    
    if (!wm.autoConnect("ESP32-Miner-Setup")) {
        Serial.println("Failed to connect, restarting...");
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("🌐 WiFi connected! IP: " + WiFi.localIP().toString());
    
    // Khởi động web server
    setupWebServer();
    
    // Kết nối pool
    if (!btcAddress.isEmpty()) {
        stratumConnect();
    } else {
        Serial.println("⚠️ BTC Address not set. Please configure via http://" + WiFi.localIP().toString());
    }
}

// ==================== LOOP ====================
void loop() {
    stratumWS.loop();
    
    // Mining loop
    if (jobReceived && miningActive) {
        unsigned long start = micros();
        
        // Ghi nonce vào header
        uint32_t nonceLE = nonce;
        memcpy(header + 76, &nonceLE, 4);
        
        uint8_t hash[32];
        sha.hashBlockHeader(header, hash);
        hashes++;
        
        if (checkTarget(hash)) {
            Serial.println("🏆 BLOCK FOUND! Submitting...");
            stratumSubmit(nonce);
            jobReceived = false;
        }
        
        nonce++;
        
        // Báo cáo hashrate mỗi 2 giây
        if (millis() - lastReport > 2000) {
            currentHashrate = hashes / 2.0f;
            Serial.printf("⚡ %.2f H/s | nonce: %u\n", currentHashrate, nonce);
            lastReport = millis();
            hashes = 0;
        }
        
        // Check stale job (quá 60 giây ko có job mới)
        if (millis() - lastJobTime > 60000) {
            jobReceived = false;
            miningActive = false;
            Serial.println("⏰ Job stale, reconnecting...");
            stratumWS.disconnect();
            delay(5000);
            stratumConnect();
        }
    }
    
    // Nếu mất kết nối pool, thử reconnect
    if (!stratumWS.isConnected() && !btcAddress.isEmpty() && WiFi.isConnected()) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 10000) {
            lastReconnect = millis();
            stratumConnect();
        }
    }
}

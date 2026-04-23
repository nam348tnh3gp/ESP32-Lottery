/*
 * ESP32/ESP8266 Lottery Miner v1.3 - Auto Core Detection (Final Clean)
 * No Serial override here; platformio.ini handles it for S2/C3/S3.
 */

#include <Arduino.h>

// ==================== PLATFORM DETECTION ====================
#if defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESP8266mDNS.h>
    #define PLATFORM_NAME "ESP8266"
    #define PLATFORM_SINGLE_CORE
    #define ESP8266_NO_RTOS
#elif defined(ESP32)
    #include <WiFi.h>
    #include <ESPmDNS.h>
    #include "esp_task_wdt.h"
    #include <Preferences.h>
    
    #if defined(CONFIG_FREERTOS_UNICORE) || defined(ESP32S2) || defined(ESP32C3) || defined(ESP32S3)
        #define PLATFORM_SINGLE_CORE
        #if defined(ESP32S2)
            #define PLATFORM_NAME "ESP32-S2"
        #elif defined(ESP32C3)
            #define PLATFORM_NAME "ESP32-C3"
        #elif defined(ESP32S3)
            #define PLATFORM_NAME "ESP32-S3"
        #else
            #define PLATFORM_NAME "ESP32 (Unicore)"
        #endif
    #else
        #define PLATFORM_DUAL_CORE
        #define PLATFORM_NAME "ESP32"
    #endif
#endif

// ==================== HASH WRAPPER ====================
#if defined(ESP8266)
  // ESP8266: dùng phần mềm unroll (DSHA2.h)
  #include "DSHA2.h"
#elif defined(ESP32)
  // Các dòng ESP32 có hardware SHA (S2, S3, C3, C6, H2, C2)
  #if defined(ESP32S2) || defined(ESP32S3) || defined(ESP32C3) || defined(ESP32C6) || defined(ESP32H2) || defined(ESP32C2)
    #include <mbedtls/sha256.h>
    class DSHA256 {
    public:
      void hashBlockHeader(const unsigned char header[80], unsigned char hash[32]) {
        unsigned char firstHash[32];
        mbedtls_sha256_ret(header, 80, firstHash, 0);   // lần 1
        mbedtls_sha256_ret(firstHash, 32, hash, 0);     // lần 2 (double SHA)
      }
    };
  #else
    // ESP32 gốc (ESP32‑WROOM, WROVER…) không có HW SHA → dùng phần mềm unroll
    #include "DSHA2.h"
  #endif
#else
  // Các board khác (hiếm) dùng DSHA2.h
  #include "DSHA2.h"
#endif

#include <WiFiManager.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

#ifndef DISABLE_WEB_DASHBOARD
  #ifdef ESP32
    #include <AsyncTCP.h>
  #else
    #include <ESPAsyncTCP.h>
  #endif
  #include <ESPAsyncWebServer.h>
#endif

// ==================== AUTO CORE DETECTION ====================
#ifdef SINGLE_CORE_MODE
    #warning "Single core mode forced by user"
    #ifdef PLATFORM_DUAL_CORE
        #undef PLATFORM_DUAL_CORE
        #define PLATFORM_SINGLE_CORE
    #endif
#endif

#ifdef PLATFORM_DUAL_CORE
    #define MINING_DUAL_CORE 1
    #define CORE_COUNT 2
#else
    #define MINING_DUAL_CORE 0
    #define CORE_COUNT 1
#endif

// ==================== CONFIG ====================
#define PREF_NAMESPACE "miner"
#define PREF_POOL_HOST "pool_host"
#define PREF_POOL_PORT "pool_port"
#define PREF_BTC_ADDR  "btc_addr"
#define PREF_WALLET    "wallet"
#define MDNS_NAME      "esp-miner"
#define WDT_TIMEOUT    30

#ifndef MINER_VERSION
  #define MINER_VERSION "1.3"
#endif

const char* backupPools[] = {"public-pool.io","public-pool.io","pool.vkbit.com","stratum.slushpool.com"};
const uint16_t backupPorts[] = {3333,4333,3333,3333};
const bool backupUseSSL[] = {false,true,false,false};
const int NUM_BACKUP_POOLS = 4;
int currentPoolIndex = 0;

String poolHost = "public-pool.io";
uint16_t poolPort = 3333;
String btcAddress = "";
String walletName = "ESP32";

// ==================== GLOBALS ====================
#ifdef ESP32
    Preferences prefs;
#else
    #include <EEPROM.h>
    struct Config {
        char poolHost[32] = "public-pool.io";
        uint16_t poolPort = 3333;
        char btcAddress[64] = "";
        char walletName[16] = "ESP8266";
    } config;
#endif

WebSocketsClient stratumWS;
DSHA256 sha;

#ifndef DISABLE_WEB_DASHBOARD
AsyncWebServer webServer(80);
#endif

volatile bool jobReceived = false;
uint8_t header[80];
uint8_t target[32];
String jobId;
volatile unsigned long hashesCore0 = 0;
volatile unsigned long hashesCore1 = 0;
volatile float currentHashrate = 0.0f;
volatile bool miningActive = false;
String currentIP = "";

volatile uint32_t nonceStart = 0;
volatile uint32_t nonceFound = 0;
volatile bool solutionFound = false;
volatile bool shouldStopMining = false;

unsigned long lastJobTime = 0;
unsigned long lastReport = 0;
unsigned long lastReconnectAttempt = 0;

#ifndef ESP8266_NO_RTOS
  #if CORE_COUNT == 2
      TaskHandle_t miningTask0 = NULL;
      TaskHandle_t miningTask1 = NULL;
  #else
      TaskHandle_t miningTask0 = NULL;
  #endif
  SemaphoreHandle_t submitMutex;
#endif

// ==================== FORWARD DECLARATIONS ====================
void stratumConnect();

// ==================== HTML DASHBOARD ====================
#ifndef DISABLE_WEB_DASHBOARD
const char dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP Miner v1.3 - Auto Core</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        body { font-family: Arial; background: #1a1a2e; color: #eee; padding: 20px; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin: 10px 0; }
        .value { font-size: 2em; color: #0f0; font-family: monospace; }
        .label { color: #aaa; }
        .highlight { color: #ffaa00; }
        input, select, button { padding: 10px; margin: 5px; border-radius: 5px; border: none; }
        button { background: #0f3460; color: white; cursor: pointer; }
        button:hover { background: #e94560; }
        .status { color: #0f0; }
        .config-panel { display: none; }
        .ip-box { background: #0f3460; padding: 10px; border-radius: 5px; font-family: monospace; }
        .btn-group { display: flex; flex-wrap: wrap; gap: 5px; }
        .btn-group button { flex: 1; min-width: 120px; }
        .pool-badge { display: inline-block; background: #e94560; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.8em; margin-left: 5px; }
        .tls-badge { display: inline-block; background: #00aa00; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.8em; margin-left: 5px; }
        .chip-badge { display: inline-block; background: #ff6600; color: white; padding: 2px 8px; border-radius: 12px; font-size: 0.8em; margin-left: 5px; }
        .info-note { background: #0f3460; padding: 10px; border-radius: 5px; margin-top: 10px; font-size: 0.9em; }
    </style>
</head>
<body>
    <h1>🎰 ESP Miner v1.3 <span id='chipBadge' class='chip-badge'>Auto-Detect</span></h1>
    
    <div class='card'>
        <span class='label'>🌐 Dashboard Access:</span><br>
        <div style='margin-top:10px;'>
            <span class='label'>IP Address:</span> 
            <span id='ipAddress' class='ip-box'>Loading...</span>
            <button onclick='copyIP()' style='padding:5px 10px; margin-left:10px;'>📋 Copy</button>
        </div>
        <div style='margin-top:5px;'>
            <span class='label'>mDNS:</span> 
            <span class='highlight'>http://esp-miner.local</span>
        </div>
    </div>
    
    <div class='card'>
        <span class='label'>⛏️ Status:</span> 
        <span id='status' class='status'>Connecting...</span>
    </div>
    
    <div class='card'>
        <span class='label'>⚡ Total Hashrate:</span>
        <div id='hashrate' class='value'>0.00 H/s</div>
    </div>
    
    <div id='coreStatsContainer'></div>
    
    <div class='card'>
        <span class='label'>📊 Total Hashes:</span>
        <div id='totalHashes' class='value'>0</div>
    </div>
    
    <div class='card'>
        <span class='label'>🔢 Nonce Range:</span>
        <div id='nonce' class='value'>0 - 0</div>
    </div>
    
    <div class='card'>
        <span class='label'>🖥️ Current Pool:</span>
        <div id='poolInfo' style='font-size:1.2em;'>-</div>
        <div style='margin-top:10px;'>
            <span class='label'>🔄 Backup Pools:</span>
            <div id='backupPools' style='color:#aaa; margin-top:5px;'>-</div>
        </div>
    </div>
    
    <div class='card'>
        <span class='label'>💳 BTC Address:</span>
        <div id='btcInfo' style='word-break:break-all;'>-</div>
        <div style='margin-top:10px;'>
            <span class='label'>👤 Worker:</span>
            <span id='workerInfo'>-</span>
        </div>
    </div>
    
    <div class='btn-group'>
        <button onclick='toggleConfig()'>⚙️ Configure</button>
        <button onclick='switchPool()'>🔄 Switch Pool</button>
        <button onclick='restartMiner()'>🔁 Restart</button>
        <button onclick='resetWiFi()'>📡 Reset WiFi</button>
    </div>
    
    <div id='configPanel' class='config-panel card'>
        <h3>Miner Configuration</h3>
        <label>Pool Host:</label>
        <input type='text' id='poolHost' placeholder='public-pool.io' value='public-pool.io'>
        
        <label>Port:</label>
        <select id='poolPort'>
            <option value='3333'>3333 (TCP)</option>
            <option value='4333'>4333 (TLS/SSL)</option>
        </select>
        
        <label>BTC Address:</label>
        <input type='text' id='btcAddr' placeholder='1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa' style='width:100%;'>
        
        <label>Worker Name:</label>
        <input type='text' id='walletName' placeholder='ESP32' value='ESP32'>
        
        <button onclick='saveConfig()'>💾 Save & Restart</button>
    </div>

    <script>
        let coreCount = 1;
        
        function toggleConfig() {
            var p = document.getElementById('configPanel');
            p.style.display = p.style.display == 'block' ? 'none' : 'block';
        }
        
        function copyIP() {
            var ip = document.getElementById('ipAddress').innerText;
            navigator.clipboard.writeText(ip);
            alert('IP Copied: ' + ip);
        }
        
        function saveConfig() {
            var data = {
                poolHost: document.getElementById('poolHost').value,
                poolPort: parseInt(document.getElementById('poolPort').value),
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
        
        function switchPool() {
            if(confirm('Switch to next backup pool?')) {
                fetch('/api/switchpool')
                    .then(r => r.json())
                    .then(d => {
                        alert('Switched to: ' + d.pool);
                        setTimeout(() => location.reload(), 2000);
                    });
            }
        }
        
        function restartMiner() {
            if(confirm('Restart miner?')) {
                fetch('/api/restart');
                setTimeout(() => location.reload(), 3000);
            }
        }
        
        function resetWiFi() {
            if(confirm('Reset WiFi settings? Device will reboot to AP mode.')) {
                fetch('/api/resetwifi');
            }
        }
        
        function updateCoreStats(data) {
            let container = document.getElementById('coreStatsContainer');
            if (data.coreCount != coreCount) {
                coreCount = data.coreCount;
                let html = '<div class="card"><span class="label">💻 CPU Cores:</span> ' + coreCount + '</div>';
                if (coreCount == 2) {
                    html += '<div style="display:flex; gap:10px;">' +
                        '<div class="card" style="flex:1; background:#0f3460; text-align:center;">' +
                            '<span class="label">Core 0</span>' +
                            '<div id="hashrate0" style="font-size:1.5em;color:#0f0;">0.00 H/s</div>' +
                        '</div>' +
                        '<div class="card" style="flex:1; background:#0f3460; text-align:center;">' +
                            '<span class="label">Core 1</span>' +
                            '<div id="hashrate1" style="font-size:1.5em;color:#0f0;">0.00 H/s</div>' +
                        '</div>' +
                    '</div>';
                }
                container.innerHTML = html;
            }
            if (coreCount == 2) {
                document.getElementById('hashrate0').innerHTML = data.hashrate0 + ' H/s';
                document.getElementById('hashrate1').innerHTML = data.hashrate1 + ' H/s';
            }
        }
        
        function fetchStats() {
            fetch('/api/stats').then(r => r.json()).then(d => {
                document.getElementById('status').innerHTML = d.status;
                document.getElementById('hashrate').innerHTML = d.hashrate + ' H/s';
                document.getElementById('totalHashes').innerHTML = d.totalHashes;
                document.getElementById('nonce').innerHTML = d.nonceStart + ' - ' + d.nonceEnd;
                document.getElementById('poolInfo').innerHTML = d.pool;
                document.getElementById('backupPools').innerHTML = d.backupPools;
                document.getElementById('btcInfo').innerHTML = d.btcAddress || 'Not set';
                document.getElementById('workerInfo').innerHTML = d.worker;
                document.getElementById('ipAddress').innerHTML = d.ip;
                document.getElementById('chipBadge').innerHTML = d.chip + ' (' + d.coreCount + ' cores)';
                document.getElementById('poolHost').value = d.poolHost;
                document.getElementById('poolPort').value = d.poolPort;
                document.getElementById('btcAddr').value = d.btcAddress;
                document.getElementById('walletName').value = d.wallet;
                updateCoreStats(d);
            });
        }
        
        setInterval(fetchStats, 2000);
        fetchStats();
    </script>
</body>
</html>
)rawliteral";
#endif // DISABLE_WEB_DASHBOARD

// ==================== PLATFORM-SPECIFIC UTILS ====================
#ifdef ESP8266
void loadConfig() {
    EEPROM.begin(sizeof(Config));
    EEPROM.get(0, config);
    EEPROM.end();
    poolHost = String(config.poolHost);
    poolPort = config.poolPort;
    btcAddress = String(config.btcAddress);
    walletName = String(config.walletName);
}

void saveConfigEEPROM() {
    strncpy(config.poolHost, poolHost.c_str(), 31);
    config.poolPort = poolPort;
    strncpy(config.btcAddress, btcAddress.c_str(), 63);
    strncpy(config.walletName, walletName.c_str(), 15);
    EEPROM.begin(sizeof(Config));
    EEPROM.put(0, config);
    EEPROM.commit();
    EEPROM.end();
}

void saveCurrentPool() {
    strncpy(config.poolHost, poolHost.c_str(), 31);
    config.poolPort = poolPort;
    EEPROM.begin(sizeof(Config));
    EEPROM.put(0, config);
    EEPROM.commit();
    EEPROM.end();
}
#else
void loadConfig() {
    prefs.begin(PREF_NAMESPACE, false);
    poolHost = prefs.getString(PREF_POOL_HOST, "public-pool.io");
    poolPort = prefs.getUShort(PREF_POOL_PORT, 3333);
    btcAddress = prefs.getString(PREF_BTC_ADDR, "");
    walletName = prefs.getString(PREF_WALLET, "ESP32");
    prefs.end();
}

void saveCurrentPool() {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_POOL_HOST, poolHost);
    prefs.putUShort(PREF_POOL_PORT, poolPort);
    prefs.end();
}
#endif

// ==================== SHARED UTILS ====================
uint8_t hexToByte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void hexToBytes(const char* hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        bytes[i] = (hexToByte(hex[i*2]) << 4) | hexToByte(hex[i*2+1]);
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

void switchToNextPool() {
    currentPoolIndex = (currentPoolIndex + 1) % NUM_BACKUP_POOLS;
    poolHost = backupPools[currentPoolIndex];
    poolPort = backupPorts[currentPoolIndex];
    saveCurrentPool();
    Serial.printf("🔄 Switched to pool: %s:%d (Index: %d, %s)\n", 
                 poolHost.c_str(), poolPort, currentPoolIndex,
                 backupUseSSL[currentPoolIndex] ? "TLS" : "TCP");
}

// ==================== STRATUM ====================
void stratumSend(const char* json) {
    if (stratumWS.isConnected()) {
        stratumWS.sendTXT(json);
    }
}

void stratumSubscribe() {
    JsonDocument doc;
    doc["id"] = 1;
    doc["method"] = "mining.subscribe";
    JsonArray params = doc["params"].to<JsonArray>();
    
    String userAgent = walletName + "/" + MINER_VERSION;
    params.add(userAgent);
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
    
    Serial.println("📡 Subscribing as: " + userAgent);
}

void stratumAuthorize() {
    JsonDocument doc;
    doc["id"] = 2;
    doc["method"] = "mining.authorize";
    JsonArray params = doc["params"].to<JsonArray>();
    
    String username = btcAddress + "." + walletName;
    params.add(username);
    params.add("x");
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
    
    Serial.println("🔑 Authorizing as: " + username);
}

void stratumSubmit(uint32_t foundNonce) {
    JsonDocument doc;
    doc["id"] = 4;
    doc["method"] = "mining.submit";
    JsonArray params = doc["params"].to<JsonArray>();
    
    String username = btcAddress + "." + walletName;
    params.add(username);
    params.add(jobId);
    params.add("");
    params.add("");
    params.add(String(foundNonce, HEX));
    
    String json;
    serializeJson(doc, json);
    stratumSend(json.c_str());
    
    Serial.println("🎯 SUBMITTED NONCE: 0x" + String(foundNonce, HEX));
}

// ==================== WATCHDOG ====================
void watchdogSetup() {
#ifdef ESP32
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
#endif
}

void watchdogReset() {
#ifdef ESP32
    esp_task_wdt_reset();
#else
    yield();
#endif
}

// ==================== MINING LOGIC (FREE-RTOS and NO-RTOS versions) ====================
#ifdef ESP8266_NO_RTOS
// ---------- ESP8266 (non-RTOS) ----------
void miningLoop() {
    if (!jobReceived || solutionFound || shouldStopMining) return;

    static uint32_t currentNonce = 0;
    static bool nonceInit = false;
    if (!nonceInit) {
        currentNonce = nonceStart;
        nonceInit = true;
    }

    uint32_t endNonce = nonceStart + 0x00FFFFFF;
    if (currentNonce > endNonce) {
        // finished range, reset
        currentNonce = nonceStart;
    }

    uint8_t localHeader[80];
    memcpy(localHeader, (void*)header, 80);
    
    // Process a small batch per call to keep system responsive
    int batchSize = 1000; // adjust as needed
    for (int i = 0; i < batchSize && currentNonce <= endNonce && !solutionFound && !shouldStopMining; i++) {
        uint32_t nonceLE = currentNonce;
        memcpy(localHeader + 76, &nonceLE, 4);
        
        uint8_t hash[32];
        sha.hashBlockHeader(localHeader, hash);
        hashesCore0++;
        
        if (checkTarget(hash)) {
            // Simple flag (no mutex needed on single core)
            if (!solutionFound) {
                solutionFound = true;
                nonceFound = currentNonce;
                Serial.printf("🏆 BLOCK FOUND! Nonce: 0x%08X\n", currentNonce);
            }
            break;
        }
        currentNonce++;
    }

    if (currentNonce > endNonce) {
        currentNonce = nonceStart; // wrap around
    }
}
#else
// ---------- ESP32 (FreeRTOS) ----------
void miningTask(void* parameter) {
    int coreId = (int)parameter;
    Serial.printf("⛏️ Mining task started on Core %d\n", coreId);
    
    esp_task_wdt_add(NULL);
    
    uint8_t localHeader[80];
    unsigned long* hashesCounter;
    
#if CORE_COUNT == 2
    hashesCounter = (coreId == 0) ? (unsigned long*)&hashesCore0 : (unsigned long*)&hashesCore1;
#else
    hashesCounter = (unsigned long*)&hashesCore0;
#endif
    
    while (true) {
        watchdogReset();
        
        if (shouldStopMining) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        
        if (jobReceived && !solutionFound) {
            memcpy(localHeader, (void*)header, 80);
            
            uint32_t startNonce, endNonce;
            
#if CORE_COUNT == 2
            if (coreId == 0) {
                startNonce = nonceStart;
                endNonce = nonceStart + 0x7FFFFFFF;
            } else {
                startNonce = nonceStart + 0x80000000;
                endNonce = 0xFFFFFFFF;
            }
#else
            startNonce = nonceStart;
            endNonce = nonceStart + 0x00FFFFFF;
#endif
            
            for (uint32_t n = startNonce; n <= endNonce && !solutionFound && !shouldStopMining; n++) {
                uint32_t nonceLE = n;
                memcpy(localHeader + 76, &nonceLE, 4);
                
                uint8_t hash[32];
                sha.hashBlockHeader(localHeader, hash);
                (*hashesCounter)++;
                
                if (checkTarget(hash)) {
                    if (xSemaphoreTake(submitMutex, portMAX_DELAY) == pdTRUE) {
                        if (!solutionFound) {
                            solutionFound = true;
                            nonceFound = n;
                            Serial.printf("🏆 BLOCK FOUND by Core %d! Nonce: 0x%08X\n", coreId, n);
                        }
                        xSemaphoreGive(submitMutex);
                    }
                    break;
                }
                
                if ((n & 0xFF) == 0) {
                    vTaskDelay(0);
                    watchdogReset();
                }
            }
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}
#endif

// ==================== STRATUM EVENT ====================
void stratumEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.println("❌ Pool disconnected: " + poolHost);
            miningActive = false;
            jobReceived = false;
            shouldStopMining = true;
            switchToNextPool();
            delay(3000);
            stratumConnect();
            break;
            
        case WStype_CONNECTED:
            Serial.println("✅ Connected to pool: " + poolHost + ":" + String(poolPort));
            stratumSubscribe();
            break;
            
        case WStype_TEXT:
            {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload, length);
                if (err) {
                    Serial.printf("⚠️ JSON parse error: %s\n", err.c_str());
                    return;
                }
                
                const char* method = doc["method"];
                if (method && strcmp(method, "mining.notify") == 0) {
                    JsonArray params = doc["params"];
                    jobId = params[0].as<String>();
                    
                    const char* prevHashHex = params[1];
                    hexToBytes(prevHashHex, header + 4, 32);
                    reverseBytes(header + 4, 32);
                    
                    const char* merkleHex = params[3];
                    hexToBytes(merkleHex, header + 36, 32);
                    reverseBytes(header + 36, 32);
                    
                    const char* versionHex = params[5];
                    hexToBytes(versionHex, header, 4);
                    reverseBytes(header, 4);
                    
                    const char* nbitsHex = params[6];
                    hexToBytes(nbitsHex, header + 72, 4);
                    reverseBytes(header + 72, 4);
                    
                    const char* ntimeHex = params[7];
                    hexToBytes(ntimeHex, header + 68, 4);
                    reverseBytes(header + 68, 4);
                    
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
                    
                    solutionFound = false;
                    shouldStopMining = false;
                    nonceStart = 0;
                    hashesCore0 = 0;
                    hashesCore1 = 0;
                    
                    jobReceived = true;
                    miningActive = true;
                    lastJobTime = millis();
                    Serial.println("📦 New job #" + jobId + " from " + poolHost);
                }
                
                int id = doc["id"];
                if (id == 1) {
                    stratumAuthorize();
                }
                if (id == 2) {
                    bool success = doc["result"];
                    if (!success) {
                        Serial.println("❌ Auth failed on " + poolHost);
                        switchToNextPool();
                        stratumWS.disconnect();
                        delay(3000);
                        stratumConnect();
                    } else {
                        Serial.println("🔑 Authorized on " + poolHost);
                    }
                }
                if (id == 4) {
                    bool accepted = doc["result"];
                    if (accepted) {
                        Serial.println("✅ Share accepted by " + poolHost);
                    } else {
                        Serial.println("❌ Share rejected by " + poolHost);
                    }
                }
            }
            break;
            
        case WStype_ERROR:
            Serial.println("❌ WebSocket error on " + poolHost);
            break;
    }
}

// ==================== STRATUM CONNECT ====================
void stratumConnect() {
    if (!btcAddress.isEmpty()) {
        bool useSSL = (poolPort == 4333 || poolPort == 4334 || poolPort == 443);
        
        Serial.printf("🔌 Connecting to %s:%d (%s)...\n", 
                     poolHost.c_str(), poolPort, useSSL ? "WSS" : "WS");
        
        if (useSSL) {
            stratumWS.beginSSL(poolHost.c_str(), poolPort, "/");
        } else {
            stratumWS.begin(poolHost.c_str(), poolPort, "/");
        }
        
        stratumWS.onEvent(stratumEvent);
        stratumWS.setReconnectInterval(10000);
    }
}

// ==================== WEB SERVER ====================
#ifndef DISABLE_WEB_DASHBOARD
void setupWebServer() {
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", dashboard_html);
    });
    
    webServer.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        
        if (!miningActive) {
            doc["status"] = "🔴 Disconnected";
        } else if (!jobReceived) {
            doc["status"] = "🟡 Waiting job";
        } else if (solutionFound) {
            doc["status"] = "🏆 Solution found!";
        } else {
            doc["status"] = "🟢 Mining";
        }
        
        float hr0 = hashesCore0 / 2.0f;
        float hr1 = hashesCore1 / 2.0f;
        
        doc["hashrate"] = String(hr0 + hr1, 2);
        doc["hashrate0"] = String(hr0, 2);
        doc["hashrate1"] = String(hr1, 2);
        doc["totalHashes"] = String(hashesCore0 + hashesCore1);
        doc["nonceStart"] = "0x" + String(nonceStart, HEX);
        doc["nonceEnd"] = "0x" + String(nonceStart + 0x00FFFFFF, HEX);
        doc["coreCount"] = CORE_COUNT;
        doc["chip"] = PLATFORM_NAME;
        
        bool currentIsSSL = (poolPort == 4333);
        doc["pool"] = poolHost + ":" + String(poolPort) + 
                     (currentIsSSL ? " <span class='tls-badge'>🔒 TLS</span>" : " <span class='pool-badge'>🔓 TCP</span>");
        doc["btcAddress"] = btcAddress;
        doc["worker"] = btcAddress.isEmpty() ? "-" : (btcAddress + "." + walletName);
        doc["poolHost"] = poolHost;
        doc["poolPort"] = poolPort;
        doc["wallet"] = walletName;
        doc["ip"] = currentIP;
        doc["version"] = MINER_VERSION;
        
        String backupList = "";
        for (int i = 0; i < NUM_BACKUP_POOLS; i++) {
            if (i > 0) backupList += " → ";
            if (i == currentPoolIndex) {
                backupList += "<span class='pool-badge'>ACTIVE</span> ";
            }
            backupList += String(backupPools[i]) + ":" + String(backupPorts[i]);
            backupList += backupUseSSL[i] ? " <span class='tls-badge'>🔒</span>" : " 🔓";
        }
        doc["backupPools"] = backupList;
        
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });
    
    webServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (!err) {
                poolHost = doc["poolHost"].as<String>();
                poolPort = doc["poolPort"].as<uint16_t>();
                btcAddress = doc["btcAddr"].as<String>();
                walletName = doc["wallet"].as<String>();
                
                if (poolHost.isEmpty()) poolHost = "public-pool.io";
                if (poolPort == 0) poolPort = 3333;
                
#ifdef ESP32
                prefs.begin(PREF_NAMESPACE, false);
                prefs.putString(PREF_POOL_HOST, poolHost);
                prefs.putUShort(PREF_POOL_PORT, poolPort);
                prefs.putString(PREF_BTC_ADDR, btcAddress);
                prefs.putString(PREF_WALLET, walletName);
                prefs.end();
#else
                saveConfigEEPROM();
#endif
                
                req->send(200, "application/json", "{\"ok\":true}");
                delay(500);
                ESP.restart();
            } else {
                req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            }
        });
    
    webServer.on("/api/switchpool", HTTP_GET, [](AsyncWebServerRequest *req) {
        switchToNextPool();
        
        JsonDocument doc;
        doc["ok"] = true;
        doc["pool"] = poolHost + ":" + String(poolPort);
        
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
        
        shouldStopMining = true;
        stratumWS.disconnect();
        delay(2000);
        stratumConnect();
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
#endif // DISABLE_WEB_DASHBOARD

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n╔══════════════════════════════════════════╗");
    Serial.println("║   🎰 ESP Lottery Miner v" + String(MINER_VERSION) + "           ║");
    Serial.println("║   Platform: " + String(PLATFORM_NAME));
    Serial.print("║   Cores: " + String(CORE_COUNT) + " (");
    Serial.print(CORE_COUNT == 2 ? "Dual" : "Single");
    Serial.println("-core)            ║");
    Serial.println("╚══════════════════════════════════════════╝\n");
    
    watchdogSetup();
    
#ifndef ESP8266_NO_RTOS
    submitMutex = xSemaphoreCreateMutex();
#endif
    
    loadConfig();
    
    for (int i = 0; i < NUM_BACKUP_POOLS; i++) {
        if (poolHost == backupPools[i] && poolPort == backupPorts[i]) {
            currentPoolIndex = i;
            break;
        }
    }
    
    Serial.printf("📋 Config - Pool: %s:%d (%s)\n", 
                 poolHost.c_str(), poolPort, 
                 (poolPort == 4333) ? "TLS" : "TCP");
    Serial.printf("💳 BTC: %s\n", btcAddress.isEmpty() ? "Not set" : btcAddress.c_str());
    Serial.printf("👤 Worker: %s\n", btcAddress.isEmpty() ? "-" : (btcAddress + "." + walletName).c_str());
    
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    
    if (!wm.autoConnect("ESP-Miner-Setup")) {
        Serial.println("Failed to connect, restarting...");
        delay(3000);
        ESP.restart();
    }
    
    WiFi.setSleep(false);
    currentIP = WiFi.localIP().toString();
    
    Serial.println("\n✅ WiFi connected!");
    Serial.println("🌐 IP: " + currentIP);
    Serial.println("📶 RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("📡 mDNS: http://" + String(MDNS_NAME) + ".local");
    }
    
#ifndef DISABLE_WEB_DASHBOARD
    setupWebServer();
#else
    Serial.println("⚠️ Web Dashboard disabled (Lite mode)");
#endif
    
    // Create mining tasks (only if FreeRTOS available)
#ifdef ESP8266_NO_RTOS
    Serial.println("⚡ Single-core mining (non-RTOS loop)");
#else
  #if CORE_COUNT == 2
    xTaskCreatePinnedToCore(miningTask, "MiningTask0", 10240, (void*)0, 1, &miningTask0, 0);
    xTaskCreatePinnedToCore(miningTask, "MiningTask1", 10240, (void*)1, 1, &miningTask1, 1);
    Serial.println("⚡ Dual-core mining (10KB stack each)");
  #else
    xTaskCreate(miningTask, "MiningTask", 8192, (void*)0, 1, &miningTask0);
    Serial.println("⚡ Single-core mining (8KB stack)");
  #endif
#endif
    
    Serial.println("\n================================================");
#ifndef DISABLE_WEB_DASHBOARD
    Serial.println("🔧 Dashboard: http://" + currentIP);
#endif
    Serial.println("🔄 Auto pool failover: ENABLED (4 pools)");
    Serial.println("🔒 TLS: " + String(poolPort == 4333 ? "ENABLED" : "DISABLED"));
    Serial.println("🛡️ WDT: " + String(WDT_TIMEOUT) + "s");
    Serial.println("================================================\n");
    
    if (!btcAddress.isEmpty()) {
        stratumConnect();
    } else {
        Serial.println("⚠️ BTC Address not set. Configure at: http://" + currentIP);
    }
}

// ==================== LOOP ====================
void loop() {
    watchdogReset();
    stratumWS.loop();
    
#ifndef ESP8266_NO_RTOS
    // FreeRTOS version: tasks handle mining, loop just handles reporting
    if (solutionFound) {
        stratumSubmit(nonceFound);
        solutionFound = false;
        jobReceived = false;
        shouldStopMining = true;
    }
#else
    // Non-RTOS version: run mining directly
    miningLoop();
    if (solutionFound) {
        stratumSubmit(nonceFound);
        solutionFound = false;
        jobReceived = false;
        shouldStopMining = true;
    }
#endif
    
    if (millis() - lastReport > 2000) {
        float hr0 = hashesCore0 / 2.0f;
        float hr1 = hashesCore1 / 2.0f;
        currentHashrate = hr0 + hr1;
        
        if (miningActive && jobReceived) {
            Serial.printf("⚡ %.2f H/s (C0: %.2f, C1: %.2f) | Nonce: 0x%08X | %s\n", 
                         currentHashrate, hr0, hr1, nonceStart, poolHost.c_str());
        }
        
        lastReport = millis();
        hashesCore0 = 0;
        hashesCore1 = 0;
        nonceStart += 0x01000000;
        
        if (nonceStart > 0xFF000000) {
            nonceStart = 0;
        }
    }
    
    if (jobReceived && (millis() - lastJobTime > 60000)) {
        jobReceived = false;
        miningActive = false;
        shouldStopMining = true;
        Serial.println("⏰ Job stale, reconnecting...");
        stratumWS.disconnect();
        delay(5000);
        stratumConnect();
    }
    
    if (!stratumWS.isConnected() && !btcAddress.isEmpty() && WiFi.isConnected()) {
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.println("🔄 Reconnecting...");
            stratumConnect();
        }
    }
    
    delay(10);
}

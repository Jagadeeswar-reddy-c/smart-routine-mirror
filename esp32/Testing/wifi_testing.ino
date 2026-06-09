#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include <stdlib.h>

const char* authModeToStr(wifi_auth_mode_t m) {
#if defined(WIFI_AUTH_OPEN)
  switch (m) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
#if defined(WIFI_AUTH_WPA_WPA2_PSK)
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
#endif
#if defined(WIFI_AUTH_WPA2_ENTERPRISE)
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
#endif
#if defined(WIFI_AUTH_WPA3_PSK)
    case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
#endif
#if defined(WIFI_AUTH_WPA2_WPA3_PSK)
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
#endif
    default: return "UNKNOWN";
  }
#else
  (void)m; return "UNKNOWN";
#endif
}

const char* wifiStatusToStr(wl_status_t s) {
  switch (s) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

const char* ssid     = "Jagadeeswar Reddy’s iPhone";
const char* password = "123456780";

const char* wifiDisconnectReasonText(uint8_t reason) {
  switch (reason) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE (wrong password or AP rejected auth)";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE (association timed out)";
    case 5:   return "ASSOC_TOOMANY";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT (AP out of range or powered off)";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "UNKNOWN";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("WiFi disconnect reason: %d — %s\n",
                  reason, wifiDisconnectReasonText(reason));
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("XIAO ESP32S3 WiFi Test Starting...");

  // Turn off first to clear any dirty stack state from a previous boot.
  // Skipping this causes "netstack cb reg failed" on XIAO ESP32-S3.
  WiFi.mode(WIFI_OFF);
  // Give the driver a bit more time to tear down before reinitializing.
  delay(300);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(onWiFiEvent);

  // Scan for diagnostics only — result is NOT used to lock BSSID/channel.
  // Passing BSSID+channel to WiFi.begin() causes AUTH_EXPIRE on XIAO ESP32-S3.
  WiFi.disconnect(true);
  delay(200);
  Serial.println("Scanning for available WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.printf("%d networks found:\n", n);
    for (int i = 0; i < n; ++i) {
      String auth;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:            auth = "OPEN";            break;
        case WIFI_AUTH_WEP:             auth = "WEP";             break;
        case WIFI_AUTH_WPA_PSK:         auth = "WPA_PSK";         break;
        case WIFI_AUTH_WPA2_PSK:        auth = "WPA2_PSK";        break;
        case WIFI_AUTH_WPA_WPA2_PSK:    auth = "WPA_WPA2_PSK";    break;
        case WIFI_AUTH_WPA2_ENTERPRISE: auth = "WPA2_ENTERPRISE";  break;
        default:                        auth = "OTHER";            break;
      }
      bool isTarget = (WiFi.SSID(i) == String(ssid));
      Serial.printf("%s %d: %-22s  %4d dBm  %s\n",
                    isTarget ? ">>" : "  ",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    auth.c_str());
      if (isTarget) {
        // Extra diagnostics for the target SSID
        Serial.printf("  -> Target details: enc=%d (%s)  BSSID=%s  channel=%d\n",
                      WiFi.encryptionType(i), auth.c_str(), WiFi.BSSIDstr(i).c_str(), WiFi.channel(i));
      }
    }
  }
  WiFi.scanDelete();

  // Extra low-level scan diagnostics using esp_wifi to get full ap records.
  {
    uint16_t ap_num = 0;
    esp_err_t r = esp_wifi_scan_get_ap_num(&ap_num);
    Serial.printf("esp_wifi_scan_get_ap_num -> %d, ap_num=%d\n", r, ap_num);
    if (ap_num > 0) {
      wifi_ap_record_t *ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * ap_num);
      if (ap_records) {
        r = esp_wifi_scan_get_ap_records(&ap_num, ap_records);
        Serial.printf("esp_wifi_scan_get_ap_records -> %d (returned %d records)\n", r, ap_num);
        for (int i = 0; i < ap_num; ++i) {
          String ss = String((char*)ap_records[i].ssid);
          if (ss == String(ssid)) {
            Serial.printf("LOW-LEVEL: Found target AP record:\n");
            Serial.printf("  ssid: %s\n", ap_records[i].ssid);
            Serial.printf("  authmode (numeric): %d (%s)\n", ap_records[i].authmode, authModeToStr(ap_records[i].authmode));
            Serial.printf("  primary channel: %d\n", ap_records[i].primary);
            Serial.printf("  rssi: %d dBm\n", ap_records[i].rssi);
            Serial.printf("  bssid: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                          ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5]);
          }
        }
        free(ap_records);
      } else {
        Serial.println("Failed to allocate ap_records buffer");
      }
    }
  }

  // Connection attempt with diagnostics and retries. This helps recover
  // from transient driver/init failures like "netstack cb reg failed".
  Serial.printf("\nConnecting to: %s\n", ssid);
  Serial.printf("  Passphrase length: %d\n", (int)strlen(password));

  const int maxAttempts = 3;
  bool connected = false;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    Serial.printf("Attempt %d/%d\n", attempt, maxAttempts);
    // Print lightweight diagnostics
    Serial.printf("  Free heap: %u bytes\n", ESP.getFreeHeap());
#if defined(CONFIG_FREERTOS_HZ)
    Serial.printf("  Task stack high water mark: %u\n", uxTaskGetStackHighWaterMark(NULL));
#endif

    // Ensure clean state before each attempt
    WiFi.disconnect(true);
    delay(200);

    if (password[0] == '\0') {
      Serial.println("  Note: empty password — attempting open network join");
      WiFi.begin(ssid);
    } else {
      WiFi.begin(ssid, password);
    }

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 40) {
      delay(500);
      Serial.print(".");
      retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    } else {
      Serial.println("  Attempt failed — will retry after a short delay.");
      delay(500);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected successfully!");
    Serial.printf("IP Address:  %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI:        %d dBm\n", WiFi.RSSI());
    Serial.printf("Channel:     %d\n", WiFi.channel());
    Serial.printf("Gateway:     %s\n", WiFi.gatewayIP().toString().c_str());
  } else {
    Serial.println("WiFi connection FAILED.");
    int st = (int)WiFi.status();
    Serial.printf("Status code: %d (%s)\n", st, wifiStatusToStr((wl_status_t)st));
    WiFi.printDiag(Serial);
    Serial.println();
    Serial.println("Checklist:");
    Serial.println("  1. Is the SSID/password correct?");
    Serial.println("  2. Is the AP on 2.4 GHz? (ESP32 does not support 5 GHz)");
    Serial.println("  3. If a phone hotspot — did a prompt appear on the phone?");
    Serial.println("  4. Is the AP's client limit full?");
    Serial.printf("  5. Whitelist this MAC on the AP if MAC filtering is on: %s\n",
                  WiFi.macAddress().c_str());
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected — IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("WiFi disconnected.");
  }
  delay(5000);
}

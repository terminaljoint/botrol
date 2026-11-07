#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HijelHID_BLEKeyboard.h>

// BOTROL ESP32: cloud-paired generic BLE-HID executor.
// Pairing codes are short-lived and exchanged for a per-device credential.
// The credential is stored in ESP32 NVS and never printed to USB serial.

const char* BOTROL_SERVER = "https://botrol-cloud.onrender.com";
const char* FIRMWARE_VERSION = "1.0.0";
const unsigned long POLL_INTERVAL_MS = 5000;
unsigned long lastPollAt = 0;

WiFiClientSecure secureClient;
HijelHID_BLEKeyboard keyboard("BOTROL");
Preferences preferences;
String wifiSsid;
String wifiPassword;
String deviceToken;
String deviceId;
String serialLine;
bool commandInProgress = false;
bool cloudPaired = false;

static const char BOTROL_ROOT_CA[] PROGMEM = R"BOTROL_CA(
-----BEGIN CERTIFICATE-----
MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX
MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE
CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx
NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT
GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0
MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube
Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e
WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd
BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd
BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN
l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw
Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v
Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG
SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ
odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY
+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs
kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep
8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1
vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl
-----END CERTIFICATE-----
)BOTROL_CA";

String makeDeviceId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  return "BOTROL-" + mac;
}

void emitStatus(const char* state, const char* detail = "") {
  StaticJsonDocument<384> doc;
  doc["type"] = "status";
  doc["state"] = state;
  doc["detail"] = detail;
  doc["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  doc["cloud"] = cloudPaired ? "registered" : "not_registered";
  serializeJson(doc, Serial);
  Serial.println();
}

bool httpsRequest(const String& method, const String& path,
                  const String& body, String& response,
                  bool authenticated = true) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  secureClient.setCACert(BOTROL_ROOT_CA);
  if (!http.begin(secureClient, String(BOTROL_SERVER) + path)) return false;
  http.setTimeout(15000);
  if (authenticated) {
    if (deviceToken.length() == 0) {
      http.end();
      return false;
    }
    http.addHeader("Authorization", String("Bearer ") + deviceToken);
  }
  http.addHeader("Content-Type", "application/json");
  int status = method == "GET" ? http.GET() : http.POST(body);
  response = status > 0 ? http.getString() : "";
  Serial.printf("[HTTPS] %d\n", status);
  if (status <= 0) Serial.println(http.errorToString(status));
  http.end();
  return status >= 200 && status < 300;
}

bool pairWithCode(const String& code) {
  if (WiFi.status() != WL_CONNECTED) {
    emitStatus("cloud_pair_failed", "Connect Wi-Fi before pairing.");
    return false;
  }
  StaticJsonDocument<256> requestDoc;
  requestDoc["pairing_code"] = code;
  requestDoc["device_id"] = deviceId;
  requestDoc["firmware_version"] = FIRMWARE_VERSION;
  String body, response;
  serializeJson(requestDoc, body);
  if (!httpsRequest("POST", "/api/v1/devices/pair", body, response, false)) {
    emitStatus("cloud_pair_failed", "Cloud rejected pairing or could not be reached.");
    return false;
  }
  StaticJsonDocument<768> responseDoc;
  if (deserializeJson(responseDoc, response)) {
    emitStatus("cloud_pair_failed", "Invalid pairing response from cloud.");
    return false;
  }
  const char* issuedToken = responseDoc["device_token"] | "";
  if (strlen(issuedToken) < 24) {
    emitStatus("cloud_pair_failed", "Cloud response did not contain a valid device credential.");
    return false;
  }
  deviceToken = String(issuedToken);
  preferences.begin("botrol", false);
  preferences.putString("device_token", deviceToken);
  preferences.putString("device_id", deviceId);
  preferences.end();
  cloudPaired = true;
  emitStatus("cloud_registered", "ESP32 paired. Unique device credential saved securely in NVS.");
  return true;
}

void reportResult(const String& commandId, const char* status, const String& message) {
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["message"] = message;
  String body, response;
  serializeJson(doc, body);
  String path = String("/api/v1/devices/") + deviceId +
                "/commands/" + commandId + "/result";
  if (!httpsRequest("POST", path, body, response))
    Serial.println("[BOTROL] Could not report execution result.");
}

bool validKeyUsage(int code) {
  return (code >= 4 && code <= 39) || (code >= 40 && code <= 44) ||
         (code >= 79 && code <= 82);
}

bool executeHidSequence(JsonArray events, String& result) {
  if (!keyboard.isPaired()) {
    result = "BOTROL BLE keyboard is not paired with the computer.";
    return false;
  }
  if (events.isNull() || events.size() == 0 || events.size() > 12) {
    result = "Invalid HID event sequence length.";
    return false;
  }
  for (JsonObject event : events) {
    const char* type = event["type"] | "";
    if (strcmp(type, "key") == 0) {
      if (event.size() != 3 || !event["code"].is<int>() ||
          !event["modifiers"].is<int>()) {
        result = "Malformed key event.";
        return false;
      }
      int code = event["code"].as<int>();
      int modifiers = event["modifiers"].as<int>();
      if (!validKeyUsage(code) || modifiers < 0 || modifiers > 15) {
        result = "Key event outside generic HID bounds.";
        return false;
      }
      keyboard.tap((uint8_t)code, (uint8_t)modifiers);
    } else if (strcmp(type, "text") == 0) {
      if (event.size() != 2 || !event["text"].is<const char*>()) {
        result = "Malformed text event.";
        return false;
      }
      const char* value = event["text"];
      size_t length = strlen(value);
      if (length == 0 || length > 80) {
        result = "Text event length outside bounds.";
        return false;
      }
      for (size_t i = 0; i < length; i++) {
        if (value[i] < 32 || value[i] > 126) {
          result = "Text event must contain printable ASCII only.";
          return false;
        }
      }
      keyboard.print(value);
    } else if (strcmp(type, "delay") == 0) {
      if (event.size() != 2 || !event["ms"].is<int>()) {
        result = "Malformed delay event.";
        return false;
      }
      int duration = event["ms"].as<int>();
      if (duration < 0 || duration > 2000) {
        result = "Delay event outside bounds.";
        return false;
      }
      delay((unsigned long)duration);
    } else {
      result = "Unknown generic HID event type.";
      return false;
    }
    delay(35);
  }
  result = "Generic HID event sequence executed.";
  return true;
}

void processCommand(JsonObject command) {
  String commandId = command["command_id"] | "";
  if (commandId.length() == 0 || commandId.length() > 64) return;
  if (String(command["command"] | "") != "hid_sequence") {
    reportResult(commandId, "failed", "Unsupported protocol operation.");
    return;
  }
  JsonObject payload = command["payload"].as<JsonObject>();
  if (payload.isNull() || payload.size() != 1 || !payload["events"].is<JsonArray>()) {
    reportResult(commandId, "failed", "Invalid generic HID sequence payload.");
    return;
  }
  String result;
  bool ok = executeHidSequence(payload["events"].as<JsonArray>(), result);
  reportResult(commandId, ok ? "completed" : "failed", result);
}

void pollCommands() {
  if (commandInProgress || !cloudPaired || WiFi.status() != WL_CONNECTED) return;
  commandInProgress = true;
  String path = String("/api/v1/devices/") + deviceId + "/commands/next";
  String response;
  if (httpsRequest("GET", path, "", response)) {
    StaticJsonDocument<3072> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) Serial.println("[BOTROL] Invalid cloud response JSON.");
    else if (!doc["command"].isNull())
      processCommand(doc["command"].as<JsonObject>());
  }
  commandInProgress = false;
}

void connectWiFi() {
  if (wifiSsid.length() == 0) {
    emitStatus("setup_required", "Connect over USB and configure Wi-Fi from the BOTROL setup page.");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 25000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
    emitStatus("wifi_connected", "Wi-Fi connection established.");
  } else {
    emitStatus("wifi_failed", "Could not connect. Check the Wi-Fi name and password.");
  }
}

void handleSerialCommand(const String& line) {
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, line)) {
    emitStatus("error", "Invalid USB serial JSON.");
    return;
  }
  const char* type = doc["type"] | "";
  if (strcmp(type, "get_status") == 0) {
    emitStatus("device_status");
  } else if (strcmp(type, "wifi_config") == 0) {
    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(password) > 63) {
      emitStatus("error", "Invalid Wi-Fi credentials.");
      return;
    }
    wifiSsid = String(ssid);
    wifiPassword = String(password);
    preferences.begin("botrol", false);
    preferences.putString("wifi_ssid", wifiSsid);
    preferences.putString("wifi_password", wifiPassword);
    preferences.end();
    emitStatus("credentials_received", "Wi-Fi settings saved to ESP32 NVS.");
    WiFi.disconnect(true);
    delay(300);
    connectWiFi();
  } else if (strcmp(type, "pair") == 0) {
    const char* code = doc["pairing_code"] | "";
    if (strlen(code) < 6 || strlen(code) > 12) {
      emitStatus("cloud_pair_failed", "Invalid pairing code.");
      return;
    }
    pairWithCode(String(code));
  } else {
    emitStatus("error", "Unknown USB serial command.");
  }
}

void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') {
      String line = serialLine;
      serialLine = "";
      line.trim();
      if (line.length()) handleSerialCommand(line);
    } else if (ch != '\r') {
      if (serialLine.length() < 1024) serialLine += ch;
      else serialLine = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  preferences.begin("botrol", true);
  wifiSsid = preferences.getString("wifi_ssid", "");
  wifiPassword = preferences.getString("wifi_password", "");
  deviceToken = preferences.getString("device_token", "");
  preferences.end();

  WiFi.mode(WIFI_STA);
  deviceId = makeDeviceId();
  cloudPaired = deviceToken.length() > 0;
  keyboard.begin();

  Serial.println("\n[BOTROL] Secure multi-user device firmware v1.0.0");
  Serial.print("[BOTROL] Device ID: ");
  Serial.println(deviceId);
  Serial.println("[BLE] Pair the device named BOTROL with your computer.");
  if (wifiSsid.length()) connectWiFi();
  else emitStatus("setup_required", "Connect over USB and configure Wi-Fi from the BOTROL setup page.");
  if (cloudPaired) emitStatus("cloud_registered", "Stored device credential loaded.");
}

void loop() {
  pollSerial();
  if (WiFi.status() != WL_CONNECTED && wifiSsid.length() && millis() % 30000 < 25) {
    connectWiFi();
  }
  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = millis();
    pollCommands();
  }
  delay(10);
}

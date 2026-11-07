#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HijelHID_BLEKeyboard.h>

// BOTROL ESP32: generic BLE-HID hardware executor.
// Task names, application choices, intent parsing and action planning belong in the cloud.
// This firmware knows only generic HID events: key, text and bounded delay.

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* BOTROL_SERVER = "https://botrol-cloud.onrender.com";
const char* DEVICE_ID = "BOTROL-ESP32-001";
const char* DEVICE_TOKEN = "YOUR_BOTROL_DEVICE_TOKEN";
const char* FIRMWARE_VERSION = "0.4.0";

const unsigned long POLL_INTERVAL_MS = 5000;
unsigned long lastPollAt = 0;
WiFiClientSecure secureClient;
HijelHID_BLEKeyboard keyboard("BOTROL");
bool commandInProgress = false;

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

bool httpsRequest(const String& method, const String& path,
                  const String& body, String& response) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  secureClient.setCACert(BOTROL_ROOT_CA);
  if (!http.begin(secureClient, String(BOTROL_SERVER) + path)) return false;
  http.setTimeout(10000);
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int status = method == "GET" ? http.GET() : http.POST(body);
  response = status > 0 ? http.getString() : "";
  Serial.printf("[HTTPS] %d\n", status);
  if (status <= 0) Serial.println(http.errorToString(status));
  http.end();
  return status >= 200 && status < 300;
}

bool registerDevice() {
  StaticJsonDocument<192> doc;
  doc["device_id"] = DEVICE_ID;
  doc["firmware_version"] = FIRMWARE_VERSION;
  String body, response;
  serializeJson(doc, body);
  bool ok = httpsRequest("POST", "/api/v1/devices/register", body, response);
  Serial.println(ok ? "[BOTROL] Registered with cloud." : "[BOTROL] Registration failed.");
  return ok;
}

void reportResult(const String& commandId, const char* status, const String& message) {
  StaticJsonDocument<256> doc;
  doc["status"] = status;
  doc["message"] = message;
  String body, response;
  serializeJson(doc, body);
  String path = String("/api/v1/devices/") + DEVICE_ID +
                "/commands/" + commandId + "/result";
  if (!httpsRequest("POST", path, body, response))
    Serial.println("[BOTROL] Could not report execution result.");
}

// USB/BLE HID usage IDs are generic keyboard usages, not task identifiers.
bool validKeyUsage(int code) {
  return (code >= 4 && code <=  39) || (code >= 40 && code <= 44) ||
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
  if (commandId.length() == 0 || commandId.length() > 64) {
    Serial.println("[BOTROL] Rejected command with invalid ID.");
    return;
  }
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
  if (commandInProgress || WiFi.status() != WL_CONNECTED) return;
  commandInProgress = true;
  String path = String("/api/v1/devices/") + DEVICE_ID + "/commands/next";
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
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 25000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected: ");
    Serial.println(WiFi.localIP());
  } else Serial.println("[WiFi] Connection timed out.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[BOTROL] Generic ESP32 cloud HID executor v0.4.0");
  keyboard.begin();
  Serial.println("[BLE] Pair the device named BOTROL with Windows.");
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) registerDevice();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) registerDevice();
  }
  if (millis() - lastPollAt >= POLL_INTERVAL_MS) {
    lastPollAt = millis();
    pollCommands();
  }
  delay(20);
}

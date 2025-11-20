#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- CONFIGURE THESE ---
const char* ssid = "WI-UC1";       // <-- set your Wi-Fi SSID
const char* password = "UbiComp4"; // <-- set your Wi-Fi password
// ------------------------

WebServer server(80);

// Simple single-file web UI embedded in program memory
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>F1 Fahrer Suche (ESP32)</title>
  <style>
    body{font-family:Segoe UI, Roboto, Arial; max-width:900px;margin:18px auto;padding:0 12px;color:#222}
    form{display:flex;gap:8px;margin-bottom:12px}
    input[type=text]{flex:1;padding:8px;border:1px solid #ccc;border-radius:4px}
    button{padding:8px 12px;border:none;background:#0b67d0;color:#fff;border-radius:4px;cursor:pointer}
    .card{border:1px solid #e0e0e0;border-radius:6px;padding:12px;margin-bottom:10px}
    pre{background:#f7f7f7;padding:12px;border-radius:6px;overflow:auto}
  </style>
</head>
<body>
  <h1>F1 Fahrer Suche (ESP32)</h1>
  <p>Gib einen Fahrernamen ein (z.B. <em>verstappen</em>) und klicke Suche. Die ESP32-Unit fragt die externe API ab und liefert das JSON zurück.</p>
  <form id="searchForm">
    <input id="query" type="text" placeholder="Fahrername (z.B. verstappen)" autocomplete="off" />
    <button id="searchBtn" type="submit">Suche</button>
  </form>
  <div id="status"></div>
  <div id="results"></div>

  <script>
    const form = document.getElementById('searchForm');
    const input = document.getElementById('query');
    const results = document.getElementById('results');
    const status = document.getElementById('status');

    form.addEventListener('submit', async e => {
      e.preventDefault();
      const q = input.value.trim();
      if(!q) { status.textContent = 'Bitte gib einen Suchbegriff ein.'; return; }
      status.textContent = `Suche nach "${q}"...`;
      results.innerHTML = '';
      try{
        const resp = await fetch(`/api/drivers?q=${encodeURIComponent(q)}`);
        if(!resp.ok) throw new Error(`Server returned ${resp.status}`);
        const data = await resp.json();
        render(data);
      }catch(err){
        status.textContent = 'Fehler: ' + err.message;
      }
    });

    function render(data){
      if(!data || !Array.isArray(data.drivers)){
        status.textContent = 'Unerwartete Antwort vom Server.'; return;
      }
      status.textContent = `Gefundene Fahrer: ${data.total ?? data.drivers.length}`;
      data.drivers.forEach(d => {
        const div = document.createElement('div'); div.className='card';
        div.innerHTML = `<strong>${d.name ?? ''} ${d.surname ?? ''}</strong> ${d.shortName? '('+d.shortName+')':''}`;
        const meta = document.createElement('div'); meta.textContent = `Nationality: ${d.nationality ?? ''} | Birthday: ${d.birthday ?? ''} | Number: ${d.number ?? '-'} `;
        div.appendChild(meta);
        const a = document.createElement('a'); a.href = d.url || '#'; a.target='_blank'; a.textContent = 'Wikipedia'; a.style.marginRight='12px'; div.appendChild(a);
        const preBtn = document.createElement('button'); preBtn.textContent='Zeige JSON'; preBtn.onclick = ()=>{ if(pre.nextSibling) { pre.remove(); } else div.appendChild(pre); };
        div.appendChild(preBtn);
        const pre = document.createElement('pre'); pre.textContent = JSON.stringify(d, null, 2);
        results.appendChild(div);
      });
    }
  </script>
</body>
</html>
)rawliteral";

// URL-encode helper
String urlEncode(const String &str){
  String encoded = "";
  char c;
  for(size_t i=0;i<str.length();i++){
    c = str[i];
    if( (c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='-' || c=='_' || c=='.' || c=='~'){
      encoded += c;
    } else if(c == ' ') {
      encoded += '+';
    } else {
      char buf[5];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      encoded += buf;
    }
  }
  return encoded;
}

void handleRoot(){
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiDrivers(){
  if(!server.hasArg("q") || server.arg("q").length() == 0){
    server.send(400, "application/json", "{\"error\":\"missing query param q\"}");
    return;
  }
  String q = server.arg("q");
  String endpoint = "https://f1api.dev/api/drivers/search?q=" + urlEncode(q) + "&limit=30&offset=0";

  WiFiClientSecure client;
  client.setInsecure(); // skip certificate validation (easier for examples). For production, verify certs.
  HTTPClient http;
  http.begin(client, endpoint);
  int code = http.GET();
  if(code > 0){
    String payload = http.getString();
    server.send(200, "application/json", payload);
  } else {
    String err = "{\"error\":\"upstream request failed\"}";
    server.send(502, "application/json", err);
  }
  http.end();
}

void handleNotFound(){
  server.send(404, "text/plain", "Not found");
}

void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("Starting ESP32 F1 proxy server...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to %s", ssid);
  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print('.');
    if(millis() - start > 20000){
      Serial.println();
      Serial.println("Failed to connect to WiFi within timeout. Restarting...");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/drivers", HTTP_GET, handleApiDrivers);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started on port 80");
}

void loop(){
  server.handleClient();
}

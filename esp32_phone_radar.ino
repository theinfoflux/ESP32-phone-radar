
#include <WiFi.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include "tcpip_adapter.h"

WebServer server(80);

const char* AP_SSID = "ESP32_PhoneRadar";
const char* AP_PASSWORD = "12345678";

#define MAX_PHONES 4
#define RSSI_SAMPLES 5

struct PhoneData {
  uint8_t mac[6];

  int rssiSamples[RSSI_SAMPLES];
  int sampleIndex;
  int sampleCount;

  float averageRSSI;
  float previousRSSI;

  bool active;
  unsigned long lastSeen;
};

PhoneData phones[MAX_PHONES];

String laptopIP = "";


// =====================================================
// MAC TO STRING
// =====================================================
String macToString(const uint8_t *mac) {
  char buffer[18];

  sprintf(buffer,
          "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);

  return String(buffer);
}


// =====================================================
// COMPARE MAC
// =====================================================
bool sameMAC(const uint8_t *a, const uint8_t *b) {

  for (int i = 0; i < 6; i++) {

    if (a[i] != b[i])
      return false;
  }

  return true;
}


// =====================================================
// FIND PHONE
// =====================================================
int findPhone(const uint8_t *mac) {

  for (int i = 0; i < MAX_PHONES; i++) {

    if (phones[i].active &&
        sameMAC(phones[i].mac, mac)) {

      return i;
    }
  }

  return -1;
}


// =====================================================
// ADD PHONE
// =====================================================
int addPhone(const uint8_t *mac) {

  for (int i = 0; i < MAX_PHONES; i++) {

    if (!phones[i].active) {

      memcpy(phones[i].mac, mac, 6);

      phones[i].sampleIndex = 0;
      phones[i].sampleCount = 0;

      phones[i].averageRSSI = -100;
      phones[i].previousRSSI = -100;

      phones[i].active = true;
      phones[i].lastSeen = millis();

      return i;
    }
  }

  return -1;
}


// =====================================================
// RSSI SMOOTHING
// =====================================================
float getAverageRSSI(PhoneData &p, int newRSSI) {

  p.rssiSamples[p.sampleIndex] = newRSSI;

  p.sampleIndex++;

  if (p.sampleIndex >= RSSI_SAMPLES)
    p.sampleIndex = 0;

  if (p.sampleCount < RSSI_SAMPLES)
    p.sampleCount++;

  float total = 0;

  for (int i = 0; i < p.sampleCount; i++) {

    total += p.rssiSamples[i];
  }

  return total / p.sampleCount;
}


// =====================================================
// PROXIMITY
// =====================================================
String getProximity(float rssi) {

  if (rssi >= -45)
    return "VERY CLOSE";

  if (rssi >= -55)
    return "CLOSE";

  if (rssi >= -65)
    return "MEDIUM";

  if (rssi >= -75)
    return "FAR";

  return "VERY FAR";
}


// =====================================================
// RADAR VISUAL DISTANCE
// NOTE: NOT REAL METERS
// =====================================================
int rssiToRadarDistance(float rssi) {

  if (rssi >= -40)
    return 12;

  if (rssi >= -50)
    return 25;

  if (rssi >= -60)
    return 40;

  if (rssi >= -70)
    return 62;

  if (rssi >= -80)
    return 82;

  return 95;
}


// =====================================================
// DETECT LAPTOP IP
// =====================================================
void detectLaptopIP() {

  if (server.client()) {

    IPAddress remoteIP =
      server.client().remoteIP();

    laptopIP =
      remoteIP.toString();

    Serial.print("Radar display device IP: ");
    Serial.println(laptopIP);
  }
}


// =====================================================
// UPDATE DEVICES
// =====================================================
void updateDevices() {

  wifi_sta_list_t stationList;

  memset(
    &stationList,
    0,
    sizeof(stationList)
  );

  esp_wifi_ap_get_sta_list(
    &stationList
  );


  tcpip_adapter_sta_list_t adapterList;

  memset(
    &adapterList,
    0,
    sizeof(adapterList)
  );

  tcpip_adapter_get_sta_list(
    &stationList,
    &adapterList
  );


  // Mark existing devices inactive
  for (int i = 0; i < MAX_PHONES; i++) {

    phones[i].active = false;
  }


  // Process connected devices
  for (int i = 0;
       i < stationList.num;
       i++) {

    uint8_t *mac =
      stationList.sta[i].mac;

    int deviceIndex = -1;

    String deviceIP = "";


    // Find device IP
    for (int j = 0;
         j < adapterList.num;
         j++) {

      if (sameMAC(
            mac,
            adapterList.sta[j].mac)) {

        deviceIP =
          IPAddress(
            adapterList.sta[j].ip.addr
          ).toString();

        break;
      }
    }


    // Ignore laptop
    if (deviceIP.length() > 0 &&
        deviceIP == laptopIP) {

      continue;
    }


    // Find or add phone
    deviceIndex =
      findPhone(mac);


    if (deviceIndex < 0) {

      deviceIndex =
        addPhone(mac);
    }


    if (deviceIndex >= 0) {

      PhoneData &p =
        phones[deviceIndex];

      p.previousRSSI =
        p.averageRSSI;

      p.averageRSSI =
        getAverageRSSI(
          p,
          stationList.sta[i].rssi
        );

      p.lastSeen =
        millis();

      p.active = true;
    }
  }
}


// =====================================================
// RADAR JSON
// =====================================================
String getRadarJSON() {

  String json = "{";

  json += "\"phones\":[";

  bool first = true;


  int angles[MAX_PHONES] = {
    -45,
    45,
    135,
    225
  };


  for (int i = 0;
       i < MAX_PHONES;
       i++) {

    if (!phones[i].active)
      continue;


    if (!first)
      json += ",";

    first = false;


    json += "{";


    json += "\"id\":";
    json += String(i + 1);


    json += ",\"mac\":\"";

    json += macToString(
      phones[i].mac
    );

    json += "\"";


    json += ",\"rssi\":";

    json += String(
      phones[i].averageRSSI,
      1
    );


    json += ",\"distance\":";

    json += String(
      rssiToRadarDistance(
        phones[i].averageRSSI
      )
    );


    json += ",\"angle\":";

    json += String(
      angles[i]
    );


    json += ",\"proximity\":\"";

    json += getProximity(
      phones[i].averageRSSI
    );

    json += "\"";


    json += "}";
  }


  json += "]}";

  return json;
}


// =====================================================
// WEB PAGE
// =====================================================
void handleRoot() {

  detectLaptopIP();


  String html = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>ESP32 PHONE RADAR</title>


<style>

* {
  box-sizing: border-box;
}


body {

  margin: 0;

  background: #020807;

  color: #00ff88;

  font-family: Arial, sans-serif;

  text-align: center;

  overflow-x: hidden;
}


h1 {

  margin: 12px 0 5px;

  font-size: 25px;

  text-shadow:
    0 0 12px #00ff88;
}


.subtitle {

  color: #8affc5;

  font-size: 13px;

  margin-bottom: 10px;
}


/* MAIN LAYOUT */

.main {

  width: 100%;

  display: flex;

  justify-content: center;

  align-items: center;

  gap: 20px;

  padding: 5px 15px 15px;
}


/* SIDE PANELS */

.side {

  width: 180px;

  min-width: 180px;

  display: flex;

  flex-direction: column;

  gap: 12px;
}


/* RADAR */

#radar {

  position: relative;

  width: min(65vw, 560px);

  height: min(65vw, 560px);

  max-width: 560px;

  max-height: 560px;

  min-width: 400px;

  min-height: 400px;

  border-radius: 50%;

  border: 3px solid #00ff88;

  background:
    radial-gradient(
      circle,
      transparent 18%,
      rgba(0,255,136,.04) 19%,
      transparent 20%,
      transparent 38%,
      rgba(0,255,136,.04) 39%,
      transparent 40%,
      transparent 58%,
      rgba(0,255,136,.04) 59%,
      transparent 60%,
      transparent 78%,
      rgba(0,255,136,.04) 79%,
      transparent 80%
    );

  box-shadow:
    0 0 20px #00ff88,
    inset 0 0 30px rgba(0,255,136,.2);

  overflow: hidden;

  flex-shrink: 0;
}


/* RADAR RINGS */

.ring {

  position: absolute;

  border:
    1px solid
    rgba(0,255,136,.35);

  border-radius: 50%;

  left: 50%;

  top: 50%;

  transform:
    translate(-50%,-50%);
}


.r1 {
  width: 20%;
  height: 20%;
}


.r2 {
  width: 40%;
  height: 40%;
}


.r3 {
  width: 60%;
  height: 60%;
}


.r4 {
  width: 80%;
  height: 80%;
}


/* CROSS LINES */

.line {

  position: absolute;

  background:
    rgba(0,255,136,.3);

  left: 50%;

  top: 0;

  width: 1px;

  height: 100%;
}


.line2 {

  position: absolute;

  background:
    rgba(0,255,136,.3);

  top: 50%;

  left: 0;

  width: 100%;

  height: 1px;
}


/* SWEEP */

.sweep {

  position: absolute;

  width: 50%;

  height: 2px;

  background: #00ff88;

  left: 50%;

  top: 50%;

  transform-origin: left center;

  box-shadow:
    0 0 12px #00ff88;

  animation:
    sweep 2s linear infinite;
}


@keyframes sweep {

  from {
    transform: rotate(0deg);
  }

  to {
    transform: rotate(360deg);
  }
}


/* CENTER */

.center {

  position: absolute;

  width: 18px;

  height: 18px;

  background: #00ff88;

  border-radius: 50%;

  left: 50%;

  top: 50%;

  transform:
    translate(-50%,-50%);

  box-shadow:
    0 0 20px #00ff88;
}


/* TARGET */

.target {

  position: absolute;

  width: 14px;

  height: 14px;

  background: red;

  border-radius: 50%;

  transform:
    translate(-50%,-50%);

  box-shadow:
    0 0 15px red;

  animation:
    pulse 1s infinite;
}


@keyframes pulse {

  0% {

    opacity: .5;

    transform:
      translate(-50%,-50%)
      scale(.8);
  }

  50% {

    opacity: 1;

    transform:
      translate(-50%,-50%)
      scale(1.3);
  }

  100% {

    opacity: .5;

    transform:
      translate(-50%,-50%)
      scale(.8);
  }
}


/* TARGET LABEL */

.label {

  position: absolute;

  color: white;

  font-size: 11px;

  font-weight: bold;

  transform:
    translate(-50%, 10px);

  white-space: nowrap;

  text-shadow:
    0 0 5px black;
}


/* PHONE CARD */

.card {

  border:
    1px solid #00ff88;

  border-radius: 10px;

  padding: 10px;

  background:
    rgba(0,255,136,.04);

  box-shadow:
    0 0 10px
    rgba(0,255,136,.2);

  text-align: left;

  min-height: 75px;
}


.phone {

  color: red;

  font-weight: bold;

  font-size: 15px;

  margin-bottom: 5px;
}


.info {

  font-size: 12px;

  color: #d5ffe9;

  line-height: 1.5;
}


.empty {

  border:
    1px dashed
    rgba(0,255,136,.3);

  color:
    rgba(0,255,136,.4);

  border-radius: 10px;

  padding: 15px;

  font-size: 12px;
}


/* MOBILE / SMALL LAPTOP */

@media(max-width: 950px) {

  .main {

    flex-direction: column;
  }


  .side {

    width: 90%;

    max-width: 600px;

    flex-direction: row;

    flex-wrap: wrap;

    justify-content: center;
  }


  .card {

    width: 170px;
  }


  #radar {

    min-width: 350px;

    min-height: 350px;

    width: 80vw;

    height: 80vw;
  }

}

</style>

</head>


<body>


<h1>ESP32 PHONE RADAR</h1>


<div class="subtitle">

Only connected phones are displayed

</div>


<div class="main">


<!-- LEFT SIDE -->

<div class="side" id="leftCards">

</div>


<!-- RADAR -->

<div id="radar">

  <div class="ring r1"></div>

  <div class="ring r2"></div>

  <div class="ring r3"></div>

  <div class="ring r4"></div>

  <div class="line"></div>

  <div class="line2"></div>

  <div class="sweep"></div>

  <div class="center"></div>

</div>


<!-- RIGHT SIDE -->

<div class="side" id="rightCards">

</div>


</div>


<script>


async function updateRadar() {

  try {

    const response =
      await fetch('/data');


    const data =
      await response.json();


    // Remove old targets

    document
      .querySelectorAll('.target')
      .forEach(
        e => e.remove()
      );


    document
      .querySelectorAll('.label')
      .forEach(
        e => e.remove()
      );


    const radar =
      document.getElementById(
        'radar'
      );


    const left =
      document.getElementById(
        'leftCards'
      );


    const right =
      document.getElementById(
        'rightCards'
      );


    left.innerHTML = "";

    right.innerHTML = "";


    const size =
      radar.clientWidth;


    const center =
      size / 2;


    data.phones.forEach(
      (phone, index) => {


        // --------------------------
        // TARGET POSITION
        // --------------------------

        const angle =
          phone.angle *
          Math.PI / 180;


        const radius =
          size *
          phone.distance /
          200;


        const x =
          center +
          Math.sin(angle) *
          radius;


        const y =
          center -
          Math.cos(angle) *
          radius;


        const target =
          document.createElement(
            'div'
          );


        target.className =
          'target';


        target.style.left =
          x + 'px';


        target.style.top =
          y + 'px';


        radar.appendChild(
          target
        );


        // --------------------------
        // TARGET LABEL
        // --------------------------

        const label =
          document.createElement(
            'div'
          );


        label.className =
          'label';


        label.innerHTML =
          'PHONE ' +
          phone.id;


        label.style.left =
          x + 'px';


        label.style.top =
          y + 'px';


        radar.appendChild(
          label
        );


        // --------------------------
        // SIDE CARD
        // --------------------------

        const card =
          document.createElement(
            'div'
          );


        card.className =
          'card';


        card.innerHTML =

          '<div class="phone">' +

          ' PHONE ' +
          phone.id +

          '</div>' +

          '<div class="info">' +

          'RSSI: ' +
          phone.rssi +
          ' dBm<br>' +

          'Signal: ' +
          phone.proximity +

          '</div>';


        // First two left,
        // next two right

        if (index % 2 == 0) {

          left.appendChild(
            card
          );

        } else {

          right.appendChild(
            card
          );
        }

      }
    );


    // Empty panel message

    if (data.phones.length == 0) {

      left.innerHTML =
        '<div class="empty">' +
        'Waiting for phones...' +
        '</div>';
    }

  }

  catch(error) {

    console.log(error);
  }

}


setInterval(
  updateRadar,
  400
);


updateRadar();


</script>


</body>

</html>

)rawliteral";


  server.send(
    200,
    "text/html",
    html
  );
}


// =====================================================
// DATA
// =====================================================
void handleData() {

  updateDevices();

  server.send(
    200,
    "application/json",
    getRadarJSON()
  );
}


// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();

  Serial.println(
    "=============================="
  );

  Serial.println(
    " ESP32 PHONE RADAR"
  );

  Serial.println(
    "=============================="
  );


  WiFi.mode(WIFI_AP);


  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );


  Serial.print(
    "WiFi Name: "
  );

  Serial.println(
    AP_SSID
  );


  Serial.print(
    "Radar URL: http://"
  );

  Serial.println(
    WiFi.softAPIP()
  );


  server.on(
    "/",
    handleRoot
  );


  server.on(
    "/data",
    handleData
  );


  server.begin();


  Serial.println(
    "Web server started."
  );


  Serial.println(
    "Waiting for phones..."
  );
}


// =====================================================
// LOOP
// =====================================================
void loop() {

  server.handleClient();

  delay(10);
}
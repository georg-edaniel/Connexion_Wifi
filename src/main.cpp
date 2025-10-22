/*
  Titre      : ESP32 → Django DRF Dashboard avec DHT22
  Auteur     : Philip Moumie
  Date       : 17/10/2025
  Description: Connexion WiFi, lecture DHT22, envoi données température/humidité
               vers l'API Django avec clé API.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <DHT.h>

Preferences prefs;
WebServer server(80);

// ==== CONFIG DHT22 ====
#define DHTPIN 4          // Broche GPIO où est connecté le DHT22
#define DHTTYPE DHT22     // Type de capteur
DHT dht(DHTPIN, DHTTYPE);

// ==== CONFIG RÉSEAU ====
String apiEndpoint  = "http://192.168.20.126:8000/esp32/ingest/";
String dashboardURL = "http://192.168.20.126:8000/client/";
String esp32ApiKey  = "change_me";

int cfgFiltre = 1;  // ID du filtre (1 → 5)

// ==== ENVOI INTERVAL ====
unsigned long lastSend = 0;
const unsigned long sendInterval = 60000; // 60s

// ==== PAGE CONFIG ====
void handleRoot() {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Config WiFi</title></head><body>";
  html += "<h2>Configuration ESP32 + DHT22</h2>";
  html += "<p>Connectez-vous au réseau <b>Capteur_Config</b> puis ouvrez <b>http://192.168.4.1</b></p>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Réseau WiFi :</label><br><select name='ssid'>" + options + "</select><br><br>";
  html += "<label>Mot de passe :</label><br><input type='password' name='password'><br><br>";
  html += "<h3>API & Configuration</h3>";
  html += "<label>Clé API :</label><br><input type='text' name='espkey' value='" + esp32ApiKey + "'><br><br>";
  html += "<label>ID Filtre (1-5) :</label><br><input type='number' name='filtre' min='1' max='5' value='" + String(cfgFiltre) + "'><br><br>";
  html += "<button type='submit'>Sauvegarder</button></form><br><br>";
  html += "<form action='/reset' method='GET'><button type='submit'>Réinitialiser le WiFi</button></form>";
  html += "<br><br><h3>État du capteur DHT22</h3>";
  
  // Lecture et affichage des valeurs DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity)) {
    html += "<p style='color:red;'>❌ Erreur de lecture DHT22</p>";
  } else {
    html += "<p>🌡️ Température: <strong>" + String(temperature, 1) + "°C</strong></p>";
    html += "<p>💧 Humidité: <strong>" + String(humidity, 1) + "%</strong></p>";
  }
  
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ==== SAVE CONFIG ====
void handleSave() {
  prefs.begin("wifi", false);
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("password", server.arg("password"));
  prefs.putString("espkey", server.arg("espkey"));
  prefs.putInt("filtre", server.arg("filtre").toInt());
  prefs.end();

  server.send(200, "text/html", "<h2>Configuration enregistrée ✅<br>Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== RESET CONFIG ====
void handleReset() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect(true);
  server.send(200, "text/html", "<h2>WiFi réinitialisé ✅<br>Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== REDIRECTION DASHBOARD ====
void handleRedirect() {
  server.sendHeader("Location", dashboardURL, true);
  server.send(302, "text/plain", "");
}

// ==== LECTURE ET ENVOI DES DONNÉES DHT22 ====
void sendDHT22Data() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi non connecté");
    return;
  }

  // Lecture des données DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  // Vérification de la lecture
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ Échec de lecture du DHT22");
    return;
  }

  Serial.printf("📊 DHT22 - Temp: %.1f°C, Hum: %.1f%%\n", temperature, humidity);

  // Nom du filtre : filtre001, filtre002...
  char buf[10];
  sprintf(buf, "filtre%03d", cfgFiltre);
  String filtreNom = String(buf);

  // ==== ENVOI TEMPÉRATURE ====
  String payloadTemp = "{";
  payloadTemp += "\"nom\":\"temperature\",";
  payloadTemp += "\"type\":\"DHT22\",";
  payloadTemp += "\"valeur\":\"" + String(temperature, 1) + "\"";
  payloadTemp += "}";

  Serial.println("🌡️ Envoi température: " + payloadTemp);

  HTTPClient http;
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  int codeTemp = http.POST(payloadTemp);
  Serial.printf("🌡️ Code réponse température: %d\n", codeTemp);
  
  if (codeTemp == 201) {
    Serial.println("✅ Température envoyée avec succès");
  } else {
    String response = http.getString();
    Serial.println("❌ Erreur température: " + response);
  }
  http.end();

  delay(1000); // Pause entre les envois

  // ==== ENVOI HUMIDITÉ ====
  String payloadHum = "{";
  payloadHum += "\"nom\":\"humidite\",";
  payloadHum += "\"type\":\"DHT22\",";
  payloadHum += "\"valeur\":\"" + String(humidity, 1) + "\"";
  payloadHum += "}";

  Serial.println("💧 Envoi humidité: " + payloadHum);

  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  int codeHum = http.POST(payloadHum);
  Serial.printf("💧 Code réponse humidité: %d\n", codeHum);
  
  if (codeHum == 201) {
    Serial.println("✅ Humidité envoyée avec succès");
  } else {
    String response = http.getString();
    Serial.println("❌ Erreur humidité: " + response);
  }
  http.end();

  // Rotation du filtre pour la prochaine lecture (optionnel)
  cfgFiltre++;
  if (cfgFiltre > 5) cfgFiltre = 1;

  prefs.begin("wifi", false);
  prefs.putInt("filtre", cfgFiltre);
  prefs.end();
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  
  // Initialisation du DHT22
  dht.begin();
  Serial.println("🌡️ Initialisation DHT22...");
  delay(2000); // Attente pour la stabilisation du capteur

  // Test lecture DHT22
  float testTemp = dht.readTemperature();
  float testHum = dht.readHumidity();
  
  if (isnan(testTemp) || isnan(testHum)) {
    Serial.println("❌ DHT22 non détecté! Vérifiez le câblage:");
    Serial.println("   - VCC → 3.3V");
    Serial.println("   - DATA → GPIO4");
    Serial.println("   - GND → GND");
  } else {
    Serial.printf("✅ DHT22 OK - Temp: %.1f°C, Hum: %.1f%%\n", testTemp, testHum);
  }

  // Configuration WiFi
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  esp32ApiKey = prefs.getString("espkey", esp32ApiKey);
  cfgFiltre = prefs.getInt("filtre", 1);
  prefs.end();

  if (ssid != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 60) {
      delay(500);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Connecté WiFi! IP : " + WiFi.localIP().toString());
      
      // Premier envoi de données
      sendDHT22Data();

      server.on("/", handleRedirect);
      server.on("/config", handleRoot);
      server.on("/save", handleSave);
      server.on("/reset", handleReset);
      server.begin();
      return;
    }
  }

  // Mode point d'accès si non connecté
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Capteur_DHT22_Config");
  Serial.println("Mode configuration : http://" + WiFi.softAPIP().toString());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.begin();
}

// ==== LOOP ====
void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastSend >= sendInterval) {
      lastSend = now;
      sendDHT22Data();
    }
  }
}
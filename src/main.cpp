/*
  Titre      : ESP32 → Django DRF Dashboard avec DHT22 et 4 Ventilateurs Noctua NF-F12
  Auteur     : Philip Moumie
  Date       : 17/10/2025
  Description: Connexion WiFi, lecture DHT22, contrôle PWM de 4 ventilateurs Noctua NF-F12
               et envoi données vers l'API Django UNIQUEMENT pour filtre001.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

Preferences prefs;
WebServer server(80);

// ==== CONFIG DHT22 ====
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ==== CONFIG DES 4 VENTILATEURS NOCTUA NF-F12 ====
// NOUVELLES BROCHES PWM (évite GPIO23 qui est RX)
#define FAN1_PWM_PIN 13    // ← CHANGÉ: 13 au lieu de 23
#define FAN2_PWM_PIN 12    // ← CHANGÉ: 12 au lieu de 22  
#define FAN3_PWM_PIN 14    // ← CHANGÉ: 14 au lieu de 21
#define FAN4_PWM_PIN 27    // ← CHANGÉ: 27 au lieu de 19

// Broches TACH pour lecture RPM (optionnel)
#define FAN1_TACH_PIN 18
#define FAN2_TACH_PIN 5
#define FAN3_TACH_PIN 17
#define FAN4_TACH_PIN 16

// Variables pour chaque ventilateur
int currentFanSpeeds[4] = {0, 0, 0, 0};  // Vitesses actuelles (0-100%)
volatile unsigned long fanPulseCounts[4] = {0, 0, 0, 0};
unsigned long lastRpmReads[4] = {0, 0, 0, 0};
int currentRpms[4] = {0, 0, 0, 0};

// ==== CONFIG RÉSEAU ====
String apiEndpoint  = "http://192.168.20.126:8000/esp32/ingest/";
String dashboardURL = "http://192.168.20.126:8000/client/";
String esp32ApiKey  = "change_me";

// ==== ENVOI INTERVAL ====
unsigned long lastSend = 0;
const unsigned long sendInterval = 60000; // 60s

// ==== INTERRUPTIONS POUR COMPTAGE RPM ====
void IRAM_ATTR fan1Interrupt() { fanPulseCounts[0]++; }
void IRAM_ATTR fan2Interrupt() { fanPulseCounts[1]++; }
void IRAM_ATTR fan3Interrupt() { fanPulseCounts[2]++; }
void IRAM_ATTR fan4Interrupt() { fanPulseCounts[3]++; }

// ==== LECTURE RPM POUR CHAQUE VENTILATEUR ====
int readFanRPM(int fanIndex) {
  unsigned long currentTime = millis();
  if (currentTime - lastRpmReads[fanIndex] < 1000) return currentRpms[fanIndex];
  
  noInterrupts();
  unsigned long count = fanPulseCounts[fanIndex];
  fanPulseCounts[fanIndex] = 0;
  interrupts();
  
  // Le NF-F12 génère 2 impulsions par tour
  currentRpms[fanIndex] = (count * 60) / 2;
  lastRpmReads[fanIndex] = currentTime;
  
  return currentRpms[fanIndex];
}

// ==== CONTRÔLE INDIVIDUEL DES VENTILATEURS ====
void setFanSpeed(int fanIndex, int speed) {
  // Limiter la vitesse entre 0 et 100%
  speed = constrain(speed, 0, 100);
  
  int pwmValue;
  
  if (speed == 0) {
    pwmValue = 0; // Arrêt complet
  } else {
    // Mapping ajusté pour 5V
    pwmValue = map(speed, 0, 100, 0, 255);
    
    // Assurer un minimum pour le démarrage en 5V
    if (pwmValue < 60 && speed > 0) {
      pwmValue = 60;
    }
  }
  
  // Appliquer le PWM au ventilateur spécifique
  switch(fanIndex) {
    case 0: ledcWrite(0, pwmValue); break;
    case 1: ledcWrite(1, pwmValue); break;
    case 2: ledcWrite(2, pwmValue); break;
    case 3: ledcWrite(3, pwmValue); break;
  }
  
  // Mettre à jour la vitesse actuelle
  currentFanSpeeds[fanIndex] = speed;
  
  int rpm = readFanRPM(fanIndex);
  Serial.printf("🎛️ Ventilateur %d réglé à %d%% (PWM: %d/255) - RPM: %d\n", 
                fanIndex + 1, speed, pwmValue, rpm);
}

// ==== CONTRÔLE DE TOUS LES VENTILATEURS ====
void setAllFansSpeed(int speed) {
  for(int i = 0; i < 4; i++) {
    setFanSpeed(i, speed);
  }
  Serial.printf("🔄 Tous les ventilateurs réglés à %d%%\n", speed);
}

// ==== GESTIONNAIRE POUR LES ROUTES NON TROUVÉES ====
void handleNotFound() {
  Serial.printf("❌ Route non trouvée: %s\n", server.uri().c_str());
  server.send(404, "text/plain", "Route non trouvée: " + server.uri());
}

// ==== CONTRÔLE MANUEL DES VENTILATEURS ====
void handleControl() {
  Serial.println("📥 Requête reçue sur /control");
  
  if (server.hasArg("speed")) {
    int newSpeed = server.arg("speed").toInt();
    Serial.printf("🎯 Nouvelle vitesse pour tous: %d%%\n", newSpeed);
    setAllFansSpeed(newSpeed);
    
    server.send(200, "text/html", 
      "<html><head><script>alert('Tous les ventilateurs réglés à " + String(newSpeed) + "%'); window.location.href = '/';</script></head></html>");
  } else {
    server.send(400, "text/plain", "Paramètre 'speed' manquant");
  }
}

// ==== CONTRÔLE INDIVIDUEL DES VENTILATEURS ====
void handleIndividualControl() {
  Serial.println("📥 Requête reçue sur /control_individual");
  
  if (server.hasArg("fan") && server.hasArg("speed")) {
    int fanIndex = server.arg("fan").toInt() - 1; // Convertir 1-4 en 0-3
    int newSpeed = server.arg("speed").toInt();
    
    if (fanIndex >= 0 && fanIndex < 4) {
      Serial.printf("🎯 Ventilateur %d réglé à %d%%\n", fanIndex + 1, newSpeed);
      setFanSpeed(fanIndex, newSpeed);
      
      server.send(200, "text/html", 
        "<html><head><script>alert('Ventilateur " + String(fanIndex + 1) + " réglé à " + String(newSpeed) + "%'); window.location.href = '/';</script></head></html>");
    } else {
      server.send(400, "text/plain", "Index ventilateur invalide (1-4)");
    }
  } else {
    server.send(400, "text/plain", "Paramètres 'fan' ou 'speed' manquants");
  }
}

// ==== PAGE CONFIG AMÉLIORÉE POUR 4 VENTILATEURS ====
void handleRoot() {
  Serial.println("📄 Affichage page configuration 4 ventilateurs");
  
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Config ESP32 - 4 Ventilos</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f5f5f5; }";
  html += ".card { background: white; padding: 20px; margin: 10px 0; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".fan-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin: 15px 0; }";
  html += ".fan-card { background: #f8f9fa; padding: 15px; border-radius: 8px; border-left: 4px solid #4CAF50; }";
  html += "input, select, button { padding: 8px; margin: 5px 0; width: 100%; box-sizing: border-box; }";
  html += "button { background: #4CAF50; color: white; border: none; cursor: pointer; }";
  html += ".btn-individual { background: #2196F3; margin: 2px; padding: 6px; }";
  html += ".btn-all { background: #FF9800; }";
  html += ".status { padding: 10px; border-radius: 5px; margin: 10px 0; }";
  html += ".success { background: #d4edda; color: #155724; }";
  html += ".warning { background: #fff3cd; color: #856404; }";
  html += ".info { background: #e2f0fb; color: #0c5460; }";
  html += ".fan-speed { background: linear-gradient(90deg, #4CAF50, #FFC107, #F44336); height: 15px; border-radius: 7px; margin: 5px 0; }";
  html += "</style></head><body>";
  
  html += "<div class='card'><h2>🌐 ESP32 + DHT22 + 4 Ventilateurs Noctua NF-F12</h2>";
  html += "<p><strong>IP:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Filtre:</strong> filtre001 (fixe)</p>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Réseau WiFi :</label><select name='ssid'>" + options + "</select>";
  html += "<label>Mot de passe :</label><input type='password' name='password'>";
  html += "<h3>🔑 API & Configuration</h3>";
  html += "<label>Clé API :</label><input type='text' name='espkey' value='" + esp32ApiKey + "'>";
  html += "<button type='submit'>💾 Sauvegarder</button></form>";
  html += "<form action='/reset' method='GET'><button type='submit' style='background:#dc3545;'>🔄 Réinitialiser WiFi</button></form>";
  html += "</div>";

  // État du système
  html += "<div class='card'>";
  html += "<h3>📊 État du système</h3>";
  
  // Lecture des valeurs DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity)) {
    html += "<div class='status warning'>❌ Erreur lecture DHT22</div>";
  } else {
    html += "<div class='status success'>";
    html += "🌡️ Température: <strong>" + String(temperature, 1) + "°C</strong><br>";
    html += "💧 Humidité: <strong>" + String(humidity, 1) + "%</strong>";
    html += "</div>";
  }

  // État des 4 ventilateurs
  html += "<div class='status info'>";
  html += "<h4>🎛️ Contrôle des 4 Ventilateurs</h4>";
  html += "<div class='fan-grid'>";
  
  for(int i = 0; i < 4; i++) {
    int rpm = readFanRPM(i);
    html += "<div class='fan-card'>";
    html += "<strong>Ventilateur " + String(i + 1) + "</strong><br>";
    html += "Vitesse: <strong>" + String(currentFanSpeeds[i]) + "%</strong><br>";
    html += "RPM: <strong>" + String(rpm) + "</strong>";
    html += "<div class='fan-speed' style='width: " + String(currentFanSpeeds[i]) + "%;'></div>";
    html += "<form action='/control_individual' method='POST' style='margin-top:5px;'>";
    html += "<input type='hidden' name='fan' value='" + String(i + 1) + "'>";
    html += "<input type='range' name='speed' min='0' max='100' value='" + String(currentFanSpeeds[i]) + "' onchange='updateSpeed" + String(i + 1) + "(this.value)'>";
    html += "<span id='speedValue" + String(i + 1) + "'>" + String(currentFanSpeeds[i]) + "%</span>";
    html += "<button type='submit' class='btn-individual'>🔄 Appliquer</button>";
    html += "</form>";
    html += "</div>";
  }
  
  html += "</div>"; // fin fan-grid
  
  // Contrôle global
  html += "<form action='/control' method='POST' style='margin-top:15px;'>";
  html += "<label><strong>Contrôle global de tous les ventilateurs:</strong></label>";
  html += "<input type='range' name='speed' min='0' max='100' value='" + String(currentFanSpeeds[0]) + "' onchange='updateAllSpeed(this.value)'>";
  html += "<span id='allSpeedValue'>" + String(currentFanSpeeds[0]) + "%</span>";
  html += "<button type='submit' class='btn-all'>🔄 Appliquer à tous</button>";
  html += "</form>";
  
  html += "</div>"; // fin status info
  html += "</div>"; // fin card

  html += "<script>";
  for(int i = 1; i <= 4; i++) {
    html += "function updateSpeed" + String(i) + "(val) { document.getElementById('speedValue" + String(i) + "').innerText = val + '%'; }";
  }
  html += "function updateAllSpeed(val) { document.getElementById('allSpeedValue').innerText = val + '%'; }";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ==== SAVE CONFIG ====
void handleSave() {
  Serial.println("💾 Sauvegarde configuration");
  
  prefs.begin("wifi", false);
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("password", server.arg("password"));
  prefs.putString("espkey", server.arg("espkey"));
  prefs.end();

  server.send(200, "text/html", "<h2>Configuration enregistrée ✅<br>Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== RESET CONFIG ====
void handleReset() {
  Serial.println("🔄 Réinitialisation WiFi");
  
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  
  server.send(200, "text/html", "<h2>WiFi réinitialisé ✅<br>Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== ENDPOINT API POUR CONTRÔLE DISTANT ====
void handleAPIcontrol() {
  Serial.println("📥 Requête API reçue sur /api/control");
  
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    Serial.println("📦 Body reçu: " + body);
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      Serial.printf("❌ Erreur parsing JSON: %s\n", error.c_str());
      String response = "{\"status\":\"error\",\"message\":\"JSON invalide: " + String(error.c_str()) + "\"}";
      server.send(400, "application/json", response);
      return;
    }
    
    // Extraction des valeurs
    int vitesse = -1;
    int filtre_id = -1;
    int ventilateur_index = -1; // Nouveau: index du ventilateur (0-3)
    
    // Gérer vitesse
    if (doc["vitesse"].is<int>()) {
      vitesse = doc["vitesse"].as<int>();
    } else if (doc["vitesse"].is<String>()) {
      vitesse = doc["vitesse"].as<String>().toInt();
    }
    
    // Gérer filtre_id
    if (doc["filtre_id"].is<int>()) {
      filtre_id = doc["filtre_id"].as<int>();
    } else if (doc["filtre_id"].is<String>()) {
      filtre_id = doc["filtre_id"].as<String>().toInt();
    }
    
    // Gérer ventilateur_index (optionnel, si absent contrôle tous)
    if (doc["ventilateur_index"].is<int>()) {
      ventilateur_index = doc["ventilateur_index"].as<int>();
    } else if (doc["ventilateur_index"].is<String>()) {
      ventilateur_index = doc["ventilateur_index"].as<String>().toInt();
    }
    
    Serial.printf("🔍 Données extraites - Vitesse: %d, Filtre ID: %d, Ventilateur: %d\n", 
                  vitesse, filtre_id, ventilateur_index);
    
    if (vitesse >= 0 && vitesse <= 100) {
      if (ventilateur_index >= 0 && ventilateur_index < 4) {
        // Contrôle d'un ventilateur spécifique
        setFanSpeed(ventilateur_index, vitesse);
        int rpm = readFanRPM(ventilateur_index);
        
        String response = "{\"status\":\"success\",\"message\":\"Ventilateur " + String(ventilateur_index) + " réglé à " + String(vitesse) + "%\",\"filtre_id\":" + String(filtre_id) + ",\"ventilateur_index\":" + String(ventilateur_index) + ",\"rpm\":" + String(rpm) + "}";
        server.send(200, "application/json", response);
        Serial.printf("✅ Commande API - Ventilateur %d → %d%% (RPM: %d)\n", ventilateur_index, vitesse, rpm);
      } else {
        // Contrôle de tous les ventilateurs
        setAllFansSpeed(vitesse);
        String response = "{\"status\":\"success\",\"message\":\"Tous les ventilateurs réglés à " + String(vitesse) + "%\",\"filtre_id\":" + String(filtre_id) + "}";
        server.send(200, "application/json", response);
        Serial.printf("✅ Commande API - Tous les ventilateurs → %d%%\n", vitesse);
      }
    } else {
      String response = "{\"status\":\"error\",\"message\":\"Vitesse invalide: " + String(vitesse) + "\"}";
      server.send(400, "application/json", response);
      Serial.printf("❌ Vitesse invalide: %d\n", vitesse);
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Méthode non autorisée\"}");
  }
}

// ==== REDIRECTION DASHBOARD ====
void handleRedirect() {
  Serial.println("🔀 Redirection vers dashboard");
  server.sendHeader("Location", dashboardURL, true);
  server.send(302, "text/plain", "");
}

// ==== LECTURE ET ENVOI DES DONNÉES DHT22 AMÉLIORÉE ====
void sendDHT22Data() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi non connecté - Impossible d'envoyer les données");
    return;
  }

  Serial.println("📡 Tentative d'envoi des données DHT22...");

  // Lecture des données DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ Échec lecture DHT22");
    return;
  }

  Serial.printf("📊 Données lues - Temp: %.1f°C, Hum: %.1f%%\n", temperature, humidity);

  // Test de connexion au serveur
  WiFiClient client;
  if (!client.connect("192.168.20.126", 8000)) {
    Serial.println("❌ Échec connexion au serveur Django");
    Serial.println("🔍 Vérifiez que le serveur Django fonctionne sur 192.168.20.126:8000");
    return;
  }
  
  Serial.println("✅ Connexion au serveur Django réussie");
  client.stop();

  // ENVOI TEMPÉRATURE
  String payloadTemp = "{\"nom\":\"temperature\",\"type\":\"DHT22\",\"valeur\":\"" + String(temperature, 1) + "\"}";
  
  HTTPClient http;
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  Serial.println("🌡️ Envoi température...");
  Serial.println("URL: " + apiEndpoint);
  Serial.println("Payload: " + payloadTemp);
  
  int codeTemp = http.POST(payloadTemp);
  Serial.printf("🌡️ Code réponse température: %d\n", codeTemp);
  
  if (codeTemp > 0) {
    String response = http.getString();
    Serial.println("📥 Réponse serveur: " + response);
  } else {
    Serial.printf("❌ Erreur HTTP température: %s\n", http.errorToString(codeTemp).c_str());
  }
  http.end();

  delay(2000); // Pause plus longue entre les envois

  // ENVOI HUMIDITÉ
  String payloadHum = "{\"nom\":\"humidite\",\"type\":\"DHT22\",\"valeur\":\"" + String(humidity, 1) + "\"}";
  
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  Serial.println("💧 Envoi humidité...");
  Serial.println("Payload: " + payloadHum);
  
  int codeHum = http.POST(payloadHum);
  Serial.printf("💧 Code réponse humidité: %d\n", codeHum);
  
  if (codeHum > 0) {
    String response = http.getString();
    Serial.println("📥 Réponse serveur: " + response);
  } else {
    Serial.printf("❌ Erreur HTTP humidité: %s\n", http.errorToString(codeHum).c_str());
  }
  http.end();
}

// ==== TEST DE CONNECTIVITÉ RÉSEAU ====
void testConnexionReseau() {
  Serial.println("\n🔍 Test de connectivité réseau:");
  Serial.println("IP ESP32: " + WiFi.localIP().toString());
  Serial.println("Force du signal: " + String(WiFi.RSSI()) + " dBm");
  Serial.println("Passerelle: " + WiFi.gatewayIP().toString());
  Serial.println("DNS: " + WiFi.dnsIP().toString());
  
  // Test ping vers le serveur Django
  Serial.println("Test connexion vers 192.168.20.126:8000...");
  
  WiFiClient client;
  if (client.connect("192.168.20.126", 8000)) {
    Serial.println("✅ Serveur Django accessible");
    client.stop();
  } else {
    Serial.println("❌ Serveur Django inaccessible");
    Serial.println("🔧 Vérifiez:");
    Serial.println("   - Le serveur Django est-il démarré?");
    Serial.println("   - L'IP 192.168.20.126 est-elle correcte?");
    Serial.println("   - Le port 8000 est-il ouvert?");
  }
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 Démarrage ESP32 - 4 Ventilateurs pour filtre001...");
  
  // Configuration PWM pour les 4 ventilateurs - NOUVELLES BROCHES
  ledcSetup(0, 25000, 8); ledcAttachPin(FAN1_PWM_PIN, 0);
  ledcSetup(1, 25000, 8); ledcAttachPin(FAN2_PWM_PIN, 1);
  ledcSetup(2, 25000, 8); ledcAttachPin(FAN3_PWM_PIN, 2);
  ledcSetup(3, 25000, 8); ledcAttachPin(FAN4_PWM_PIN, 3);
  
  Serial.println("✅ Broches PWM configurées:");
  Serial.printf("   Ventilateur 1: GPIO%d\n", FAN1_PWM_PIN);
  Serial.printf("   Ventilateur 2: GPIO%d\n", FAN2_PWM_PIN);
  Serial.printf("   Ventilateur 3: GPIO%d\n", FAN3_PWM_PIN);
  Serial.printf("   Ventilateur 4: GPIO%d\n", FAN4_PWM_PIN);
  
  // Configuration des broches TACH
  pinMode(FAN1_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN2_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN3_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN4_TACH_PIN, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(FAN1_TACH_PIN), fan1Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN2_TACH_PIN), fan2Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN3_TACH_PIN), fan3Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN4_TACH_PIN), fan4Interrupt, FALLING);
  
  // Démarrer tous les ventilateurs à 0%
  setAllFansSpeed(0);
  
  // Initialisation DHT22
  dht.begin();
  delay(2000);
  
  // Test DHT22
  float testTemp = dht.readTemperature();
  float testHum = dht.readHumidity();
  
  if (isnan(testTemp) || isnan(testHum)) {
    Serial.println("❌ DHT22 non détecté");
  } else {
    Serial.printf("✅ DHT22 OK - Temp: %.1f°C, Hum: %.1f%%\n", testTemp, testHum);
  }

  Serial.println("🎛️ 4 Ventilateurs Noctua NF-F12 initialisés - Prêts pour le contrôle PWM");
  Serial.println("📡 Toutes les données seront envoyées pour filtre001");

  // Chargement configuration WiFi
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  esp32ApiKey = prefs.getString("espkey", esp32ApiKey);
  prefs.end();

  // Tentative connexion WiFi
  if (ssid != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    Serial.print("📡 Connexion WiFi");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
      delay(1000);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi connecté! IP: " + WiFi.localIP().toString());
      
      // Test de connectivité réseau
      testConnexionReseau();
      
      // ROUTES MODE CONNECTÉ
      server.on("/", handleRedirect);
      server.on("/config", handleRoot);
      server.on("/save", HTTP_POST, handleSave);
      server.on("/reset", HTTP_GET, handleReset);
      server.on("/control", HTTP_POST, handleControl);
      server.on("/control_individual", HTTP_POST, handleIndividualControl);
      server.on("/api/control", HTTP_POST, handleAPIcontrol);
      server.onNotFound(handleNotFound);
      
      server.begin();
      Serial.println("🌐 Serveur HTTP démarré (mode STA)");
      
      sendDHT22Data();
      return;
    }
  }

  // MODE POINT D'ACCÈS
  Serial.println("🔄 Mode point d'accès");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_4Ventilos", "12345678");
  
  Serial.println("📶 AP démarré: ESP32_4Ventilos");
  Serial.println("🔗 IP: " + WiFi.softAPIP().toString());

  // ROUTES MODE AP
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/control_individual", HTTP_POST, handleIndividualControl);
  server.on("/api/control", HTTP_POST, handleAPIcontrol);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("🌐 Serveur HTTP démarré (mode AP)");
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
  
  // Lecture RPM périodique de tous les ventilateurs
  static unsigned long lastRpmCheck = 0;
  if (millis() - lastRpmCheck > 2000) {
    lastRpmCheck = millis();
    for(int i = 0; i < 4; i++) {
      readFanRPM(i);
    }
  }
}
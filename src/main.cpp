/*
  Titre      : ESP32 → Dashboard avec DHT22 et 4 Ventilateurs
  Auteur     : Philip Moumie
  Date       : 17/10/2025
  Description: Connexion WiFi, lecture DHT22, contrôle PWM de 4 ventilateurs
               et envoi données vers l'API Django pour filtre001.
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

// ==== CONFIG DES 4 VENTILATEURS ====
#define FAN1_PWM_PIN 13
#define FAN2_PWM_PIN 12  
#define FAN3_PWM_PIN 14
#define FAN4_PWM_PIN 27

// Variables pour chaque ventilateur
int currentFanSpeeds[4] = {0, 0, 0, 0};  // Vitesses actuelles (0-100%)

// ==== CONFIG RÉSEAU ====
String apiEndpoint  = "http://192.168.20.126:8000/esp32/ingest/";
String dashboardURL = "http://192.168.20.126:8000/client/";
String esp32ApiKey  = "change_me";

// ==== ENVOI INTERVAL ====
unsigned long lastSend = 0;
const unsigned long sendInterval = 60000; // 60s

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
  
  Serial.printf("Ventilateur %d réglé à %d%% (PWM: %d/255)\n", 
                fanIndex + 1, speed, pwmValue);
}

// ==== CONTRÔLE DE TOUS LES VENTILATEURS ====
void setAllFansSpeed(int speed) {
  for(int i = 0; i < 4; i++) {
    setFanSpeed(i, speed);
  }
  Serial.printf("Tous les ventilateurs réglés à %d%%\n", speed);
}

// ==== GESTIONNAIRE POUR LES ROUTES NON TROUVÉES ====
void handleNotFound() {
  Serial.printf("Route non trouvée: %s\n", server.uri().c_str());
  server.send(404, "text/plain", "Route non trouvée: " + server.uri());
}

// ==== CONTRÔLE MANUEL DES VENTILATEURS ====
void handleControl() {
  Serial.println("Requête reçue sur /control");
  
  if (server.hasArg("speed")) {
    int newSpeed = server.arg("speed").toInt();
    Serial.printf("Nouvelle vitesse pour tous: %d%%\n", newSpeed);
    setAllFansSpeed(newSpeed);
    
    server.send(200, "text/html", 
      "<html><head><script>alert('Tous les ventilateurs réglés à " + String(newSpeed) + "%'); window.location.href = '/';</script></head></html>");
  } else {
    server.send(400, "text/plain", "Paramètre 'speed' manquant");
  }
}

// ==== CONTRÔLE INDIVIDUEL DES VENTILATEURS ====
void handleIndividualControl() {
  Serial.println("Requête reçue sur /control_individual");
  
  if (server.hasArg("fan") && server.hasArg("speed")) {
    int fanIndex = server.arg("fan").toInt() - 1; // Convertir 1-4 en 0-3
    int newSpeed = server.arg("speed").toInt();
    
    if (fanIndex >= 0 && fanIndex < 4) {
      Serial.printf("Ventilateur %d réglé à %d%%\n", fanIndex + 1, newSpeed);
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

// ==== PAGE CONFIG SIMPLIFIÉE ====
void handleRoot() {
  Serial.println("Affichage page configuration");
  
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuration ESP32</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; color: #333; }";
  html += ".container { max-width: 600px; margin: 0 auto; }";
  html += ".card { background: white; padding: 25px; margin: 15px 0; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }";
  html += "h1, h2, h3 { color: #2c3e50; margin-top: 0; }";
  html += "input, select, button { padding: 12px; margin: 8px 0; width: 100%; box-sizing: border-box; border: 1px solid #ddd; border-radius: 6px; font-size: 16px; }";
  html += "button { background: #3498db; color: white; border: none; cursor: pointer; font-weight: bold; }";
  html += "button:hover { background: #2980b9; }";
  html += ".btn-reset { background: #e74c3c; margin-top: 10px; }";
  html += ".btn-reset:hover { background: #c0392b; }";
  html += ".status { padding: 15px; border-radius: 8px; margin: 15px 0; }";
  html += ".success { background: #d5f4e6; color: #27ae60; border-left: 4px solid #27ae60; }";
  html += ".warning { background: #fdebd0; color: #e67e22; border-left: 4px solid #e67e22; }";
  html += ".info { background: #d6eaf8; color: #2980b9; border-left: 4px solid #2980b9; }";
  html += ".fan-control { background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 10px 0; }";
  html += ".fan-grid { display: grid; grid-template-columns: 1fr; gap: 10px; }";
  html += ".speed-display { text-align: center; font-size: 18px; font-weight: bold; color: #2c3e50; margin: 10px 0; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<div class='card'>";
  html += "<h1>Configuration ESP32</h1>";
  html += "<p><strong>Adresse IP:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Filtre:</strong> filtre001</p>";
  
  html += "<form action='/save' method='POST'>";
  html += "<h3>Configuration WiFi</h3>";
  html += "<label>Réseau WiFi :</label>";
  html += "<select name='ssid'>" + options + "</select>";
  html += "<label>Mot de passe :</label>";
  html += "<input type='password' name='password' placeholder='Entrez le mot de passe WiFi'>";
  html += "<label>Clé API :</label>";
  html += "<input type='text' name='espkey' value='" + esp32ApiKey + "'>";
  html += "<button type='submit'>Sauvegarder la configuration</button>";
  html += "</form>";
  
  html += "<form action='/reset' method='GET'>";
  html += "<button type='submit' class='btn-reset'>Réinitialiser WiFi</button>";
  html += "</form>";
  html += "</div>";

  // État du système
  html += "<div class='card'>";
  html += "<h3>État du système</h3>";
  
  // Lecture des valeurs DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity)) {
    html += "<div class='status warning'>Erreur de lecture du capteur DHT22</div>";
  } else {
    html += "<div class='status success'>";
    html += "Température: <strong>" + String(temperature, 1) + "°C</strong><br>";
    html += "Humidité: <strong>" + String(humidity, 1) + "%</strong>";
    html += "</div>";
  }

  // Contrôle des ventilateurs
  html += "<div class='status info'>";
  html += "<h3>Contrôle des ventilateurs</h3>";
  html += "<div class='fan-grid'>";
  
  for(int i = 0; i < 4; i++) {
    html += "<div class='fan-control'>";
    html += "<h4>Ventilateur " + String(i + 1) + "</h4>";
    html += "<div class='speed-display'>Vitesse: " + String(currentFanSpeeds[i]) + "%</div>";
    html += "<form action='/control_individual' method='POST'>";
    html += "<input type='hidden' name='fan' value='" + String(i + 1) + "'>";
    html += "<input type='range' name='speed' min='0' max='100' value='" + String(currentFanSpeeds[i]) + "' onchange='updateSpeed" + String(i + 1) + "(this.value)'>";
    html += "<div style='text-align:center; margin:10px 0;'><span id='speedValue" + String(i + 1) + "'>" + String(currentFanSpeeds[i]) + "%</span></div>";
    html += "<button type='submit'>Appliquer</button>";
    html += "</form>";
    html += "</div>";
  }
  
  html += "</div>"; // fin fan-grid
  
  // Contrôle global
  html += "<div class='fan-control'>";
  html += "<h4>Contrôle global</h4>";
  html += "<form action='/control' method='POST'>";
  html += "<input type='range' name='speed' min='0' max='100' value='" + String(currentFanSpeeds[0]) + "' onchange='updateAllSpeed(this.value)'>";
  html += "<div class='speed-display'><span id='allSpeedValue'>" + String(currentFanSpeeds[0]) + "%</span></div>";
  html += "<button type='submit'>Appliquer à tous les ventilateurs</button>";
  html += "</form>";
  html += "</div>";
  
  html += "</div>"; // fin status info
  html += "</div>"; // fin card
  html += "</div>"; // fin container

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
  Serial.println("Sauvegarde configuration");
  
  prefs.begin("wifi", false);
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("password", server.arg("password"));
  prefs.putString("espkey", server.arg("espkey"));
  prefs.end();

  server.send(200, "text/html", "<h2>Configuration enregistrée avec succès<br>Redémarrage en cours...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== RESET CONFIG ====
void handleReset() {
  Serial.println("Réinitialisation WiFi");
  
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  
  server.send(200, "text/html", "<h2>WiFi réinitialisé<br>Redémarrage en cours...</h2>");
  delay(3000);
  ESP.restart();
}

// ==== ENDPOINT API POUR CONTRÔLE DISTANT ====
void handleAPIcontrol() {
  Serial.println("Requête API reçue sur /api/control");
  
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    Serial.println("Body reçu: " + body);
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      Serial.printf("Erreur parsing JSON: %s\n", error.c_str());
      String response = "{\"status\":\"error\",\"message\":\"JSON invalide: " + String(error.c_str()) + "\"}";
      server.send(400, "application/json", response);
      return;
    }
    
    // Extraction des valeurs
    int vitesse = -1;
    int filtre_id = -1;
    int ventilateur_index = -1;
    
    if (doc["vitesse"].is<int>()) {
      vitesse = doc["vitesse"].as<int>();
    } else if (doc["vitesse"].is<String>()) {
      vitesse = doc["vitesse"].as<String>().toInt();
    }
    
    if (doc["filtre_id"].is<int>()) {
      filtre_id = doc["filtre_id"].as<int>();
    } else if (doc["filtre_id"].is<String>()) {
      filtre_id = doc["filtre_id"].as<String>().toInt();
    }
    
    if (doc["ventilateur_index"].is<int>()) {
      ventilateur_index = doc["ventilateur_index"].as<int>();
    } else if (doc["ventilateur_index"].is<String>()) {
      ventilateur_index = doc["ventilateur_index"].as<String>().toInt();
    }
    
    Serial.printf("Données extraites - Vitesse: %d, Filtre ID: %d, Ventilateur: %d\n", 
                  vitesse, filtre_id, ventilateur_index);
    
    if (vitesse >= 0 && vitesse <= 100) {
      if (ventilateur_index >= 0 && ventilateur_index < 4) {
        // Contrôle d'un ventilateur spécifique
        setFanSpeed(ventilateur_index, vitesse);
        
        String response = "{\"status\":\"success\",\"message\":\"Ventilateur " + String(ventilateur_index) + " réglé à " + String(vitesse) + "%\",\"filtre_id\":" + String(filtre_id) + ",\"ventilateur_index\":" + String(ventilateur_index) + "}";
        server.send(200, "application/json", response);
        Serial.printf("Commande API - Ventilateur %d → %d%%\n", ventilateur_index, vitesse);
      } else {
        // Contrôle de tous les ventilateurs
        setAllFansSpeed(vitesse);
        String response = "{\"status\":\"success\",\"message\":\"Tous les ventilateurs réglés à " + String(vitesse) + "%\",\"filtre_id\":" + String(filtre_id) + "}";
        server.send(200, "application/json", response);
        Serial.printf("Commande API - Tous les ventilateurs → %d%%\n", vitesse);
      }
    } else {
      String response = "{\"status\":\"error\",\"message\":\"Vitesse invalide: " + String(vitesse) + "\"}";
      server.send(400, "application/json", response);
      Serial.printf("Vitesse invalide: %d\n", vitesse);
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"error\",\"message\":\"Méthode non autorisée\"}");
  }
}

// ==== REDIRECTION DASHBOARD ====
void handleRedirect() {
  Serial.println("Redirection vers dashboard");
  server.sendHeader("Location", dashboardURL, true);
  server.send(302, "text/plain", "");
}

// ==== LECTURE ET ENVOI DES DONNÉES DHT22 ====
void sendDHT22Data() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi non connecté - Impossible d'envoyer les données");
    return;
  }

  Serial.println("Tentative d'envoi des données DHT22...");

  // Lecture des données DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Échec lecture DHT22");
    return;
  }

  Serial.printf("Données lues - Temp: %.1f°C, Hum: %.1f%%\n", temperature, humidity);

  // Test de connexion au serveur
  WiFiClient client;
  if (!client.connect("192.168.20.126", 8000)) {
    Serial.println("Échec connexion au serveur Django");
    return;
  }
  
  Serial.println("Connexion au serveur Django réussie");
  client.stop();

  // ENVOI TEMPÉRATURE
  String payloadTemp = "{\"nom\":\"temperature\",\"type\":\"DHT22\",\"valeur\":\"" + String(temperature, 1) + "\"}";
  
  HTTPClient http;
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  Serial.println("Envoi température...");
  int codeTemp = http.POST(payloadTemp);
  Serial.printf("Code réponse température: %d\n", codeTemp);
  
  if (codeTemp > 0) {
    String response = http.getString();
    Serial.println("Réponse serveur: " + response);
  } else {
    Serial.printf("Erreur HTTP température: %s\n", http.errorToString(codeTemp).c_str());
  }
  http.end();

  delay(2000);

  // ENVOI HUMIDITÉ
  String payloadHum = "{\"nom\":\"humidite\",\"type\":\"DHT22\",\"valeur\":\"" + String(humidity, 1) + "\"}";
  
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  Serial.println("Envoi humidité...");
  int codeHum = http.POST(payloadHum);
  Serial.printf("Code réponse humidité: %d\n", codeHum);
  
  if (codeHum > 0) {
    String response = http.getString();
    Serial.println("Réponse serveur: " + response);
  } else {
    Serial.printf("Erreur HTTP humidité: %s\n", http.errorToString(codeHum).c_str());
  }
  http.end();
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  Serial.println("Démarrage ESP32 - 4 Ventilateurs pour filtre001...");
  
  // Configuration PWM pour les 4 ventilateurs
  ledcSetup(0, 25000, 8); ledcAttachPin(FAN1_PWM_PIN, 0);
  ledcSetup(1, 25000, 8); ledcAttachPin(FAN2_PWM_PIN, 1);
  ledcSetup(2, 25000, 8); ledcAttachPin(FAN3_PWM_PIN, 2);
  ledcSetup(3, 25000, 8); ledcAttachPin(FAN4_PWM_PIN, 3);
  
  Serial.println("Broches PWM configurées:");
  Serial.printf("   Ventilateur 1: GPIO%d\n", FAN1_PWM_PIN);
  Serial.printf("   Ventilateur 2: GPIO%d\n", FAN2_PWM_PIN);
  Serial.printf("   Ventilateur 3: GPIO%d\n", FAN3_PWM_PIN);
  Serial.printf("   Ventilateur 4: GPIO%d\n", FAN4_PWM_PIN);
  
  // Démarrer tous les ventilateurs à 0%
  setAllFansSpeed(0);
  
  // Initialisation DHT22
  dht.begin();
  delay(2000);
  
  // Test DHT22
  float testTemp = dht.readTemperature();
  float testHum = dht.readHumidity();
  
  if (isnan(testTemp) || isnan(testHum)) {
    Serial.println("DHT22 non détecté");
  } else {
    Serial.printf("DHT22 OK - Temp: %.1f°C, Hum: %.1f%%\n", testTemp, testHum);
  }

  Serial.println("4 Ventilateurs initialisés - Prêts pour le contrôle PWM");

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
    
    Serial.print("Connexion WiFi");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
      delay(1000);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connecté! IP: " + WiFi.localIP().toString());
      
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
      Serial.println("Serveur HTTP démarré (mode STA)");
      
      sendDHT22Data();
      return;
    }
  }

  // MODE POINT D'ACCÈS
  Serial.println("Mode point d'accès");
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_Filtre", "12345678");
  
  Serial.println("Point d'accès démarré: ESP32_Filtre");
  Serial.println("IP: " + WiFi.softAPIP().toString());

  // ROUTES MODE AP
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/control_individual", HTTP_POST, handleIndividualControl);
  server.on("/api/control", HTTP_POST, handleAPIcontrol);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Serveur HTTP démarré (mode AP)");
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
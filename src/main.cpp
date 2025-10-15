/*
  Titre      : Configuration WiFi + Redirection vers Dashboard Django
  Auteur     : Philip Moumie (arrangé)
  Date       : 15/10/2025
  Description: Connexion WiFi via page web (AP fallback), sauvegarde Preferences,
               envoi périodique de mesures vers endpoint /esp32/ingest/,
               diagnostic HTTP complet (code + body).
  Version    : 0.0.3
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

Preferences prefs;
WebServer server(80);

// Valeurs par défaut (adapter si tu veux)
String dashboardURL = "http://192.168.1.100/client/";    // à remplacer par l'IP réelle si besoin
String apiEndpoint  = "http://172.18.191.73:8000/esp32/ingest/";
String esp32ApiKey  = "change_me";

// Paramètres capteur
String cfgNom = "esp32_temp";
String cfgType = "temp";
int cfgFiltre = 0; // 0 = non défini / optionnel (n'enverra pas de filtre si 0)

// Envoi périodique
unsigned long lastSend = 0;
const unsigned long sendInterval = 60000; // 60s

// --- Helpers pour lire les prefs ---
void loadPrefs() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  prefs.end();

  prefs.begin("cfg", false);
  cfgNom      = prefs.getString("nom", cfgNom);
  cfgType     = prefs.getString("type", cfgType);
  cfgFiltre   = prefs.getInt("filtre", cfgFiltre);
  esp32ApiKey = prefs.getString("espkey", esp32ApiKey);
  apiEndpoint = prefs.getString("api", apiEndpoint);
  dashboardURL= prefs.getString("dashboard", dashboardURL);
  prefs.end();
}

// Page de configuration AP
void handleRoot() {
  int n = WiFi.scanNetworks();
  String options = "";
  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }

  // Lire prefs actuelles pour préremplir
  loadPrefs();

  // Préparer la valeur affichée pour le champ "filtre" (laisser vide si non défini)
  String filtreVal = (cfgFiltre > 0) ? String(cfgFiltre) : String("");

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuration WiFi</title></head><body>";
  html += "<h2>Bienvenue ! Configuration WiFi</h2>";
  html += "<p>Connectez-vous au réseau <b>Capteur_Config</b> puis ouvrez cette page (http://192.168.4.1).</p>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Réseau WiFi :</label><br><select name='ssid'>" + options + "</select><br><br>";
  html += "<label>Mot de passe :</label><br><input type='password' name='password'><br><br>";

  html += "<h3>Configuration capteur & API</h3>";
  html += "<label>Nom du capteur :</label><br><input type='text' name='nom' value='" + cfgNom + "'><br><br>";
  html += "<label>Type :</label><br><input type='text' name='type' value='" + cfgType + "'><br><br>";
  html += "<label>Filtre (optionnel) :</label><br><input type='number' name='filtre' value='" + filtreVal + "'><br><br>";
  html += "<label>Clé API X-ESP32-KEY :</label><br><input type='text' name='espkey' value='" + esp32ApiKey + "'><br><br>";
  html += "<label>API Endpoint (full URL) :</label><br><input type='text' name='api' value='" + apiEndpoint + "' style='width:90%'><br><br>";
  html += "<label>Dashboard URL :</label><br><input type='text' name='dashboard' value='" + dashboardURL + "' style='width:90%'><br><br>";

  html += "<button type='submit'>Sauvegarder (WiFi + capteur)</button></form><br><br>";
  html += "<form action='/reset' method='GET'><button type='submit'>Réinitialiser le WiFi / prefs</button></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Sauvegarde SSID + mot de passe + config
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  String nom = server.arg("nom");
  String type = server.arg("type");
  String filtre = server.arg("filtre");
  String espkey = server.arg("espkey");
  String api = server.arg("api");
  String dashboard = server.arg("dashboard");

  // Sauvegarde WiFi (namespace "wifi")
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  // Sauvegarde config capteur/API (namespace "cfg")
  prefs.begin("cfg", false);
  if (nom.length() > 0) prefs.putString("nom", nom);
  if (type.length() > 0) prefs.putString("type", type);
  if (filtre.length() > 0) prefs.putInt("filtre", filtre.toInt());
  else prefs.remove("filtre");
  if (espkey.length() > 0) prefs.putString("espkey", espkey);
  if (api.length() > 0) prefs.putString("api", api);
  if (dashboard.length() > 0) prefs.putString("dashboard", dashboard);
  prefs.end();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Connexion en cours</title></head><body>";
  html += "<h2>WiFi et configuration enregistrés ✅</h2>";
  html += "<p>L’appareil va redémarrer et tenter de se connecter au réseau choisi.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
  delay(2000);
  ESP.restart();
}

// Réinitialisation complète des prefs
void handleReset() {
  prefs.begin("wifi", false); prefs.clear(); prefs.end();
  prefs.begin("cfg", false); prefs.clear(); prefs.end();
  WiFi.disconnect(true);
  server.send(200, "text/html", "<h2>WiFi et prefs réinitialisés ! Redémarrage...</h2>");
  delay(2000);
  ESP.restart();
}

// Page après connexion réussie
void handleDashboard() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Connexion réussie</title></head><body>";
  html += "<h2>Connexion réussie ✅</h2>";
  html += "<p>IP locale : <b>" + WiFi.localIP().toString() + "</b></p>";
  html += "<p><a href='" + dashboardURL + "' target='_blank'><button>Ouvrir le Dashboard</button></a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void performDashboardRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("performDashboardRequest: pas de connexion WiFi");
    return;
  }
  HTTPClient http;
  Serial.println("Appel automatique du dashboard : " + dashboardURL);
  http.begin(dashboardURL);
  int code = http.GET();
  if (code > 0) {
    Serial.printf("Dashboard réponse HTTP: %d\n", code);
    String body = http.getString();
    Serial.println(body.substring(0, min(300, (int)body.length())));
  } else {
    Serial.printf("Erreur HTTP vers dashboard (code): %d\n", code);
  }
  http.end();
}

// Teste une connexion TCP simple vers host:port (timeout en ms)
bool testTcpConnect(const String &host, uint16_t port, uint16_t timeoutMs = 3000) {
  WiFiClient client;
  Serial.printf("Test TCP -> %s:%u ... ", host.c_str(), port);
  unsigned long start = millis();
  bool ok = client.connect(host.c_str(), port, timeoutMs);
  unsigned long dt = millis() - start;
  if (ok) {
    Serial.printf("OK (%.lums)\n", dt);
    client.stop();
    return true;
  } else {
    Serial.printf("ECHEC (%.lums)\n", dt);
    return false;
  }
}

// Extract host and port from a simple http://host:port/path or http://host/path
void extractHostPort(const String &url, String &hostOut, uint16_t &portOut) {
  hostOut = "";
  portOut = 80;
  String s = url;
  // remove protocol
  if (s.startsWith("http://")) s = s.substring(7);
  else if (s.startsWith("https://")) { s = s.substring(8); portOut = 443; }
  int slash = s.indexOf('/');
  String hostport = (slash == -1) ? s : s.substring(0, slash);
  int colon = hostport.indexOf(':');
  if (colon != -1) {
    hostOut = hostport.substring(0, colon);
    portOut = hostport.substring(colon + 1).toInt();
  } else {
    hostOut = hostport;
  }
}

void sendSimulatedData() {
  loadPrefs(); // relire config au cas où
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("sendSimulatedData: pas connecté WiFi");
    return;
  }

  // Génère température simulée (20.0 - 30.0)
  float temperature = random(200, 300) / 10.0;

  // Construire JSON en utilisant cfgFiltre etc.
  String mac = WiFi.macAddress();
  String nameSafe = mac; // on peut remplacer ":" si tu veux
  nameSafe.replace(":", ""); // plus propre pour le nom

  // Construire JSON proprement : omettre le champ "filtre" si cfgFiltre == 0 (optionnel)
  String payload = "{";
  bool firstField = true;

  if (cfgFiltre > 0) {
    payload += "\"filtre\":" + String(cfgFiltre);
    firstField = false;
  }

  if (!firstField) payload += ",";
  payload += "\"nom\":\"" + String(cfgNom) + "_" + nameSafe + "\"";
  firstField = false;

  payload += ",\"type\":\"" + cfgType + "\"";
  payload += ",\"valeur\":\"" + String(temperature, 1) + "\"";
  payload += "}";

  Serial.println("Envoi vers : " + apiEndpoint);
  Serial.println("Payload: " + payload);

  HTTPClient http;
  http.begin(apiEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-ESP32-KEY", esp32ApiKey);

  int code = http.POST(payload);
  if (code > 0) {
    Serial.printf("POST HTTP code: %d\n", code);
    String resp = http.getString();
    Serial.println("Réponse body: " + resp);
  } else {
    // code <= 0 : erreur réseau (resolve, connect, timeout, etc.)
    Serial.printf("Erreur POST (pas de réponse HTTP valide) : %d\n", code);
  }
  http.end();
}

// Forward declaration for handleRedirect
void handleRedirect();

void setup() {
  Serial.begin(115200);
  delay(100);

  // Charger prefs pour connaitre si on a des identifiants WiFi
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  loadPrefs(); // charger config capteur/API

  if (ssid != "") {
    Serial.print("Tentative connexion WiFi à ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
      delay(500);
      Serial.print(".");
      tries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ Connecté ! IP : " + WiFi.localIP().toString());

      // Appel au dashboard et envoi initial
      performDashboardRequest();
      sendSimulatedData();

      // config du mini serveur web pour permettre ouverture du dashboard local
      server.on("/", handleRedirect);
      server.on("/dashboard", handleDashboard);
      server.on("/config", handleRoot);
      server.on("/save", handleSave);
      server.on("/reset", handleReset);
      server.begin();
      return;
    } else {
      Serial.println("Échec connexion WiFi, bascule en AP mode");
    }
  }

  // Mode AP pour configuration
  WiFi.softAP("Capteur_Config");
  IPAddress IP = WiFi.softAPIP();
  Serial.println("AP Mode: http://" + IP.toString());
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.begin();
}

void loop() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastSend >= sendInterval) {
      lastSend = now;
      sendSimulatedData();
    }
  }
}

// Redirection simple depuis "/" vers le dashboard
void handleRedirect() {
  // recharge prefs au besoin
  loadPrefs();
  server.sendHeader("Location", dashboardURL, true);
  server.send(302, "text/plain", "");
}
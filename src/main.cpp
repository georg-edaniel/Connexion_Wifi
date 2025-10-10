/*
  Titre      : Configuration WiFi + Redirection vers Dashboard Django
  Auteur     : Philip Moumie
  Date       : 09/10/2025
  Description: Ce programme permet à l'ESP32 de se connecter à un réseau WiFi via une page web.
               Toutes les instructions nécessaires sont affichées au client directement dans le navigateur.
               Une fois connecté, l’ESP32 redirige vers la page Django "/clients/".
  Version    : 0.0.2
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

Preferences prefs;
WebServer server(80);

// Adresse du Dashboard Django (à adapter selon ton serveur)
String dashboardURL = "http://192.168.1.100/clients/";

// Page de configuration WiFi (mode AP)
void handleRoot() {
  int n = WiFi.scanNetworks();
  String options = "";

  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuration WiFi</title></head><body>";
  html += "<h2>Bienvenue !</h2>";
  html += "<p>Vous êtes connecté au réseau de configuration <b>Capteur_Config</b>.</p>";
  html += "<p>Ouvrez cette page (http://192.168.4.1) pour configurer votre WiFi.</p>";
  html += "<h3>Étape 1 : Choisissez votre réseau WiFi</h3>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Réseau WiFi :</label><br><select name='ssid'>" + options + "</select><br><br>";
  html += "<label>Mot de passe :</label><br><input type='password' name='password'><br><br>";
  html += "<button type='submit'>Se connecter</button></form><br><br>";
  html += "<form action='/reset' method='GET'><button type='submit'>Réinitialiser le WiFi</button></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Sauvegarde SSID + mot de passe
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Connexion en cours</title></head><body>";
  html += "<h2>WiFi enregistré ✅</h2>";
  html += "<p>L’appareil va redémarrer et tenter de se connecter à <b>" + ssid + "</b>.</p>";
  html += "<p>Veuillez patienter quelques secondes...</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
  delay(3000);
  ESP.restart();
}

// Réinitialisation WiFi
void handleReset() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();

  WiFi.disconnect(true);
  server.send(200, "text/html", "<h2>WiFi réinitialisé ! Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

// Page après connexion réussie
void handleDashboard() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Connexion réussie</title></head><body>";
  html += "<h2>Connexion réussie ✅</h2>";
  html += "<p>L’ESP32 est connecté à votre réseau WiFi.</p>";
  html += "<p>Adresse IP locale de l’appareil : <b>" + WiFi.localIP().toString() + "</b></p>";
  html += "<p>Accédez à votre Dashboard :</p>";
  html += "<a href='" + dashboardURL + "'><button>Ouvrir le Dashboard</button></a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid != "") {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Connexion à ");
    Serial.println(ssid);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ Connecté ! IP : " + WiFi.localIP().toString());

      server.on("/", handleDashboard);  // Page d’accueil = confirmation + bouton Dashboard
      server.on("/config", handleRoot); // Accès manuel à la config
      server.on("/save", handleSave);
      server.on("/reset", handleReset);
      server.begin();
      return;
    }
  }

  // Mode configuration si pas de WiFi
  Serial.println("\n⚙️ Mode configuration WiFi");
  WiFi.softAP("Capteur_Config");
  IPAddress IP = WiFi.softAPIP();
  Serial.println("Connectez-vous à : http://" + IP.toString());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/reset", handleReset);
  server.begin();
}

void loop() {
  server.handleClient();
}
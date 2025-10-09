/*
  Titre      : Configuration WiFi ESP32
  Auteur     : Philip Moumie
  Date       : 09/10/2025
  Description: Ce programme permet à un utilisateur de configurer le WiFi de l'ESP32 via une page web.
               Si aucun réseau n’est enregistré, l’ESP32 crée un point d’accès local pour afficher une
               interface de configuration. Une fois le WiFi configuré, l’ESP32 se connecte automatiquement
               à ce réseau à chaque démarrage.
  Version    : 0.0.1
*/



#include <WiFi.h>            // Pour gérer le WiFi
#include <WebServer.h>       // Pour créer un serveur web local
#include <Preferences.h>     // Pour stocker SSID et mot de passe dans la mémoire flash

Preferences prefs;           // Objet pour accéder à la mémoire
WebServer server(80);        // Serveur web sur le port 80

// Fonction qui affiche la page de configuration WiFi
void handleRoot() {
  int n = WiFi.scanNetworks();  // Scan des réseaux disponibles
  String options = ""; 

  for (int i = 0; i < n; i++) {
    options += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>"; 
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuration WiFi</title></head><body>";
  html += "<h2>Configuration WiFi</h2>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Réseau WiFi :</label><br><select name='ssid'>" + options + "</select><br><br>";
  html += "<label>Mot de passe :</label><br><input type='password' name='password'><br><br>";
  html += "<button type='submit'>Se connecter</button></form><br><br>";
  html += "<form action='/reset' method='GET'><button type='submit'>Réinitialiser le WiFi</button></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Fonction qui enregistre le SSID et mot de passe
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  prefs.begin("wifi", false);       // Ouverture en écriture
  prefs.putString("ssid", ssid);    // Sauvegarde du SSID
  prefs.putString("password", password); // Sauvegarde du mot de passe
  prefs.end();                      // Fermeture

  server.send(200, "text/html", "<h2>WiFi enregistré ! Redémarrage...</h2>");
  delay(3000);
  ESP.restart();                    // Redémarrage de l’ESP32
}

// Fonction qui efface les identifiants WiFi et redémarre
void handleReset() {
  prefs.begin("wifi", false);
  prefs.clear();                    // Supprime SSID + mot de passe
  prefs.end();

  WiFi.disconnect(true);            // Déconnecte et efface les identifiants internes
  server.send(200, "text/html", "<h2>WiFi réinitialisé ! Redémarrage...</h2>");
  delay(3000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);             // Pour afficher les infos dans le moniteur série

  // Lecture des identifiants enregistrés
  prefs.begin("wifi", true);        
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid != "") {
    WiFi.begin(ssid.c_str(), password.c_str());  // Tentative de connexion
    Serial.print("Connexion à ");
    Serial.println(ssid);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      Serial.print(".");
      tries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnecté ! IP : " + WiFi.localIP().toString());

      // Serveur web actif même en mode connecté
      server.on("/", handleRoot);
      server.on("/save", handleSave);
      server.on("/reset", handleReset);
      server.begin();
      return;
    }
  }

  // Si pas de connexion, on lance le mode configuration
  Serial.println("\nMode configuration WiFi"); // Démarre en mode point d’accès
  WiFi.softAP("Capteur_Config");              // Crée un réseau local
  IPAddress IP = WiFi.softAPIP(); // Récupère l’IP du point d’accès
  Serial.println("Connectez-vous à : http://" + IP.toString()); // Affiche l’IP du point d’accès

  // Routes du serveur web
  server.on("/", handleRoot); // Page de configuration
  server.on("/save", handleSave); // Sauvegarde des identifiants
  server.on("/reset", handleReset); // Réinitialisation WiFi
  server.begin();
}

void loop() {
  server.handleClient();  // Gère les requêtes HTTP
}
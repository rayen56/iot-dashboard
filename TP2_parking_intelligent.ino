/*
    - Servo-moteur      -> ouverture/fermeture de la barrière
    - Capteur ultrason   -> détection de véhicule / mesure de distance
    - Capteur DHT22      -> température ambiante
    - Capteur LDR        -> luminosité
    - LED + Buzzer       -> alarme si obstacle trop proche
    - Écran LCD I2C      -> affichage des informations
    - HTTP POST          -> envoi des données vers ThingSpeak (Write API Key)
    - HTTP GET           -> lecture d'une commande distante (Read API Key)

*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ----------------- WIFI -----------------
const char* ssid     = "Wokwi-GUEST";
const char* password = "";

// ----------------- THINGSPEAK -----------------
const char* server            = "http://api.thingspeak.com";
String writeAPIKey            = "QFD4TKDD3L9PMMXR";   
String readAPIKey             = "YPMJLPUH3F2KQKS4";    
unsigned long channelID        = 3429397;                 
const int fieldCommande        = 1;                        

unsigned long lastUpdate = 0;
const unsigned long updateInterval = 20000; 

// ----------------- PINS -----------------
#define TRIG_PIN     5
#define ECHO_PIN     18
#define SERVO_PIN    13
#define DHT_PIN      4
#define DHT_TYPE     DHT22
#define LDR_PIN      34   // pin analogique (ADC1)
#define LED_PIN      2
#define BUZZER_PIN   15

#define SEUIL_DISTANCE_CM 10.0   // distance critique déclenchant l'alarme

// ----------------- OBJETS -----------------
Servo barriere;
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); // adresse I2C courante

bool barriereOuverte = false;

// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  barriere.attach(SERVO_PIN);
  barriere.write(0);         
  barriereOuverte = false;

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Demarrage...");

  connecterWiFi();

  delay(1500);
  lcd.clear();
}

// ============================================================
void loop() {

  // 1) Mesure de la distance (capteur ultrason)
  float distance = mesurerDistance();

  // 2) Mesure de température et humidité (DHT22)
  float temperature = dht.readTemperature();
  float humidite    = dht.readHumidity();
  if (isnan(temperature)) temperature = 0;
  if (isnan(humidite))    humidite = 0;

  // 3) Mesure de la luminosité (LDR)
  int luminosite = analogRead(LDR_PIN);

  // 4) Gestion de l'alarme (LED + buzzer)
  if (distance > 0 && distance <= SEUIL_DISTANCE_CM) {
    digitalWrite(LED_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // 5) Gestion automatique de la barrière selon la distance
  //    (véhicule détecté proche de l'entrée -> ouverture)
  if (distance > 0 && distance <= 30.0 && !barriereOuverte) {
    ouvrirBarriere();
  } else if (distance > 30.0 && barriereOuverte) {
    fermerBarriere();
  }

  // 6) Affichage sur l'écran LCD I2C
  afficherLCD(distance, temperature, luminosite);

  // 7) Communication avec ThingSpeak (toutes les 20 secondes)
  if (millis() - lastUpdate > updateInterval) {
    envoyerDonneesThingSpeak(distance, temperature, luminosite);
    int commande = lireCommandeThingSpeak();
    appliquerCommandeDistante(commande);
    lastUpdate = millis();
  }

  delay(1000);
}

// ============================================================
// Connexion WiFi
void connecterWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");

  int essais = 0;
  while (WiFi.status() != WL_CONNECTED && essais < 30) {
    delay(500);
    Serial.print(".");
    essais++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecte, IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nEchec de connexion WiFi");
  }
}

// ============================================================
// Mesure de distance via capteur ultrason
float mesurerDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duree = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duree == 0) return -1; 

  float distance = duree * 0.0343 / 2.0;
  return distance;
}

// ============================================================
// Ouverture / fermeture de la barrière
void ouvrirBarriere() {
  barriere.write(90);
  barriereOuverte = true;
  Serial.println("Barriere ouverte");
}

void fermerBarriere() {
  barriere.write(0);
  barriereOuverte = false;
  Serial.println("Barriere fermee");
}

// ============================================================
// Affichage LCD
void afficherLCD(float distance, float temperature, int luminosite) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("D:");
  lcd.print(distance, 1);
  lcd.print("cm T:");
  lcd.print(temperature, 1);

  lcd.setCursor(0, 1);
  lcd.print("Lum:");
  lcd.print(luminosite);
  lcd.print(barriereOuverte ? " OUV" : " FER");
}

// ============================================================
// Envoi des données vers ThingSpeak (HTTP POST avec Write API Key)
void envoyerDonneesThingSpeak(float distance, float temperature, int luminosite) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(server) + "/update";

  String postData = "api_key=" + writeAPIKey +
                     "&field1=" + String(distance) +
                     "&field2=" + String(temperature) +
                     "&field3=" + String(luminosite) +
                     "&field4=" + String(barriereOuverte ? 1 : 0);

  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(postData);

  if (code > 0) {
    Serial.println("ThingSpeak POST reussi, code: " + String(code));
  } else {
    Serial.println("Erreur ThingSpeak POST: " + http.errorToString(code));
  }
  http.end();
}

// ============================================================
// Lecture d'une commande distante depuis ThingSpeak
int lireCommandeThingSpeak() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  HTTPClient http;
  String url = String(server) + "/channels/" + String(channelID) +
               "/fields/" + String(fieldCommande) +
               "/last.txt?api_key=" + readAPIKey;

  http.begin(url);
  int code = http.GET();
  int commande = -1;

  if (code == 200) {
    String reponse = http.getString();
    reponse.trim();
    if (reponse.length() > 0) {
      commande = reponse.toInt();
    }
  } else {
    Serial.println("Erreur ThingSpeak GET: " + String(code));
  }
  http.end();
  return commande;
}

// ============================================================
// Application de la commande distante reçue (ex : 1=ouvrir, 0=fermer)
void appliquerCommandeDistante(int commande) {
  if (commande == 1 && !barriereOuverte) {
    ouvrirBarriere();
  } else if (commande == 0 && barriereOuverte) {
    fermerBarriere();
  }
}
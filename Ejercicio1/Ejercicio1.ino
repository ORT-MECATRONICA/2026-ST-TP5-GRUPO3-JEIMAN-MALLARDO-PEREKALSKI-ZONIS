//Grupo 3: Kenai Jeiman, Marco Mallardo, Ramiro Perekalski, Martín Zonis
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <Wire.h>
#include <DHT.h>
#include <U8g2lib.h>
#include "time.h"

#define DHTPIN 23        
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

#define SW1 34
#define SW2 35

typedef enum {
  RST,
  P1,
  P2,
  P1AP2,
  P2AP1
} estados_t;

estados_t maquinaPantalla = RST;
#define WIFI_SSID "MECA-IoT-V2"
#define WIFI_PASSWORD "IoT$2026"
#define Web_API_KEY "AIzaSyCH-6lkPl6yPjd0TNlXiQBuVp6oKSCBi68"
#define DATABASE_URL "https://st-tp5-g3-default-rtdb.firebaseio.com/"
#define USER_EMAIL "marcomallardo1903@gmail.com"
#define USER_PASS "Marco1"

void processData(AsyncResult &aResult);
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

unsigned long lastSendTime = 0;
unsigned long sendInterval = 30000; //default es 30seg
long int millisUltimoCheck = 0;

String uid;
String databasePath;
String tempPath = "/temperature";
String timePath = "/timestamp";
String parentPath;

int timestamp;
const char* ntpServer = "pool.ntp.org";

float temperature = 0.0;

// Objetos JSON
object_t jsonData, obj1, obj2;
JsonWriter writer;

// Variables para el control de botones
bool SW1Presionado = false;
bool SW2Presionado = false;

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("\nConectado a WiFi!");
}

// Mantenemos getTime solo para el nombre de la ruta en Firebase (no admite "/")
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return(0);
  }
  time(&now);
  return now;
}

// Función solicitada para guardar en el JSON
String obtenerTiempoReal() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "Sin hora"; 
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

void setup() {
  Serial.begin(115200);

  pinMode(SW1, INPUT);
  pinMode(SW2, INPUT);

  dht.begin();
  u8g2.begin();

  initWiFi();
  configTime(-10800, 0, ntpServer); //adaptado a gmt-3

  // Configurar cliente SSL
  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(5);

  // Iniciar Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

void loop() {
  app.loop();

  bool sw1Activo = !digitalRead(SW1);
  bool sw2Activo = !digitalRead(SW2);

  // Lectura del sensor de temperatura cada 5 segundos
  if (millis() - millisUltimoCheck >= 5000) {
    float t = dht.readTemperature();
    if (!isnan(t)) {
      temperature = t;
    }
    millisUltimoCheck = millis();
  }

  switch (maquinaPantalla) {
    
    case RST:
      maquinaPantalla = P1;
      break;

    case P1:
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.setCursor(0, 15);
      u8g2.print("VA : ");
      u8g2.print(temperature);
      u8g2.sendBuffer();

      if (sw1Activo) SW1Presionado = true;
      if (sw2Activo) SW2Presionado = true;

      if (!sw1Activo && SW1Presionado && !sw2Activo) {
        SW1Presionado = false;
      }
      if (!sw2Activo && SW2Presionado && !sw1Activo) {
        SW2Presionado = false;
      }

      if (SW1Presionado && SW2Presionado) {
        SW1Presionado = false;
        SW2Presionado = false;
        maquinaPantalla = P1AP2;
      }
      break;

    case P1AP2:
      // Esperar a que se suelten ambos botones para entrar a P2
      if (!sw1Activo && !sw2Activo) {
        maquinaPantalla = P2;
      }
      break;

    case P2:
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.setCursor(0, 15);
      u8g2.print("ciclo: ");
      u8g2.print(sendInterval / 1000);
      u8g2.sendBuffer();

      if (sw1Activo) SW1Presionado = true;
      if (sw2Activo) SW2Presionado = true;

      // Aumentar intervalo (Unicamente SW1 apretado y luego soltado)
      if (!sw1Activo && SW1Presionado && !sw2Activo) {
        sendInterval += 30000;
        SW1Presionado = false;
      }
      
      // Disminuir intervalo (Unicamente SW2 apretado y luego soltado)
      if (!sw2Activo && SW2Presionado && !sw1Activo) {
        if (sendInterval > 30000) { // Límite mínimo de 30 segundos
          sendInterval -= 30000;
        }
        SW2Presionado = false;
      }

      if (SW1Presionado && SW2Presionado) {
        SW1Presionado = false;
        SW2Presionado = false;
        maquinaPantalla = P2AP1;
      }
      break;

    case P2AP1:
      // Esperar a que se suelten ambos botones para entrar a P1
      if (!sw1Activo && !sw2Activo) {
        maquinaPantalla = P1;
      }
      break;
  }

  // --- ENVÍO DE DATOS A FIREBASE ---
  if (app.ready()) {
    unsigned long currentTime = millis();
    if (currentTime - lastSendTime >= sendInterval) {
      lastSendTime = currentTime;

      uid = app.getUid().c_str();
      databasePath = "/UsersData/" + uid + "/readings";

      timestamp = getTime(); // Para la ruta en Firebase
      String fechaHoraFormateada = obtenerTiempoReal(); // Tu función para el JSON

      Serial.print("Enviando datos - time: ");
      Serial.println(fechaHoraFormateada);

      parentPath = databasePath + "/" + String(timestamp);

      // Crear y unir JSON
      writer.create(obj1, tempPath, temperature);
      writer.create(obj2, timePath, fechaHoraFormateada);
      writer.join(jsonData, 2, obj1, obj2);

      Database.set<object_t>(aClient, parentPath, jsonData, processData, "RTDB_Send_Data");
    }
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  if (aResult.isEvent()) Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());
  if (aResult.isDebug()) Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  if (aResult.isError()) Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
  if (aResult.available()) Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}
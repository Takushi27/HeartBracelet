#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <NTPClient.h>

int pulsePin = 0;    

const char *ssid = "";
const char *password = ""; 

volatile int BPM;                   
volatile int Signal;                
volatile int IBI = 600;             
volatile boolean Pulse = false;  
volatile boolean QS = false;
volatile int rate[10];                      
volatile unsigned long sampleCounter = 0;   
volatile unsigned long lastBeatTime = 0;    
volatile int P = 512;                       
volatile int T = 512;                       
volatile int thresh = 550;                  
volatile int amp = 100;                     
volatile boolean firstBeat = true;          
volatile boolean secondBeat = false;

hw_timer_t *timer = NULL;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

#define PROJECT_ID ""
#define API_KEY ""
#define URL ""

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
const unsigned long saveDuration = 120000;
unsigned long lastSaveTime = 0;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

float voltage = 0.0;

int smoothSignal(int newSignal) {
  static int buffer[5] = {0};  
  static int index = 0;
  buffer[index] = newSignal;
  index = (index + 1) % 5;  
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += buffer[i];
  }
  return sum / 5;  
}

void detectHeartBeat() {
  Signal = smoothSignal(analogRead(pulsePin));              
  sampleCounter += 2;                         
  int N = sampleCounter - lastBeatTime;       

  if (Signal < thresh && N > (IBI / 5) * 3) {
    if (Signal < T) {
      T = Signal;
    }
  }

  if (Signal > thresh && Signal > P) {
    P = Signal;
  }

  if (N > 500) {
    if ((Signal > thresh) && (Pulse == false) && (N > (IBI / 5) * 3)) {
      Pulse = true;
      IBI = sampleCounter - lastBeatTime;
      lastBeatTime = sampleCounter;
      
      long runningTotal = 0;
      for (int i = 0; i < 9; i++) {
        rate[i] = rate[i + 1];
        runningTotal += rate[i];
      }
      rate[9] = IBI;
      runningTotal += rate[9];
      runningTotal /= 10;
      BPM = 60000 / runningTotal; 
      QS = true;
    }
  }

  if (Signal < thresh && Pulse == true) {
    Pulse = false;
    amp = P - T;
    thresh = amp / 2 + T;
    P = thresh;
    T = thresh;
  }

  if (N > 2500) {
    thresh = 512;
    P = 512;
    T = 512;
    lastBeatTime = sampleCounter;
    firstBeat = true;
    secondBeat = false;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(4, OUTPUT);
  
  WiFi.begin(ssid, password);
  
  Serial.println("Conectando ao Wi-Fi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Tentando conectar...");
  }

  Serial.println("Conectado ao Wi-Fi!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
  
  timeClient.begin();

  config.api_key = API_KEY;
  config.database_url = URL;

  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Usuário Cadastrado");
    signupOK = true;
  } else {
    Serial.println("Falha no cadastro");
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  timer = timerBegin(0, 80, true);  
  timerAttachInterrupt(timer, &detectHeartBeat, true);  
  timerAlarmWrite(timer, 2000, true);  
  timerAlarmEnable(timer);  
}

void connection(){
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(4, LOW);
    Serial.println("Wi-Fi desconectado!");
  } else {
    digitalWrite(4, HIGH);
  }
}

void firebaseDates() {
  if (QS == true) {
    Serial.print("Heart-Beat Found ");
    Serial.print("BPM: ");
    Serial.println(BPM);

    lastBeatTime = millis();
    QS = false; 
  }

  if (BPM > 0 && BPM < 200) {  
    Serial.print("Real-Time BPM: ");
    Serial.println(BPM);
  }

  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  int hour = ptm->tm_hour - 0;
  if (hour < 0) hour += 24;
  
  int minute = ptm->tm_min;
  int second = ptm->tm_sec;


  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();  

    if (BPM >= 30 && BPM < 200) {  
    
      char uniqueID[50];
      sprintf(uniqueID, "%04d-%02d-%02d_%02d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, hour, minute, second);

      FirebaseJson json;
      json.set("BPM", BPM);

      char date[30];
      sprintf(date, "%02d:%02d:%02d", hour, minute, second);
      json.set("Hora", date);

      String data = "bracelet/" + String(uniqueID);
      if (Firebase.RTDB.setJSON(&fbdo, data.c_str(), &json)) {
        Serial.println("Dados enviados com sucesso!");
        Serial.print("Hora atual: ");
        Serial.println(date);
      } else {
        Serial.println("Erro ao enviar: " + fbdo.errorReason());
      }
    } else {
      Serial.println("BPM inválido. Não enviando para o Firebase.");
    }

    if (millis() - lastSaveTime > saveDuration) {
    
      String path = "bracelet"; 
      if (Firebase.RTDB.deleteNode(&fbdo, path.c_str())) {
        Serial.println("Todos os dados apagados com sucesso!");
      } else {
        Serial.println("Erro ao apagar: " + fbdo.errorReason());
      }
      lastSaveTime = millis(); 
    }
  }
}

void loop(){
  connection();
  delay(2000);
  firebaseDates();
}
  
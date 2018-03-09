#define Project_DEBUG

#ifdef Project_DEBUG
#define DEBUG_Serial Serial
#define DEBUG_print(...)    DEBUG_Serial.print(__VA_ARGS__)
#define DEBUG_println(...)  DEBUG_Serial.println(__VA_ARGS__)
#define DEBUG_begin(...)    DEBUG_Serial.begin(__VA_ARGS__)
#define DEBUG_printf(...)   DEBUG_Serial.printf(__VA_ARGS__)
#define DEBUG_flush(...)    DEBUG_Serial.flush(__VA_ARGS__)
#define DEBUG_var(token)  { DEBUG_print( #token " = "); DEBUG_println(token); }
#else
#define DEBUG_Serial
#define DEBUG_print(...)
#define DEBUG_println(...)
#define DEBUG_begin(...)
#define DEBUG_printf(...)
#define DEBUG_flush(...)
#define DEBUG_var(token)
#endif

/*------------------ BEGIN INCLUDE ------------------*/
#include <Arduino.h>
#include <Hash.h>

//#include <ESP8266mDNS.h>
//#include <WiFiUdp.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>

#include <SPI.h>
#include "MFRC522.h"

#include <list>     // подключаем заголовок списка
#include <iterator> // заголовок итераторов
/*------------------ END INCLUDE ------------------*/

ESP8266WiFiMulti WiFiMulti;
HTTPClient httpPi;

/*------------------ BEGIN hash_t ------------------*/
#define size_hash 7

typedef struct {
	uint8_t hash[size_hash];
} hash_t;

bool operator < ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return a.hash[i] < b.hash[i];
	}
	return false;
}

bool operator > ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return a.hash[i] > b.hash[i];
	}
	return false;
}

bool operator == ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return false;
	}
	return true;
}

// A predicate
// bool single_digit(const hash_t & value) {
// 	const hash_t con_1 = {0x04, 0x0B, 0x22, 0xB2, 0xB1, 0x56, 0xFF};
// 	return (value < con_1 );
// }
/*------------------ END hash_t ------------------*/

/* wiring the MFRC522 to ESP8266 (ESP-12)
  RST     = GPIO5  D1
  SDA(SS) = GPIO4  D2
  MOSI    = GPIO13 D7
  MISO    = GPIO12 D6
  SCK     = GPIO14 D5
  GND     = GND
  BUZER   = D4 (inverse)
  RELAY   = D3
  3.3V    = 3.3V
  IRQ     = D8
*/

#define RELAY D3
#define BUSER D4
#define RST_PIN         D1           // Configurable, see typical pin layout above
#define SS_PIN          D2           // Configurable, see typical pin layout above
#define IRQ_PIN         D8           // Configurable, depends on hardware

const char * const ssid[3]     = { "wave-rtf"           , "Casper"             , "BML"      };
const char * const password[3] = { "2764637bodapidgorni", "2764637casper201206", "BMLadmin" };

//                        "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E"
//const char fingerprint[] = "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E";
const char urlLog[] = "http://iot.kpi.ua/web/api/uid";

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

char _hostname[14]; // "RFID_%08X"

void Beep(unsigned int time0, unsigned int time1, int N);
void RelayTrigger();
void dump_byte_array(byte * buffer, byte bufferSize);
void BuildBuffer(char* uid, byte uidSize);
void GetInstructions(String jsonConfig);
void updateESP(String FileBin);


void setup() {
  WiFi.persistent(false); // пароль буде записано на флеш, лише якщо поточні значення не відповідають тому, що вже зберігається у флеш-пам’яті.
  DEBUG_begin(115200);
  DEBUG_Serial.setDebugOutput(true);
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);
  pinMode(BUSER, OUTPUT);
  digitalWrite(BUSER, HIGH);
  ESP.wdtEnable(WDTO_8S);
  /* setup the IRQ pin*/
  //pinMode(IRQ_PIN, INPUT_PULLUP);
  /*Activate the interrupt*/
  //attachInterrupt(IRQ_PIN, readCard, FALLING);
  DEBUG_println(F("Booting...."));

  SPI.begin();           // Init SPI bus
  mfrc522.PCD_Init();    // Init MFRC522

  for (int i = 0; i < 1; ++i)
    WiFiMulti.addAP(ssid[i], password[i]);

  httpPi.setReuse(true);

  Serial.println("Connecting Wifi...");
    if(WiFiMulti.run() == WL_CONNECTED) {
        DEBUG_println("");
        DEBUG_println("WiFi connected");
        DEBUG_println("IP address: ");
        DEBUG_println(WiFi.localIP());
    }

    // Hostname defaults to esp8266-[ChipID]
    sprintf(_hostname, "RFID_%08X", ESP.getChipId());
    DEBUG_println(_hostname);

    DEBUG_println(F("Ready!"));
    DEBUG_println(F("======================================================"));
    DEBUG_println(F("Scan for Card and print UID:"));
}

void loop() {
  if(WiFiMulti.run() != WL_CONNECTED) {
    DEBUG_println("WiFi reconnected!");
    //delay(1000);
  }
  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  //const byte* uid_selCard = mfrc522.uid.uidByte;
  //const byte uid_size = mfrc522.uid.size;
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  Beep(100, 0 , 1);

  // Show some details of the PICC (that is: the tag/card)
  DEBUG_print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  //dump_byte_array(uid_selCard, uid_size);
  DEBUG_println();
  //mfrc522.PICC_HaltA();
  //mfrc522.PCD_StopCrypto1();
}

/**
 * @param time0 Час звучання
 * @param time1 Час спокою
 * @param N     Кількість піків
 *
 * Опис:
*/
void Beep(unsigned int time0, unsigned int time1, int N) {
  for (int i = 0; i < N; i++) {
    digitalWrite(BUSER, LOW);
    delay(time0);
    digitalWrite(BUSER, HIGH);
    delay(time1);
  }
}

void RelayTrigger() {
  //if (open_door == false)
  digitalWrite(RELAY, HIGH);
  Beep(50, 100 , 2); delay(500);
  digitalWrite(RELAY, LOW);
}


// Helper routine to dump a byte array as hex values to DEBUG_
void dump_byte_array(byte * buffer, byte bufferSize) {
  int j = 0;
  char sBuffer[bufferSize * 2 + 1];
  for (byte i = 0; i < bufferSize; i++) {
    j += sprintf(sBuffer + j, "%02X", buffer[i]);
  }
  DEBUG_println(sBuffer);
  //BuildBuffer(sBuffer, j);

  const byte aray_uid[] = { 0x04, 0x0B, 0xA5, 0xAA, 0xFF, 0x4A, 0x81,
                            0x04, 0x0B, 0x22, 0xB2, 0xB1, 0x56, 0x85 };

  const int byte_UID = 7;
  const int max_i = sizeof(aray_uid)/byte_UID;
  DEBUG_println(sizeof(aray_uid)/byte_UID );

  for (int i = 0; i < max_i; i++) {
    for (int k = 0; k < byte_UID; k++) {
      if (buffer[k] == aray_uid[i*byte_UID + k]) {
        DEBUG_print("ok :");
        DEBUG_println(buffer[k], HEX);
        if (k == byte_UID - 1) {
          RelayTrigger();
          //open_door = true;
          BuildBuffer(sBuffer, j);
          return;
        }
      } else {
        DEBUG_print("no :");
        DEBUG_print(buffer[j], HEX);
        DEBUG_print(" : ");
        DEBUG_println(aray_uid[i*byte_UID+j], HEX);
        break;
      }
    }
  }
  //open_door = false;
  BuildBuffer(sBuffer, j);
  //Beep(100, 200 , 5);
}

/************************************************
   Build buffer
 ************************************************/
void BuildBuffer(char* uid, byte uidSize) {
  unsigned int j = 0;
  char* buffer = (char*) malloc(sizeof(urlLog) / sizeof(char) + 5 + sizeof(_hostname) / sizeof(char) + 5 + uidSize + 1); // uid.length()
  //char buffer[sizeof(urlLog) / sizeof(char) + 5 + sizeof(_hostname) / sizeof(char) + 5 + uidSize + 1]; // uid.length()
  //memset(buffer, '\0', sizeof(buffer) );
  //sprintf(buffer, "%s?esp=%s&uid=%s", urlLog, uid);
  j += sprintf(buffer, urlLog);
  j += sprintf(buffer + j, "?esp=");
  j += sprintf(buffer + j, _hostname);
  j += sprintf(buffer + j, "&uid=");
  for (int i = 0; i < uidSize; i++, j++) {
    buffer[j] = uid[i];
  }
  buffer[j++] = '\0';
  DEBUG_print("Lenght buffer = ");
  DEBUG_println(j);
  DEBUG_println(sizeof(urlLog) / sizeof(char) + 5 + sizeof(_hostname) / sizeof(char) + 5 + uidSize);
  //http://192.168.88.253/rfid/web/api/uid??esp=RFID_771906&uid=047710A2205284
  DEBUG_println(buffer);

  if ((WiFiMulti.run() == WL_CONNECTED)) {

    DEBUG_print("[HTTP] begin...\n");
    // configure traged server and url
    httpPi.begin(buffer);//, fingerprint); //HTTP"http://work.rtf.kpi.ua/web/"

    DEBUG_print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = httpPi.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      DEBUG_printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        GetInstructions( httpPi.getString() );
      } else {
        DEBUG_println("Bad server!"); // Сервер відповів на запит негативно
        Beep(25, 25 , 15);
        DEBUG_println( httpPi.errorToString(httpCode).c_str() );
      }
    } else { // Сервер нічого не відповів
      DEBUG_printf("[HTTP] GET... failed, error: %s\n", httpPi.errorToString(httpCode).c_str());
      Beep(25, 100 , 5);
    }
    httpPi.end();
    DEBUG_print("dBm ");
    DEBUG_println(WiFi.RSSI());
  } else {
    Beep(25, 100 , 5);
    Beep(1000, 0 , 1);
    // wifi_conecting();
    // запис даних в ліст

  }
}

void GetInstructions(String jsonConfig) {
  //DEBUG_println(jsonConfig);
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(jsonConfig);
  if (!root.success()) {
    DEBUG_println("parseObject() failed");
    return;
  }
  #ifdef Project_DEBUG
    root.prettyPrintTo(DEBUG_Serial);
    DEBUG_println();
  #endif
  /*
    {
    "esp": "RFID_12625188",
    "uid": "04132F92D22F80",
    "user_id": 1,
    "location_id": 3,
    "file_bin": "",
    "status": 200
    }
  */

  switch(root["status"].as<int>()) {
    case 200: // Реєстрація
      Beep(500, 100 , 1);
      break;
    case 201: // вхід користувача
      Beep(50, 100 , 2);
      RelayTrigger();

      //delay(5000);
      break;
//    case 202: // вихід користувача
//      Beep(50, 50 , 4);
//      //delay(5000);
//      break;
    case 203: // оновленя прошивки
      updateESP( root["file_bin"].as<String>() );
      break;
    case 401: // не зареєстрований девайс
      Beep(500, 250 , 3);
      break;
    case 402: // не зареєстрований користувач
      Beep(100, 200 , 5);
      break;
    case 403: // 402 та 401
      break;
  }
}

void updateESP(String FileBin) {
  if (FileBin == "")
    return;
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    ESP.wdtFeed();
    ESP.wdtEnable(60000);
    t_httpUpdate_return ret = ESPhttpUpdate.update(FileBin);
    //t_httpUpdate_return  ret = ESPhttpUpdate.update("https://server/file.bin");

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        DEBUG_printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        DEBUG_println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        DEBUG_println("HTTP_UPDATE_OK");
        break;
    }

    ESP.wdtEnable(WDTO_8S);
  }
}

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
#define size_hash 20

#pragma pack(push, 1) // Розмір кластера вирівнювання 1
typedef struct {
	uint8_t hash[size_hash];
} hash_t;
#pragma pack(pop) // Розмір кластера вирівнювання стандартний (4)

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

const char * const ssid[]     = { "wave-rtf"           , "Casper"             , "BML"     , "Cossaks"  };
const char * const password[] = { "2764637bodapidgorni", "2764637casper201206", "BMLadmin", "asdf1234" };

//                        "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E"
//const char fingerprint[] = "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E";
const char urlLog[] = "http://iot.kpi.ua/web/api/uid";

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

char _hostname[14]; // "RFID_%08X"

typedef std::list<hash_t> hash_list;
hash_list UID;


void Beep(unsigned int time0, unsigned int time1, int N);
void RelayTrigger();
void dump_byte_array(byte * buffer, byte bufferSize);
void BuildBuffer(const hash_t & h);
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

  for (int i = 0; i < 4; ++i)
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

		DEBUG_print("SHA1:");
		DEBUG_print(sha1("abc"));
		//uint8_t hash[20];
		hash_t h;
		uint8_t data[] = {
			0x04, 0x0B, 0x22, 0xB2, 0xB1, 0x56, 0x85, // Перша картка
			(uint8_t)(ESP.getChipId() >> 24), (uint8_t)(ESP.getChipId() >> 16), // сіль
			(uint8_t)(ESP.getChipId() >> 8) , (uint8_t)(ESP.getChipId() >> 0)
		};
    sha1( data , sizeof(data) , &h.hash[0] );
		UID.push_back( h ); // Запис в ліст

		for (byte i = 0; i < sizeof(data) - 4; ++i) {
			data[i] = (uint8_t)( 0x040BA5AAFF4A81 >> (8*(sizeof(data)-5-i) ) ); // Друга картка
		}
    sha1( data , sizeof(data) , &h.hash[0] );
		UID.push_back( h ); // Запис в ліст

	#ifdef Project_DEBUG
		std::list<hash_t>::iterator UID_Iter;
		DEBUG_print("Before sorting: HASH_UID =\n");
		for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
			for (int i = 0; i < sizeof(hash_t); i++)
				printf("%02X ", (*UID_Iter).hash[i]);
			DEBUG_println();
		}
		DEBUG_println();
	#endif

		UID.sort(); // Відсортувати для правильної роботи інших функцій обробки!
		// UID.sort( operator > ); // Відсортувати за умовою
		UID.unique(); // Видалити повторення

	#ifdef Project_DEBUG
		DEBUG_print("After sorting: HASH_UID =\n");
		for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
			for (int i = 0; i < sizeof(hash_t); i++)
				printf("%02X ", (*UID_Iter).hash[i]);
			DEBUG_println();
		}
		DEBUG_println();
	#endif


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
void dump_byte_array(uint8_t * buffer, uint8_t bufferSize) {

	std::list<hash_t>::iterator UID_Iter;

	hash_t h;
	uint8_t data[bufferSize + 4];
	data[bufferSize+0] = (uint8_t)(ESP.getChipId() >> 24);
	data[bufferSize+1] = (uint8_t)(ESP.getChipId() >> 16);
	data[bufferSize+2] = (uint8_t)(ESP.getChipId() >> 8);
	data[bufferSize+3] = (uint8_t)(ESP.getChipId() >> 0);

	for (byte i = 0; i < bufferSize; ++i) {
		data[i] = buffer[i];
	}

	sha1( data , sizeof(data) , &h.hash[0] );
	UID_Iter = std::find( UID.begin(), UID.end(), h ); // Знайти об'єкт та повернути ітератор на нього
	if ( *UID_Iter == h ) {
		// Відкрити двері
		RelayTrigger();
		BuildBuffer( h );

#ifdef Project_DEBUG
		for (int i = 0; i < sizeof(hash_t); i++)
			printf("%02X ", (*UID_Iter).hash[i]);
#endif
		//
	} else {
		// З'єднатися з сервером та перевірити чи це не є реєстрація нового користувача
		// UID.push_back( h ); // Запис в ліст
		BuildBuffer( h );
	}




	// const hash_t con = {0x04, 0x0B, 0x22, 0xB3, 0x00, 0x00, 0x00};
	// UID_Iter = std::find( UID.begin(), UID.end(), con ); // Знайти об'єкт та повернути ітератор на нього
	// UID.insert(UID_Iter, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} ); // Вставити цей об'єкт на місце ітератора
	// UID.remove({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}); // Видалити цей об'єкт
	// UID.remove_if(single_digit); // Видалити всі об'єкти за умовою
}

/************************************************
   Build bufferv
 ************************************************/
void BuildBuffer(const hash_t & h) {
  char* buffer = (char*) malloc(sizeof(urlLog) + 5 + sizeof(_hostname) + 5 + sizeof(hash_t)*2 + 1); // uid.length()
  //char buffer[sizeof(urlLog) / sizeof(char) + 5 + sizeof(_hostname) / sizeof(char) + 5 + uidSize + 1]; // uid.length()
  //memset(buffer, '\0', sizeof(buffer) );
  byte j = sprintf(buffer, "%s?esp=%s&uid=", urlLog, _hostname);
	for (int i = 0; i < sizeof(hash_t); ++i) {
		j += sprintf(buffer + j, "%02X", h.hash[i]);
	}
  buffer[j++] = '\0';
  DEBUG_print("Lenght payload = ");
  DEBUG_println(j);
	DEBUG_print("Lenght buffer = ");
  DEBUG_println(sizeof(urlLog) + 5 + sizeof(_hostname) + 5 + sizeof(hash_t)*2 + 1);
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

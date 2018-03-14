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
//#include <deque> 		// подключаем заголовочный файл деков
#include <queue>    // подключаем заголовочный файл очереди
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

bool operator <= ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return a.hash[i] < b.hash[i];
	}
	return true;
}

bool operator >= ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return a.hash[i] > b.hash[i];
	}
	return true;
}

bool operator == ( const hash_t & a, const hash_t & b ) {
	for (int i = 0; i < size_hash; i++) {
		if (a.hash[i] != b.hash[i]) return false;
	}
	return true;
}

hash_t operator ^ ( const hash_t & a, const hash_t & b ) {
	hash_t res;
	for (int i = 0; i < size_hash; i++) {
		res.hash[i] = a.hash[i] ^ b.hash[i];
	}
	return res;
}

hash_t operator ^= ( const hash_t & a, const hash_t & b ) {
	return a^b;
}

hash_t operator + ( const hash_t & a, const hash_t & b ) {
	hash_t res;
	uint16_t buff = 0;
	for (int i = 0; i < size_hash; i++) {
		buff >>= 8;
		buff += a.hash[i] + b.hash[i];
		res.hash[i] = (uint8_t)buff;
		//bool overflow = __builtin_add_overflow( a.hash[i], b.hash[i], &res.hash[i] ); // '__builtin_add_overflow' was not declared in this scope
	}
	return res;
}

// A predicate
// bool single_digit(const hash_t & value) {
// 	const hash_t con_1 = {0x04, 0x0B, 0x22, 0xB2, 0xB1, 0x56, 0xFF};
// 	return (value < con_1 );
// }
/*------------------ END hash_t ------------------*/

struct info {
	uint64_t open : 1;
	uint64_t time : 63;
	hash_t uid;
};



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

const char * const ssid[]     = { "wave-rtf"           , "Casper"             , /*"BML"     ,*/ "Cossaks"  };
const char * const password[] = { "2764637bodapidgorni", "2764637casper201206", /*"BMLadmin",*/ "asdf1234" };
#define SSID 3

//                        "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E"
//const char fingerprint[] = "25 D4 98 65 88 94 30 B9 E9 62 3F 49 D3 A2 73 99 48 26 37 4E";
const char urlLog[] = "http://iot.kpi.ua/web/api/uid";

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

char _hostname[14]; // "RFID_%08X"

typedef std::list<hash_t> hash_list;
hash_list UID;


void Beep_s(unsigned int time0, unsigned int time1, int N);
void Beep(unsigned int freq, unsigned int time);
void RelayTrigger();
void dump_byte_array(byte * buffer, byte bufferSize);
void BuildBuffer(hash_t & h, const bool & open);
void BuildBuffer(info &inf);
void GetInstructions(String jsonConfig, hash_t &h, const bool & open);
void updateESP(String FileBin);

unsigned long start = millis();
std::queue<info> myQueue; // Створення черги

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

	WiFi.mode(WIFI_STA);
  for (int i = 0; i < SSID; ++i)
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

		hash_t h;
		uint8_t data[] = {
			0x04, 0x0B, 0x22, 0xB2, 0xB1, 0x56, 0x85, // Перша картка
			// (uint8_t)(ESP.getChipId() >> 24), (uint8_t)(ESP.getChipId() >> 16), // сіль
			// (uint8_t)(ESP.getChipId() >> 8) , (uint8_t)(ESP.getChipId() >> 0)
		};
    sha1( data , sizeof(data) , &h.hash[0] );
		UID.push_back( h ); // Запис в ліст

		for (byte i = 0; i < sizeof(data) - 4; ++i) {
			data[i] = (uint8_t)( 0x040BA5AAFF4A81 >> (8*(sizeof(data)-5-i) ) ); // Друга картка
		}
    sha1( data , sizeof(data) , &h.hash[0] );
		UID.push_back( h ); // Запис в ліст

		// UID.push_back( {0x13, 0x18, 0x33, 0x2D, 0xAE, 0xE6, 0xCE, 0xBE, 0xA9, 0x38, 0x74, 0xDF, 0x1B, 0xFE, 0x08, 0x83, 0xD5, 0x42, 0xA9, 0xEF} );

#ifdef Project_DEBUG
		std::list<hash_t>::iterator UID_Iter;
		DEBUG_print("Before sorting: HASH_UID =\n");
		for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
			for (int i = 0; i < sizeof(hash_t); i++)
				DEBUG_printf("%02X ", (*UID_Iter).hash[i]);
			DEBUG_printf("\n");
		}
		DEBUG_printf("\n");
#endif

		UID.sort(); // Відсортувати для правильної роботи інших функцій обробки!
		// UID.sort( operator > ); // Відсортувати за умовою
		UID.unique(); // Видалити повторення

#ifdef Project_DEBUG
		DEBUG_print("After sorting: HASH_UID =\n");
		for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
			for (int i = 0; i < sizeof(hash_t); i++)
				DEBUG_printf("%02X ", (*UID_Iter).hash[i]);
			DEBUG_printf("\n");
		}
		DEBUG_printf("\n");
#endif
    DEBUG_println(F("Ready!"));
    DEBUG_println(F("======================================================"));
    DEBUG_println(F("Scan for Card and print UID:"));
}

void loop() {
	//start;
  if(WiFiMulti.run() != WL_CONNECTED) {
    DEBUG_println("WiFi reconnected!");
    delay(100);
  }
	if (!myQueue.empty()) { // если дек не пуст
    // вывод на экран элементов дека
		// myQueue.size();
		BuildBuffer(myQueue.front());
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
  Beep_s(100, 0 , 1);

  // Show some details of the PICC (that is: the tag/card)
  DEBUG_print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  //dump_byte_array(uid_selCard, uid_size);
  DEBUG_println();
}

/**
 * @param time0 Час звучання
 * @param time1 Час спокою
 * @param N     Кількість піків
 *
 * Опис:
*/
void Beep_s(unsigned int time0, unsigned int time1, int N) {
  for (int i = 0; i < N; i++) {
    digitalWrite(BUSER, LOW);
    delay(time0);
    digitalWrite(BUSER, HIGH);
    delay(time1);
  }
}

/**
 * @param freq Частота звучання (ШІМ)
 * @param time Час звучання
 * @param ampl Гучність звучання
 *
 * Ноти:
 *E = 329		C5= 523.25
 *B = 493		D5=587.33
 *Fs = 698	E5=659.25
 *Es = 659	F5=698.46
 *Gs = 783	G5=783.99
 *G = 392		A5=880.00
 *A = 440
 *D = 587
 *F = 349
*/
void Beep(unsigned int freq, unsigned int time) {
	analogWriteFreq(freq);
	analogWrite(BUSER, PWMRANGE/2);
	delay(time);
}

void RelayTrigger() {
  //if (open_door == false)
  digitalWrite(RELAY, HIGH);
  Beep_s(50, 100 , 2); delay(500);
  digitalWrite(RELAY, LOW);
}


// Helper routine to dump a byte array as hex values to DEBUG_
void dump_byte_array(uint8_t * buffer, uint8_t bufferSize) {

	std::list<hash_t>::iterator UID_Iter;

	hash_t h;
	uint8_t data[bufferSize /*+ 4*/];
	// data[bufferSize+0] = (uint8_t)(ESP.getChipId() >> 24);
	// data[bufferSize+1] = (uint8_t)(ESP.getChipId() >> 16);
	// data[bufferSize+2] = (uint8_t)(ESP.getChipId() >> 8);
	// data[bufferSize+3] = (uint8_t)(ESP.getChipId() >> 0);

	for (byte i = 0; i < bufferSize; ++i) {
		data[i] = buffer[i];
	}

	sha1( data , sizeof(data) , &h.hash[0] );
	UID_Iter = std::find( UID.begin(), UID.end(), h ); // Знайти об'єкт та повернути ітератор на нього
	if ( *UID_Iter == h ) {
		// Відкрити двері
		RelayTrigger();
		BuildBuffer( h, true );
	} else {
		// З'єднатися з сервером та перевірити чи це не є реєстрація нового користувача
		// UID.push_back( h ); // Запис в ліст
		BuildBuffer( h, false );
	}

#ifdef Project_DEBUG
  DEBUG_printf("UID_Iter = ");
	for (int i = 0; i < sizeof(hash_t); i++)
		DEBUG_printf("%02X ", (*UID_Iter).hash[i]);
  DEBUG_printf("\nh = ");
	for (int i = 0; i < sizeof(hash_t); i++)
		DEBUG_printf("%02X ", h.hash[i]);
	DEBUG_printf("\n");
#endif
  // const hash_t con = {0x04, 0x0B, 0x22, 0xB3, 0x00, 0x00, 0x00};
	// UID_Iter = std::find( UID.begin(), UID.end(), con ); // Знайти об'єкт та повернути ітератор на нього
	// UID.insert(UID_Iter, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} ); // Вставити цей об'єкт на місце ітератора
	// UID.remove({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}); // Видалити цей об'єкт
	// UID.remove_if(single_digit); // Видалити всі об'єкти за умовою
#ifdef Project_DEBUG
	DEBUG_print("After sorting: HASH_UID =\n");
	for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
		for (int i = 0; i < sizeof(hash_t); i++)
			DEBUG_printf("%02X ", (*UID_Iter).hash[i]);
		DEBUG_printf("\n");
	}
	DEBUG_printf("\n");
#endif
}

/************************************************
   Build bufferv
 ************************************************/
void BuildBuffer(hash_t & h, const bool & open) {
  char* buffer = (char*) malloc(sizeof(urlLog) + 5 + sizeof(_hostname) + 5 + sizeof(open) + 5 + sizeof(hash_t)*2 + 1); // uid.length()
  byte j = sprintf(buffer, "%s?esp=%s&act=%d&uid=", urlLog, _hostname, open);
	for (int i = 0; i < sizeof(hash_t); ++i) {
		j += sprintf(buffer + j, "%02X", h.hash[i]);
	}
	buffer[j++] = '\0';
  DEBUG_print("Lenght payload = ");
  DEBUG_var(j);
	DEBUG_print("Lenght buffer = ");
  DEBUG_println(sizeof(urlLog) + 5 + sizeof(_hostname) + 5 + sizeof(open) + 5 + sizeof(hash_t)*2 + 1);
  //http://192.168.88.253/rfid/web/api/uid??esp=RFID_771906&uid=047710A2205284
	//http://iot.kpi.ua/web/api/uid?esp=RFID_0017E5C6&act=1&uid=1318332DAEE6CEBEA93874DF1BFE0883D542A9EF
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
        GetInstructions( httpPi.getString(), h , open);
      } else {
        DEBUG_println("Bad server!"); // Сервер відповів на запит негативно
        Beep_s(25, 25 , 15);
        DEBUG_println( httpPi.errorToString(httpCode).c_str() );
				// запис даних в ліст
				myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
      }
    } else { // Сервер нічого не відповів
      DEBUG_printf("[HTTP] GET... failed, error: %s\n", httpPi.errorToString(httpCode).c_str());
      Beep_s(25, 25 , 15);
			WiFi.disconnect(); // Перепідєднаємося!
			// запис даних в ліст
			myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
    }
    httpPi.end();
    DEBUG_print("dBm ");
    DEBUG_println(WiFi.RSSI());
  } else {
    Beep_s(25, 25 , 15);
    Beep_s(1000, 200 , 2);
    // запис даних в ліст
		myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
  }
}




void BuildBuffer(info &inf) {																					   // "sizeof(inf.time)"     "sizeof(inf.open)" invalid application of 'sizeof' to a bit-field
  char* buffer = (char*) malloc(sizeof(urlLog) + 5 + sizeof(_hostname) + 6 + sizeof(uint64_t) + 5 +        1         + 5 + sizeof(hash_t)*2 + 1); // uid.length()
  byte j = sprintf(buffer, "%s?esp=%s&time=%d&act=%d&uid=", urlLog, _hostname, inf.time ,inf.open);
	for (int i = 0; i < sizeof(hash_t); ++i) {
		j += sprintf(buffer + j, "%02X", inf.uid.hash[i]);
	}
	buffer[j++] = '\0';
  DEBUG_print("Lenght payload = ");
  DEBUG_var(j);
	DEBUG_print("Lenght buffer = ");											 												// "sizeof(inf.open)"
  DEBUG_println(sizeof(urlLog) + 5 + sizeof(_hostname) + 6 + sizeof(uint64_t) + 5 +        1         + 5 + sizeof(hash_t)*2 + 1);
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
        //GetInstructions( httpPi.getString(), h , open); // Не треба нічого робити
				DynamicJsonBuffer jsonBuffer;
			  JsonObject& root = jsonBuffer.parseObject(httpPi.getString());
				hash_t secret_xor = {0x01, 0x4A, 0x8F, 0xE5, 0xCC, 0xB1, 0x9B, 0xA6, 0x1C, 0x4C, 0x08, 0x73, 0xD3, 0x91, 0xE9, 0x87, 0x98, 0x2F, 0xBB, 0xD3};
				secret_xor ^= inf.uid;
			  if (root.success() && root["secret_key"].as<String>() == sha1(secret_xor.hash , sizeof(secret_xor)) ) {
			    myQueue.pop(); // Видалити цей запис з черги;
					DEBUG_println("Bad server!");
			  }
      } else {
        DEBUG_println("Bad server!"); // Сервер відповів на запит негативно
        Beep_s(25, 25 , 15);
        DEBUG_println( httpPi.errorToString(httpCode).c_str() );
      }
    } else { // Сервер нічого не відповів
      DEBUG_printf("[HTTP] GET... failed, error: %s\n", httpPi.errorToString(httpCode).c_str());
      Beep_s(25, 25 , 15);
			WiFi.disconnect(); // Перепідєднаємося!
    }
    httpPi.end();
    DEBUG_print("dBm ");
    DEBUG_println(WiFi.RSSI());
  }
}


void GetInstructions(String jsonConfig, hash_t &h, const bool & open) {
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
    "esp": "RFID_006A5B88",
    "uid": "A94A8FE5CCB19BA61C4C0873D391E987982FBBD3,
    "user_id": 1,
    "location_id": 3,
    "file_bin": "",
    "status": 200
		"secret_key": 0xA94A8FE5CCB19BA61C4C0873D391E987982FBBD3
    //secret_xor = {0x01, 0x4A, 0x8F, 0xE5, 0xCC, 0xB1, 0x9B, 0xA6, 0x1C, 0x4C, 0x08, 0x73, 0xD3, 0x91, 0xE9, 0x87, 0x98, 0x2F, 0xBB, 0xD3}
    }
  */
	ESP.wdtFeed(); // нагодувати WDT

	hash_t secret_xor = {0x01, 0x4A, 0x8F, 0xE5, 0xCC, 0xB1, 0x9B, 0xA6, 0x1C, 0x4C, 0x08, 0x73, 0xD3, 0x91, 0xE9, 0x87, 0x98, 0x2F, 0xBB, 0xD3};
	secret_xor ^= h;

  switch(root["status"].as<int>()) {
    case 200: // Реєстрація
			if(open) return; // Користувач вже є зареєстрований
			//sha1( secret_xor.hash , sizeof(secret_xor) , &h.hash[0] );
			if ( root["secret_key"].as<String>() == sha1(secret_xor.hash , sizeof(secret_xor)) ) {
			//if ( root["secret_key"].as<hash_t>() == h ) {
				UID.push_back( h ); // Запис в ліст
				UID.sort(); // Відсортувати для правильної роботи інших функцій обробки!
#ifdef Project_DEBUG
				std::list<hash_t>::iterator UID_Iter;
				DEBUG_print("After sorting: HASH_UID =\n");
				for ( UID_Iter = UID.begin( ); UID_Iter != UID.end( ); UID_Iter++ ) {
					for (int i = 0; i < sizeof(hash_t); i++)
						DEBUG_printf("%02X ", (*UID_Iter).hash[i]);
					DEBUG_printf("\n");
				}
				DEBUG_printf("\n");
#endif
				RelayTrigger(); // Відчинити двері
				Beep_s(500, 100 , 1);
			} else {
				DEBUG_println("Error secret_key!!!");
				Beep(523,500);  // 523 hertz (C5) for 500 milliseconds
				Beep(587,500);
				Beep(659,500);
				Beep(698,500);
				Beep(784,500);
				// запис даних в ліст
				myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
			}
      break;
    case 201: // вхід користувача
			if(open) return; // Користувач вже увійшов
			// Відкрити двері але не реєструвати користувача
			//sha1( secret_xor.hash , sizeof(secret_xor) , &h.hash[0] );
			if ( root["secret_key"].as<String>() == sha1(secret_xor.hash , sizeof(secret_xor)) ) {
			//if ( root["secret_key"].as<hash_t>() == h ) {
				RelayTrigger(); // Відчинити двері
				Beep_s(300, 100 , 3);
			} else {
				DEBUG_println("Error secret_key!!!");
				Beep(523,500);  // 523 hertz (C5) for 500 milliseconds
				Beep(587,500);
				Beep(659,500);
				Beep(698,500);
				Beep(784,500);
				// запис даних в ліст
				myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
			}
      break;
//    case 202: // вихід користувача
//      Beep_s(50, 50 , 4);
//      //delay(5000);
//      break;
    case 203: // оновленя прошивки
			if ( root["secret_key"].as<String>() == sha1(secret_xor.hash , sizeof(secret_xor)) )
      	updateESP( root["file_bin"].as<String>() );
      break;
    case 401: // не зареєстрований девайс
    case 402: // не зареєстрований користувач
    case 403: // 402 та 401
			Beep_s(500, 500 , 6);
			if(open) { // Користувач вже увійшов
				//sha1( secret_xor.hash , sizeof(secret_xor) , &h.hash[0] );
				if ( root["secret_key"].as<String>() == sha1(secret_xor.hash , sizeof(secret_xor)) ) {
				//if ( root["secret_key"].as<hash_t>() == h ) {
					// Не санкціонований вхід!
					UID.remove(h); // Видалити цей об'єкт
				} else { // Не довіряти серверу, та нічого не робити!
					DEBUG_println("Error secret_key!!!");
					Beep(523,500);  // 523 hertz (C5) for 500 milliseconds
					Beep(587,500);
					Beep(659,500);
					Beep(698,500);
					Beep(784,500);
					// запис даних в ліст
					myQueue.push( { .open = open, .time = millis(), .uid = h } ); // додати елемент в кінець
				}
			}
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

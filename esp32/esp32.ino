// Protospace Pinball Wizard
// Sends high scores to the portal
//
// Board: ESP32 Wrover Module

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>  // "LiquidCrystal I2C" by Marco Schwartz v1.1.2
#include <ArduinoJson.h>        // v6.19.4
#include <ElegantOTA.h>         // v2.2.9
//#include <WebSerial.h>          // v1.3.0

#include "secrets.h"
//#include "lets_encrypt_ca.h"

//String portalAPI = "https://api.my.protospace.ca";
String portalAPI = "https://api.spaceport.dns.t0.vc";

#define RX1_PIN 9
#define TX1_PIN 10
//HardwareSerial *gameSerial = &Serial;   	// for development
HardwareSerial *gameSerial = &Serial1;  	// for ATmega

#define GAME_DATA_DELAY_MS 500
#define CONTROLLER_DELAY_MS 2000
#define BONUS_WAIT_TIME 5000
#define CONNECT_TIMEOUT_MS 30000
#define ELLIPSIS_ANIMATION_DELAY_MS 1000
#define HEARTBEAT_INTERVAL_MS 1000 * 60 * 60  // hourly

#define NUM_MAX_PLAYERS      4

#define GAME_STATE_UNKNOWN   -1
#define GAME_STATE_IN_GAME   0
#define GAME_STATE_IDLE      1
#define GAME_STATE_SETTINGS  2
static const char* gameStateLabels[] = {"IN GAME", "IDLE", "SETTINGS"};
int gameState = GAME_STATE_UNKNOWN;

#define PLAYER_UNKNOWN   -1
#define PLAYER1           0
#define PLAYER2           1
#define PLAYER3           2
#define PLAYER4           3
static const char* playerNumberLabels[] = {"PLAYER1", "PLAYER2", "PLAYER3", "PLAYER4"};
static const char* playerNumberLabelsShort[] = {"P1", "P2", "P3", "P4"};
int playerNumber = PLAYER_UNKNOWN;

int playerScores[NUM_MAX_PLAYERS];
String scannedCard = "";
String playerCards[NUM_MAX_PLAYERS];
String playerNames[NUM_MAX_PLAYERS];

WiFiClientSecure wc;
WebServer server(80);

//void recvMsg(uint8_t *data, size_t len){
//	WebSerial.println("Received Data...");
//	String d = "";
//	for(int i=0; i < len; i++){
//		d += char(data[i]);
//	}
//	WebSerial.println(d);
//}

LiquidCrystal_I2C lcd(0x27, 16, 2);

void rebootArduino() {
	lcd.clear();
	lcd.print("REBOOTING...");
	delay(1000);
	ESP.restart();
}

enum controllerStates {
	CONTROLLER_BEGIN,
	CONTROLLER_WIFI_CONNECT,
	CONTROLLER_GET_TIME,
	CONTROLLER_HEARTBEAT,
	CONTROLLER_RESET,
	CONTROLLER_IDLE,
	CONTROLLER_IN_GAME,
	CONTROLLER_GET_NAME,
	CONTROLLER_WAIT_FOR_BONUS,
	CONTROLLER_SEND_SCORES,
	CONTROLLER_SEND_RETRY,
	CONTROLLER_SUCCESS,
	CONTROLLER_DELAY,
	CONTROLLER_WAIT,
};
enum controllerStates controllerState = CONTROLLER_BEGIN;

enum dataStates {
	DATA_START,
	DATA_GAME_STATE,
	DATA_ACTIVE_PLAYER,
	DATA_PLAYER_SCORES,
	DATA_FINISH,
	DATA_DELAY,
	DATA_WAIT,
};
enum dataStates dataState = DATA_START;


void processControllerState() {
	static unsigned long timer = millis();
	static enum controllerStates nextControllerState;
	static int statusCode;
	static StaticJsonDocument<1024> jsonDoc;
	static int retryCount;
	static String gameId;

	String response;
	String postData;
	bool failed;
	time_t now;
	struct tm timeinfo;
	int i;
	int result;
	HTTPClient https;

	switch (controllerState) {
		case CONTROLLER_BEGIN:
			Serial.println("[WIFI] Connecting...");

			timer = millis();
			controllerState = CONTROLLER_WIFI_CONNECT;
			break;

		case CONTROLLER_WIFI_CONNECT:
			lcd.setCursor(0,0);
			lcd.print("CONNECTING");
			for (i = 0; i < (millis() / ELLIPSIS_ANIMATION_DELAY_MS) % 4; i++) {
				lcd.print(".");
			}
			lcd.print("   ");

			if (WiFi.status() == WL_CONNECTED) {
				Serial.print("[WIFI] Connected. IP address: ");
				Serial.println(WiFi.localIP());

				lcd.setCursor(0,1);
				lcd.print(WiFi.localIP());

				Serial.println("[TIME] Setting time using NTP.");
				configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
				nextControllerState = CONTROLLER_GET_TIME;
				controllerState = CONTROLLER_DELAY;
			}

			if (millis() - timer > CONNECT_TIMEOUT_MS) {  // overflow safe
				WiFi.disconnect();
				WiFi.mode(WIFI_OFF);

				lcd.clear();
				lcd.print("CONNECT FAILED");
				lcd.setCursor(0,1);
				lcd.print("WAIT 5 MINS...");
				delay(300 * 1000);

				rebootArduino();
			}

			break;

		case CONTROLLER_GET_TIME:
			// time is needed to generate game ID

			lcd.setCursor(0,0);
			lcd.print("GETTING TIME");
			for (i = 0; i < (millis() / ELLIPSIS_ANIMATION_DELAY_MS) % 4; i++) {
				lcd.print(".");
			}
			lcd.print("   ");

			time(&now);
			if (now > 8 * 3600 * 2) {
				gmtime_r(&now, &timeinfo);
				Serial.print("[TIME] Current time in UTC: ");
				Serial.print(asctime(&timeinfo));

				Serial.println("[WIFI] Test connection to portal...");
				controllerState = CONTROLLER_HEARTBEAT;
			}

			break;

		case CONTROLLER_HEARTBEAT:
			// test connection to the portal

			lcd.clear();
			lcd.print("CHECK PORTAL...");

			result = https.begin(wc, portalAPI + "/stats/");

			if (!result) {
				Serial.println("[WIFI] https.begin failed.");
				lcd.clear();
				lcd.print("CONNECTION ERROR");
				nextControllerState = CONTROLLER_BEGIN;
				controllerState = CONTROLLER_DELAY;
				break;
			}

			result = https.GET();

			Serial.printf("[WIFI] Http code: %d\n", result);

			if (result != HTTP_CODE_OK) {
				Serial.printf("[WIFI] Portal GET failed, error:\n%s\n", https.errorToString(result).c_str());
				lcd.clear();
				lcd.print("BAD REQUEST: ");
				lcd.print(result);
				nextControllerState = CONTROLLER_BEGIN;
				controllerState = CONTROLLER_DELAY;
				break;
			}

			Serial.println("[WIFI] Heartbeat success.");
			lcd.setCursor(0,1);
			lcd.print("GOOD");

			controllerState = CONTROLLER_RESET;

			break;

		case CONTROLLER_RESET:
			gameState = GAME_STATE_UNKNOWN;
			playerNumber = PLAYER_UNKNOWN;
			time(&now);
			gameId = String(now);
			retryCount = 0;

			playerScores[0] = 0;
			playerScores[1] = 0;
			playerScores[2] = 0;
			playerScores[3] = 0;

			playerCards[0] = "";
			playerCards[1] = "";
			playerCards[2] = "";
			playerCards[3] = "";

			playerNames[0] = "";
			playerNames[1] = "";
			playerNames[2] = "";
			playerNames[3] = "";

			Serial.println("[GAME] Cleared game data.");

			lcd.clear();
			lcd.print("WAITING FOR    ");
			lcd.setCursor(0,1);
			lcd.print("GAME");

			timer = millis();
			controllerState = CONTROLLER_IDLE;
			break;

		case CONTROLLER_IDLE:
			if (gameState == GAME_STATE_IN_GAME) {
				time(&now);
				gameId = String(now);
				Serial.print("[GAME] Starting new game with ID: ");
				Serial.println(gameId);

				controllerState = CONTROLLER_IN_GAME;
				break;
			}

			if (millis() - timer > HEARTBEAT_INTERVAL_MS) {  // overflow safe
				controllerState = CONTROLLER_HEARTBEAT;
			}

			break;

		case CONTROLLER_IN_GAME:
			if (gameState == GAME_STATE_IDLE) {
				Serial.println("[GAME] Game over, sending scores...");
				lcd.clear();
				lcd.print("GAME OVER");
				timer = millis();
				controllerState = CONTROLLER_WAIT_FOR_BONUS;
				break;
			}

			if (playerNumber >= 0) {
				lcd.setCursor(0,0);

				bool nameIsSet = playerNames[playerNumber].length() > 0;
				if (nameIsSet) {
					lcd.print(playerNumberLabelsShort[playerNumber]);
					lcd.print(" ");
					lcd.print(playerNames[playerNumber]);
					lcd.print("             ");
				} else {
					lcd.print(playerNumberLabels[playerNumber]);
					lcd.print(" SCAN NOW");
				}

				lcd.setCursor(0,1);
				lcd.print("SCORE: ");
				lcd.print(playerScores[playerNumber]);
				lcd.print("                 ");
			}

			break;

		case CONTROLLER_GET_NAME:
			lcd.clear();
			lcd.print("CHECKING CARD       ");

			result = https.begin(wc, portalAPI + "/pinball/" + scannedCard + "/get_name/");

			if (!result) {
				Serial.println("[CARD] https.begin failed.");
				lcd.clear();
				lcd.print("CONNECTION ERROR");
				nextControllerState = CONTROLLER_HEARTBEAT;
				controllerState = CONTROLLER_DELAY;
				break;
			}

			//https.addHeader("Authorization", PINBALL_API_TOKEN);
			result = https.GET();

			Serial.printf("[CARD] Http code: %d\n", result);

			if (result != HTTP_CODE_OK) {
				Serial.printf("[CARD] Bad scan, error:\n%s\n", https.errorToString(result).c_str());
				lcd.clear();
				lcd.print("BAD SCAN: ");
				lcd.print(result);
				nextControllerState = CONTROLLER_IN_GAME;
				controllerState = CONTROLLER_DELAY;
				break;
			}

			response = https.getString();

			Serial.print("[CARD] Response: ");
			Serial.println(response);

			deserializeJson(jsonDoc, response);

			playerNames[playerNumber] = jsonDoc["name"].as<String>();
			playerCards[playerNumber] = scannedCard;

			controllerState = CONTROLLER_IN_GAME;
			break;

		case CONTROLLER_WAIT_FOR_BONUS:
			if (millis() - timer > BONUS_WAIT_TIME) {  // overflow safe
				controllerState = CONTROLLER_SEND_SCORES;
			}

			break;

		case CONTROLLER_SEND_SCORES:
			failed = false;
			for (i = 0; i < NUM_MAX_PLAYERS; i++) {
				bool playerUnclaimed = playerCards[i].length() == 0;
				if (playerUnclaimed) {
					continue;
				}

				result = https.begin(wc, portalAPI + "/pinball/score/");

				if (!result) {
					Serial.println("[SCORE] https.begin failed.");
					lcd.clear();
					lcd.print("CONNECTION ERROR");
					nextControllerState = CONTROLLER_SEND_RETRY;
					controllerState = CONTROLLER_DELAY;
					failed = true;
					break;
				}

				postData = "card_number="
					+ playerCards[i]
					+ "&game_id="
					+ gameId
					+ "&player="
					+ String(i+1)
					+ "&score="
					+ String(playerScores[i]);

				Serial.println("[SCORE] POST data:");
				Serial.println(postData);

				https.addHeader("Content-Type", "application/x-www-form-urlencoded");
				https.addHeader("Content-Length", String(postData.length()));
				//https.addHeader("Authorization", PINBALL_API_TOKEN);
				result = https.POST(postData);

				Serial.printf("[SCORE] Http code: %d\n", result);

				if (result != HTTP_CODE_OK) {
					Serial.printf("[SCORE] Bad send, error:\n%s\n", https.errorToString(result).c_str());
					lcd.clear();
					lcd.print("BAD SEND: ");
					lcd.print(result);
					nextControllerState = CONTROLLER_SEND_RETRY;
					controllerState = CONTROLLER_DELAY;
					failed = true;
					break;
				}

			}

			if (!failed) {
				controllerState = CONTROLLER_SUCCESS;
			}

			break;

		case CONTROLLER_SEND_RETRY:
			if (retryCount >= 5) {
				lcd.clear();
				lcd.print("SEND FAILED");
				lcd.setCursor(0,1);
				lcd.print("RESETTING...");
				nextControllerState = CONTROLLER_HEARTBEAT;
				controllerState = CONTROLLER_DELAY;
				break;
			}

			retryCount++;

			lcd.clear();
			lcd.print("RETRYING ");
			lcd.print(retryCount);
			lcd.print(" / 5");

			Serial.print("[GAME] Retrying ");
			Serial.print(retryCount);
			Serial.println(" / 5...");

			controllerState = CONTROLLER_SEND_SCORES;
			break;

		case CONTROLLER_SUCCESS:
			lcd.clear();
			lcd.print("SCORES SENT!");

			nextControllerState = CONTROLLER_HEARTBEAT;
			controllerState = CONTROLLER_DELAY;
			break;

		case CONTROLLER_DELAY:
			timer = millis();
			controllerState = CONTROLLER_WAIT;
			break;

		case CONTROLLER_WAIT:
			if (millis() - timer > CONTROLLER_DELAY_MS) {  // overflow safe
				controllerState = nextControllerState;
			}
			break;
	}

	return;
}

void processDataState() {
	static enum dataStates nextDataState;
	static unsigned long timer;

	switch (dataState) {
		case DATA_START:
			dataState = DATA_GAME_STATE;
			break;

		case DATA_GAME_STATE:
			Serial.println("Getting game state...");
			gameSerial->println("dump 169 1");
			nextDataState = DATA_ACTIVE_PLAYER;
			dataState = DATA_DELAY;
			break;

		case DATA_ACTIVE_PLAYER:
			Serial.println("Getting player number...");
			gameSerial->println("dump 173 1");
			nextDataState = DATA_PLAYER_SCORES;
			dataState = DATA_DELAY;
			break;

		case DATA_PLAYER_SCORES:
			Serial.println("Getting player scores...");
			gameSerial->println("dump 256 16");
			nextDataState = DATA_FINISH;
			dataState = DATA_DELAY;
			break;

		case DATA_FINISH:
			dataState = DATA_GAME_STATE;
			break;


		case DATA_DELAY:
			timer = millis();
			dataState = DATA_WAIT;
			break;

		case DATA_WAIT:
			if (millis() - timer > GAME_DATA_DELAY_MS) {  // overflow safe
				dataState = nextDataState;
			}
			break;
	}
}

void parseGameData(String data) {
	int num;

	if (data.startsWith("0x00A9:")) {  // game state
		num = data.substring(11, 12).toInt();

		if (num < 0 || num > 2) {
			return;
		}

		gameState = num;

		Serial.print("Set gamestate: ");
		Serial.println(gameStateLabels[gameState]);
	} else if (data.startsWith("0x00AD:")) {  // player number
		num = data.substring(11, 12).toInt();

		if (num < 0 || num > 3) {
			return;
		}

		playerNumber = num;

		Serial.print("Set player number: ");
		Serial.println(playerNumberLabels[playerNumber]);
	} else if (gameState == GAME_STATE_IN_GAME && data.startsWith("0x0100:")) {  // player scores
		String scoreStr;

		scoreStr = data.substring(10, 12)
			+ data.substring(15, 17)
			+ data.substring(20, 22)
			+ data.substring(25, 27);
		playerScores[PLAYER1] = scoreStr.toInt();

		scoreStr = data.substring(30, 32)
			+ data.substring(35, 37)
			+ data.substring(40, 42)
			+ data.substring(45, 47);
		playerScores[PLAYER2] = scoreStr.toInt();

		scoreStr = data.substring(50, 52)
			+ data.substring(55, 57)
			+ data.substring(60, 62)
			+ data.substring(65, 67);
		playerScores[PLAYER3] = scoreStr.toInt();

		scoreStr = data.substring(70, 72)
			+ data.substring(75, 77)
			+ data.substring(80, 82)
			+ data.substring(85, 87);
		playerScores[PLAYER4] = scoreStr.toInt();

		Serial.print("Set player scores.");
	}
}

void setup()
{
	Serial.begin(115200);
	Serial.setTimeout(50);

	if (*gameSerial == Serial1) {
		Serial.println("Game serial configured as Serial1 (ATmega).");
		gameSerial->begin(115200, SERIAL_8N1);   //, RX1_PIN, TX1_PIN);
		gameSerial->setTimeout(50);
	}

	Serial2.begin(9600);
	Serial2.setTimeout(50);

	Serial.println("Host boot up");
	Serial.println("Waiting 2 seconds...");

	delay(2000);

	lcd.init();
	lcd.backlight();

	lcd.clear();
	lcd.print("BOOT UP");

	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASS);

	//X509List cert(lets_encrypt_ca);
	//wc.setTrustAnchors(&cert);
	wc.setInsecure();  // disables all SSL checks. don't use in production

	server.on("/", []() {
			server.send(200, "text/plain", "<i>SEE YOU PINBALL WIZARD...</i>");
			});

	ElegantOTA.begin(&server);
	//WebSerial.begin(&server);
	//WebSerial.msgCallback(recvMsg);
	server.begin();

	Serial.println("Setup complete.");
	delay(500);
}

void loop()
{
	if (Serial.available() > 0)
	{
		String data = Serial.readString();
		data.trim();

		gameSerial->println(data);
	}

	if (gameSerial->available() > 0)
	{
		String data = gameSerial->readString();
		data.trim();
		Serial.print("Game serial data: ");
		Serial.println(data);

		if (data.length() > 8) {
			parseGameData(data);
		}
	}

	if (Serial2.available() > 0)
	{
		String data = Serial2.readString();

		Serial.print("RFID scan: ");
		Serial.print(data);
		Serial.print(", len: ");
		Serial.println(data.length());

		if (data.substring(1, 11) == "0700B5612A") {
			rebootArduino();
		}

		if (controllerState == CONTROLLER_IN_GAME && playerNumber >= 0) {
			bool nameIsSet = playerNames[playerNumber].length() > 0;

			if (!nameIsSet) {
				scannedCard = data.substring(1, 11);

				Serial.print("Card: ");
				Serial.println(scannedCard);

				controllerState = CONTROLLER_GET_NAME;
			}
		}

	}

	processControllerState();
	processDataState();
	server.handleClient();
}

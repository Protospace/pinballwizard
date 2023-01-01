#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <WebSerial.h>

#include "secrets.h"
//#include "lets_encrypt_ca.h"

WiFiClientSecure wc;
WebServer server(80);

void recvMsg(uint8_t *data, size_t len){
	WebSerial.println("Received Data...");
	String d = "";
	for(int i=0; i < len; i++){
		d += char(data[i]);
	}
	WebSerial.println(d);
}

void (* rebootArduino) (void) = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

enum controllerStates {
	BEGIN,
	WIFI_CONNECT,
	GET_TIME,
	HEARTBEAT,
	FREEZE,
};

enum controllerStates controllerState = BEGIN;

String scannedCard = "";

void processControllerState() {
	static unsigned long timer = millis();
	static int statusCode;
	static StaticJsonDocument<1024> jsonDoc;

	time_t now = time(nullptr);
	struct tm timeinfo;
	int i;
	int result;
	HTTPClient https;

	switch (controllerState) {
		case BEGIN:
			Serial.println("[WIFI] Connecting...");
			controllerState = WIFI_CONNECT;
			break;

		case WIFI_CONNECT:
			lcd.setCursor(0,0);
			lcd.print("CONNECTING");
			for (i = 0; i < (millis() / 1000) % 4; i++) {
				lcd.print(".");
			}
			lcd.print("   ");

			if (WiFi.status() == WL_CONNECTED) {
				Serial.print("[WIFI] Connected. IP address: ");
				Serial.println(WiFi.localIP());

				Serial.println("[TIME] Setting time using NTP.");
				configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
				controllerState = GET_TIME;
			}

			break;

		case GET_TIME:
			// time is needed to check cert

			lcd.setCursor(0,0);
			lcd.print("GETTING TIME");
			for (i = 0; i < (millis() / 1000) % 4; i++) {
				lcd.print(".");
			}
			lcd.print("   ");

			now = time(nullptr);
			if (now > 8 * 3600 * 2) {
				gmtime_r(&now, &timeinfo);
				Serial.print("[TIME] Current time in UTC: ");
				Serial.print(asctime(&timeinfo));

				Serial.println("[WIFI] Test connection to portal...");
				controllerState = HEARTBEAT;
			}

			break;

		case HEARTBEAT:
			// test connection to the portal

			lcd.setCursor(0,0);
			lcd.print("CHECK PORTAL...");

			result = https.begin(wc, "https://api.my.protospace.ca/stats/");

			if (!result) {
				Serial.println("[WIFI] https.begin failed.");
				controllerState = BEGIN;
			}

			result = https.GET();

			Serial.printf("[WIFI] Http code: %d\n", result);

			if (result != HTTP_CODE_OK) {
				Serial.printf("[WIFI] Portal GET failed, error:\n%s\n", https.errorToString(result).c_str());
				controllerState = BEGIN;
			}

			Serial.println("[WIFI] Heartbeat success.");
			lcd.setCursor(0,1);
			lcd.print("GOOD");

			controllerState = FREEZE;

			break;

		case FREEZE:
			break;
	}

	return;
}

void setup()
{
	Serial.begin(115200);

	Serial2.begin(9600);
	Serial2.setTimeout(50);

	Serial.println("Host boot up");

	lcd.init();
	lcd.backlight();

	lcd.setCursor(0,0);
	lcd.print("BOOT UP");

	WiFi.mode(WIFI_STA);
	WiFi.begin("Protospace", "yycmakers");

	//X509List cert(lets_encrypt_ca);
	//wc.setTrustAnchors(&cert);
	wc.setInsecure();  // disables all SSL checks. don't use in production

	server.on("/", []() {
		server.send(200, "text/plain", "Hi! I am ESP32.");
	});

	ElegantOTA.begin(&server);
	WebSerial.begin(&server);
	WebSerial.msgCallback(recvMsg);
	server.begin();

	delay(500);
}

void loop()
{
	if (Serial2.available() > 0)
	{
		String data = Serial2.readString();

		Serial.print("RFID scan: ");
		Serial.print(data);
		Serial.print(", len: ");
		Serial.println(data.length());

		//if (controllerState == WAIT_FOR_SCAN && data[0] == 0x02 && data[13] == 0x03) {
		//	scannedCard = data.substring(1, 11);

		//	Serial.print("Card: ");
		//	Serial.println(scannedCard);

		//	//controllerState = GET_BALANCE;
		//}
	}

    if (Serial.available() > 0)
    {
		String data = Serial.readString();
		data.trim();
		Serial.print("Serial: ");
		Serial.println(data);

		if (data == "ok") {
			Serial.println("Typed ok");
		}
    }

	processControllerState();
	server.handleClient();
}

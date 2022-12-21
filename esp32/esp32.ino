#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>

#include "secrets.h"

void (* rebootArduino) (void) = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

enum controllerStates {
	BEGIN,
	DELAY_CONNECT,
	CONNECT,
	HEARTBEAT,
	WAIT_FOR_SCAN,
};

enum controllerStates controllerState = BEGIN;

String scannedCard = "";

void processControllerState() {
	static unsigned long timer = millis();
	static int statusCode;

	static StaticJsonDocument<1024> jsonDoc;

	switch (controllerState) {
		case BEGIN:
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
	lcd.print("Boot up");

	WiFi.mode(WIFI_STA);
	WiFi.begin("Protospace", "yycmakers");

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

		if (controllerState == WAIT_FOR_SCAN && data[0] == 0x02 && data[13] == 0x03) {
			scannedCard = data.substring(1, 11);

			Serial.print("Card: ");
			Serial.println(scannedCard);

			//controllerState = GET_BALANCE;
		}
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
}

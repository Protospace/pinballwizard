#include <Arduino.h>
#include <HardwareSerial.h>

void setup()
{
	Serial.begin(115200);

	Serial2.begin(9600);
	Serial2.setTimeout(100);

	Serial.println("Host boot up");
}


void loop()
{
	if (Serial2.available() > 0)
	{
		String data = Serial2.readString();

		Serial.print("RFID: ");
		Serial.println(data);
	}
}

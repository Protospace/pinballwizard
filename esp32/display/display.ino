// Example LCD sketch
// Connect to SCL and SDA

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

byte protocoin[] = {
	B01110,
	B10101,
	B11111,
	B10100,
	B11111,
	B00101,
	B11111,
	B00100
};

void setup()
{
	lcd.init();
	lcd.createChar(0, protocoin);
	lcd.backlight();

	lcd.setCursor(0,0);
	lcd.write(0);
	lcd.setCursor(1,0);
	lcd.print("12.34");
}

int count = 0;

void loop()
{
	lcd.setCursor(0,1);
	lcd.print(count);
	count++;
	delay(100);
}

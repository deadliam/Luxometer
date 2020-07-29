/*
  ArduMeter (Arduino incident light meter for cameras)
  by Alan Wang
  Updated by Anatolii Kasianov

  BH1750/BH1750FVI digital light intensity sensor:
  VCC -> 3.3V
  GND -> GND
  SDA -> pin A4
  SCL -> pin A5
  ADD -> (none)

  0.96" SSD1306 OLED 128x64 display:
  VCC -> 3.3V
  GND -> GND
  SDA -> pin A4
  SCL -> pin A5

  1x push button
  leg 1 -> 3.3V
  leg 2 -> pin D5 with 10 kOhms pull-down resistor connect to GND

  2x 10 kOhms potentiometer (knob)
  leg 1 -> 3.3V
  leg 2 (middle) -> pin A2/A3 (max analog signal about 730-740)
  leg 3 -> GND

  The Arduino Nano is powered by a USB powerbank via USB cable.

*/
#include <EEPROM.h>
#include <avr/sleep.h>
#include <math.h>
#include <Wire.h>
#include <BH1750.h> //for BH1750, https://github.com/claws/BH1750
#include "SSD1306Ascii.h" //for OLED, https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiAvrI2c.h" //same as above

BH1750 bh1750LightMeter;
SSD1306AsciiAvrI2c oled1306;
bool started = false; //if user started to use light meter (pressed button)
int BUTTON_PIN = 5;
int KNOB_PIN = 1;
bool buttonMeasurePressed; //status of measure light button
int buttonState = 0;  // 0 = not pressed   --- 1 = long pressed --- 2 short pressed
long prev = 0;
int DURATION_IN_MILLIS = 1000;

// EEPROM for memory recording
#define ISOIndexAddr        1
#define apertureIndexAddr   2
#define modeIndexAddr       3
#define T_expIndexAddr      4
#define meteringModeAddr    5
#define ndIndexAddr         6

#define defaultApertureIndex 12
#define defaultISOIndex      11
#define defaultModeIndex     0
#define defaultT_expIndex    19

uint8_t ISOIndex =          EEPROM.read(ISOIndexAddr);
uint8_t apertureIndex =     EEPROM.read(apertureIndexAddr);
uint8_t T_expIndex =        EEPROM.read(T_expIndexAddr);
uint8_t modeIndex =         EEPROM.read(modeIndexAddr);
uint8_t meteringMode =      EEPROM.read(meteringModeAddr);
uint8_t ndIndex =           EEPROM.read(ndIndexAddr);

int knobApertureStatus; //status of aperture select knob
int prevKnobApertureStatus; //previous status of aperture select knob
int knobISOstatus; //status of ISO select knob
int prevKnobISOstatus; //previous status of ISO select knob
long lux; //illuminance
float EV; //exposure value
int ISO; //film or digital sensor sensitivity
const int incidentCalibration = 340; //incident light calibration const, see https://en.wikipedia.org/wiki/Light_meter#Calibration_constants

void setup() {
  //initialize
  pinMode(BUTTON_PIN, INPUT);
  Serial.begin(9600);
  Wire.begin();
  bh1750LightMeter.begin(0x23); //BH1750_ONE_TIME_HIGH_RES_MODE_2
  //"initialize" BH1750 by using it, or it will return NaN at the first time
  lux = bh1750LightMeter.readLightLevel();
  //initialize OLED
  oled1306.begin(&Adafruit128x32, 0x3C);
  oled1306.setFont(Stang5x7);
  oled1306.clear();
  oled1306.setCursor(15, 0);
  oled1306.println("== LIGHTMETER ==");
  oled1306.setCursor(45, 1);
  oled1306.println("v 0.1");
  oled1306.setCursor(25, 3);
  oled1306.println("press button");
//  oled1306.set1X();

    // IF NO MEMORY WAS RECORDED BEFORE, START WITH THIS VALUES otherwise it will read "255"
//  if (apertureIndex > MaxApertureIndex) {
//    apertureIndex = defaultApertureIndex;
//  }
//  if (ISOIndex > MaxISOIndex) {
//    ISOIndex = defaultISOIndex;
//  }
//  if (T_expIndex > MaxTimeIndex) {
//    T_expIndex = defaultT_expIndex;
//  }
//  if (modeIndex < 0 || modeIndex > 1) {
//    // Aperture priority. Calculating shutter speed.
//    modeIndex = 0;
//  }
//  if (meteringMode > 1) {
//    meteringMode = 0;
//  }
//  if (ndIndex > MaxNDIndex) {
//    ndIndex = 0;
//  }
//  lux = getLux();
//  refresh();
  
  Serial.println("ArduMeter (Arduino incident light meter for camera) READY:");
  Serial.println("button -> measure light");
  Serial.println("knobs -> change aperature/ISO");
  
}

void loop() {

//  waitForButtonState();
  
  //read status from button and potentiometers
  buttonMeasurePressed = digitalRead(BUTTON_PIN);
//  Serial.println(analogRead(KNOB_PIN));
  knobApertureStatus = map(analogRead(KNOB_PIN), 0, 690, 1, 16);
//  knobISOstatus = map(analogRead(3), 60, 600, 1, 10);
  knobISOstatus = 6;
  
  if (buttonMeasurePressed) {
    //measure light
    if (!started) started = true;
    displayExposureSetting(true);
  } else {
    //change aperture/ISO settings
    if ((knobApertureStatus != prevKnobApertureStatus || knobISOstatus != prevKnobISOstatus) && started) {
      displayExposureSetting(false);
    }
  }
  //record potentiometer previous status
  prevKnobApertureStatus = knobApertureStatus;
  prevKnobISOstatus = knobISOstatus;
  delay(150);
}

void displayExposureSetting(bool measureNewExposure) {
  float aperature;
  float shutterSpeed;
  //select aperature via potentiometer 1; the values are based on most common aperatures you'll find on digital and analog cameras
  switch (knobApertureStatus) {
    case 1: aperature = 1.4; break;
    case 2: aperature = 1.7; break;
    case 3: aperature = 2; break;
    case 4: aperature = 2.8; break;
    case 5: aperature = 3.5; break;
    case 6: aperature = 4; break;
    case 7: aperature = 4.5; break;
    case 8: aperature = 5.6; break;
    case 9: aperature = 6.3; break;
    case 10: aperature = 8; break;
    case 11: aperature = 10; break;
    case 12: aperature = 11; break;
    case 13: aperature = 12.7; break;
    case 14: aperature = 16; break;
    case 15: aperature = 22; break;
    case 16: aperature = 32;
  }
  //select ISO via potentiometer 2; the values are based on common film speeds
  switch (knobISOstatus) {
    case 1: ISO = 12; break;
    case 2: ISO = 25; break;
    case 3: ISO = 50; break;
    case 4: ISO = 100; break;
    case 5: ISO = 160; break;
    case 6: ISO = 200; break;
    case 7: ISO = 400; break;
    case 8: ISO = 800; break;
    case 9: ISO = 1600; break;
    case 10: ISO = 3200;
  }
  if (measureNewExposure) {
    //measure light level (illuminance) and get a new lux value
    lux = bh1750LightMeter.readLightLevel();
    Serial.print("Measured illuminance = ");
    Serial.print(lux);
    Serial.println(" lux");
  }
  //calculate EV
  EV = (log10(lux * ISO / incidentCalibration) / log10(2));
  Serial.println(EV);
  if (isfinite(EV)) {
    //calculate shutter speed if EV is not NaN or infinity
    shutterSpeed = pow(2, EV) / pow(aperature, 2);

    // -----------------------------------
    //output results to serial port and OLED
    Serial.print("Exposure settings: ISO = ");
    Serial.print(ISO);
    Serial.print(", EV = ");
    Serial.print(EV);
    Serial.print(", aperture = f/");
    Serial.print(aperature, 1);
    Serial.print(", ");
    Serial.print("shutter speed = ");
    if (shutterSpeed >= 1) {
      Serial.print("1/");
      Serial.print(shutterSpeed);
    } else {
      Serial.print((1 / shutterSpeed));
    }
    Serial.println("s");
    oled1306.clear();

    // -----------------------------------
    drawScreen();
    
    drawISO(ISO);
    drawEV(EV);
    drawAperture(aperature);
    drawShutter(shutterSpeed);
    drawLx(lux);

    // -----------------------------------
    
  } else {
    Serial.println("Error: exposure out of bounds");
    
    oled1306.clear();
    drawScreen();
    drawISO(ISO);
    drawEV(-1);
    drawAperture(aperature);
    drawShutter(-1);
    drawLx(lux);
  }
}


void waitForButtonState() {
  buttonState = 0;
  if(digitalRead(BUTTON_PIN)) {
    prev = millis();
    buttonState = 1;
    while((millis() - prev) <= DURATION_IN_MILLIS) {
      if(!(digitalRead(BUTTON_PIN))) {
        buttonState = 2;
        break;
      }
    }
  }
}


void drawAperture(float aperture) {
  oled1306.setCursor(12, 1);
  oled1306.println("F " + String(aperture, 1));
}

void drawShutter(float shutter) {
  if (shutter < 0) {
    oled1306.setCursor(12, 3);
    oled1306.println("####");
  } else {
    oled1306.setCursor(12, 3);
    if (shutter >= 1) {
      oled1306.print("1/");
      oled1306.print(shutter, 0);
    } else {
      oled1306.print((1 / shutter), 0);
    }
    oled1306.println("s");
  }
}

void drawISO(int iso) {
  oled1306.setCursor(70, 0);
  oled1306.print("ISO ");
  oled1306.println(iso);
}

void drawEV(float ev) {
  if (ev < 0) {
    oled1306.setCursor(70, 1);
    oled1306.print("EV  ");
    oled1306.println("####");
  } else {
    oled1306.setCursor(70, 1);
    oled1306.print("EV  ");
    oled1306.println(ev, 2);
  }
}

void drawLx(int lx) {
  oled1306.setCursor(70, 3);
  oled1306.print(lx);
  oled1306.println(" Lx");
}

void drawArrow(int pos) {
  if (pos == 1) {
    oled1306.setCursor(0, 1);
  } else {
    oled1306.setCursor(0, 3);
  }
  oled1306.println(">");
}

void drawScreen() {
  oled1306.setCursor(55, 0);
  oled1306.println("|");
  oled1306.setCursor(55, 1);
  oled1306.println("|");
  oled1306.setCursor(55, 2);
  oled1306.println("|");
  oled1306.setCursor(55, 3);
  oled1306.println("|");
  oled1306.setCursor(65, 2);
  oled1306.println("----------");

}

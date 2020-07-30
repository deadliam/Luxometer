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
#include "OneButton.h"
#include <BH1750.h> //for BH1750, https://github.com/claws/BH1750
#include "SSD1306Ascii.h" //for OLED, https://github.com/greiman/SSD1306Ascii
#include "SSD1306AsciiAvrI2c.h" //same as above

BH1750 bh1750LightMeter;
SSD1306AsciiAvrI2c oled1306;
bool started = false; //if user started to use light meter (pressed button)
int BUTTON_PIN = 5;
int KNOB_PIN = 1;
boolean knobSwitchState = false;

boolean ButtonStateShort = false;
boolean ButtonStateLong = false;

//// SWITCH ////
static const int buttonPin = 5;                    // switch pin
int buttonStatePrevious = LOW;                      // previousstate of the switch

unsigned long minButtonLongPressDuration = 2000;    // Time we wait before we see the press as a long press
unsigned long buttonLongPressMillis;                // Time in ms when we the button was pressed
bool buttonStateLongPress = false;                  // True if it is a long press
const int intervalButton = 50;                      // Time between two readings of the button state
unsigned long previousButtonMillis;                 // Timestamp of the latest reading
unsigned long buttonPressDuration;                  // Time the button is pressed in ms
unsigned long currentMillis;          // Variabele to store the number of milleseconds since the Arduino has started
//------------------------------

// EEPROM for memory recording
#define ISOIndexAddr        0
#define apertureIndexAddr   1

#define defaultISOIndex      4
#define defaultApertureIndex 1

#define MaxISOIndex             10
#define MaxApertureIndex        16

int ISOIndex; // =          EEPROM.read(ISOIndexAddr);
int prevISOIndex;
int apertureIndex; // =     EEPROM.read(apertureIndexAddr);
int prevApertureIndex;

int knobStatus;
int prevKnobStatus;

String menuArrow = "aperture";

float aperture;
float shutterSpeed = -1;
long lux = -1; //illuminance
float EV = -1; //exposure value
int ISO; //film or digital sensor sensitivity
//const int incidentCalibration = 340; //incident light calibration const, see https://en.wikipedia.org/wiki/Light_meter#Calibration_constants
const int incidentCalibration = 16;

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
  oled1306.setCursor(15, 1);
  oled1306.println("== LIGHTMETER ==");
  oled1306.setCursor(45, 3);
  oled1306.println("v 0.2");
//  oled1306.setCursor(25, 3);
//  oled1306.println("press button");
//  oled1306.set1X();
  
  ISOIndex = EEPROM.read(ISOIndexAddr);
  apertureIndex = EEPROM.read(apertureIndexAddr);

    // IF NO MEMORY WAS RECORDED BEFORE, START WITH THIS VALUES otherwise it will read "255"
  if (apertureIndex > MaxApertureIndex) {
    apertureIndex = defaultApertureIndex;
  }
  if (ISOIndex > MaxISOIndex) {
    ISOIndex = defaultISOIndex;
  }
  
  Serial.println("Button short press -> measure light");
  Serial.println("Button long press -> switch item");
  Serial.println("Knob -> change aperture/ISO");
  delay(2000);
  matchApertureAndIsoToValue();
  displayExposureSetting(false);  
}

void loop() {
  currentMillis = millis();
 
  waitForButtonState();
  waitForKnobHasChanged();
  delay(150);
}

void displayExposureSetting(bool measureNewExposure) {
  
  if (measureNewExposure) {
    //measure light level (illuminance) and get a new lux value
    lux = bh1750LightMeter.readLightLevel();
    Serial.print("Measured illuminance = ");
    Serial.print(lux);
    Serial.println(" lux");
    ButtonStateShort = !ButtonStateShort;
  }
  //calculate EV
  EV = (log10(lux * ISO / incidentCalibration) / log10(2));
//  Serial.println(EV);
  if (isfinite(EV)) {
    //calculate shutter speed if EV is not NaN or infinity
    shutterSpeed = pow(2, EV) / pow(aperture, 2);

    //output results to serial port and OLED
    Serial.print("Exposure settings: ISO = ");
    Serial.print(ISO);
    Serial.print(", EV = ");
    Serial.print(EV);
    Serial.print(", aperture = f/");
    Serial.print(aperture, 1);
    Serial.print(", ");
    Serial.print("shutter speed = ");
    if (shutterSpeed >= 1) {
      Serial.print("1/");
      Serial.print(shutterSpeed);
    } else {
      Serial.print((1 / shutterSpeed));
    }
    Serial.println("s");

  } else {
    Serial.println("Error: exposure out of bounds");
    EV = -1;
    shutterSpeed = -1;
    
  }
  drawUI();
  saveSettings();
}

void waitForKnobHasChanged() {

  knobStatus = analogRead(KNOB_PIN);
  
  if ((menuArrow == "aperture") && (apertureIndex == prevApertureIndex)) {
    apertureIndex = map(knobStatus, 0, 690, 1, 16);
  }
  if ((menuArrow == "iso") && (ISOIndex == prevISOIndex)) {
    ISOIndex = map(knobStatus, 0, 690, 1, 10);
  }
  
  if (ButtonStateLong && knobSwitchState) {
    ButtonStateLong = !ButtonStateLong;
    knobSwitchState = !knobSwitchState;
    menuArrow = "aperture";
    drawUI();
    
  } else if (ButtonStateLong && !knobSwitchState) {
    ButtonStateLong = !ButtonStateLong;
    knobSwitchState = !knobSwitchState;
    menuArrow = "iso";
    drawUI();
  }
  matchApertureAndIsoToValue();
  
  //record potentiometer previous status
  prevKnobStatus = knobStatus;
  prevISOIndex = ISOIndex;
  prevApertureIndex = apertureIndex;
}

void matchApertureAndIsoToValue() {
  if ((apertureIndex != prevApertureIndex) || (ISOIndex != prevISOIndex)) {
    if (!knobSwitchState || !started) {
      //select aperature via potentiometer 1; the values are based on most common aperatures you'll find on digital and analog cameras
      switch (apertureIndex) {
        case 1: aperture = 1.4; break;
        case 2: aperture = 1.7; break;
        case 3: aperture = 2; break;
        case 4: aperture = 2.8; break;
        case 5: aperture = 3.5; break;
        case 6: aperture = 4; break;
        case 7: aperture = 4.5; break;
        case 8: aperture = 5.6; break;
        case 9: aperture = 6.3; break;
        case 10: aperture = 8; break;
        case 11: aperture = 10; break;
        case 12: aperture = 11; break;
        case 13: aperture = 12.7; break;
        case 14: aperture = 16; break;
        case 15: aperture = 22; break;
        case 16: aperture = 32;
      } 
    } 
    if (knobSwitchState || !started) {
     //select ISO via potentiometer 2; the values are based on common film speeds
      switch (ISOIndex) {
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
    }
    displayExposureSetting(false);
    started = true;
  }
}
  
void waitForButtonState() {
  
  // If the difference in time between the previous reading is larger than intervalButton
  if(currentMillis - previousButtonMillis > intervalButton) {
    
    // Read the digital value of the button (LOW/HIGH)
    int buttonState = digitalRead(buttonPin);    

    // If the button has been pushed AND
    // If the button wasn't pressed before AND
    // IF there was not already a measurement running to determine how long the button has been pressed
    if (buttonState == HIGH && buttonStatePrevious == LOW && !buttonStateLongPress) {
      buttonLongPressMillis = currentMillis;
      buttonStatePrevious = HIGH;
      Serial.println("Button pressed");
    }

    // Calculate how long the button has been pressed
    buttonPressDuration = currentMillis - buttonLongPressMillis;

    // If the button is pressed AND
    // If there is no measurement running to determine how long the button is pressed AND
    // If the time the button has been pressed is larger or equal to the time needed for a long press
    if (buttonState == HIGH && !buttonStateLongPress && buttonPressDuration >= minButtonLongPressDuration) {
      buttonStateLongPress = true;
      Serial.println("Button long pressed");
      ButtonStateLong = !ButtonStateLong;
    }
      
    // If the button is released AND
    // If the button was pressed before
    if (buttonState == LOW && buttonStatePrevious == HIGH) {
      buttonStatePrevious = LOW;
      buttonStateLongPress = false;
      Serial.println("Button released");

      // If there is no measurement running to determine how long the button was pressed AND
      // If the time the button has been pressed is smaller than the minimal time needed for a long press
      // Note: The video shows:
      //       if (!buttonStateLongPress && buttonPressDuration < minButtonLongPressDuration) {
      //       since buttonStateLongPress is set to FALSE on line 75, !buttonStateLongPress is always TRUE
      //       and can be removed.
      if (buttonPressDuration < minButtonLongPressDuration) {
        Serial.println("Button pressed shortly");
        ButtonStateShort = !ButtonStateShort;
        displayExposureSetting(true);
      }
    }
    
    // store the current timestamp in previousButtonMillis
    previousButtonMillis = currentMillis;
  }
}

void drawUI() {

  Serial.print("ApIdx = ");
  Serial.print(apertureIndex);
  Serial.print(" | PrevApIdx = ");
  Serial.print(prevApertureIndex);
  Serial.print(" |||| ISOIndex = ");
  Serial.print(ISOIndex);
  Serial.print(" | PrevISOIndex = ");
  Serial.println(prevISOIndex);
  
  oled1306.clear();
  drawCursor();
  drawScreen();
  drawISO();
  drawEV();
  drawAperture();
  drawShutter();
  drawLx();
}

void drawAperture() {
  oled1306.setCursor(12, 1);
  oled1306.println("F " + String(aperture, 1));
}

void drawShutter() {
  if (shutterSpeed < 0) {
    oled1306.setCursor(12, 3);
    oled1306.println("####");
  } else {
    oled1306.setCursor(12, 3);
    if (shutterSpeed >= 1) {
      oled1306.print("1/");
      oled1306.print(shutterSpeed, 0);
    } else {
      oled1306.print((1 / shutterSpeed), 0);
    }
    oled1306.println("s");
  }
}

void drawISO() {
  if (ISO < 0) {
    oled1306.setCursor(70, 0);
    oled1306.print("ISO ");
    oled1306.println("####");
  } else {
    oled1306.setCursor(70, 0);
    oled1306.print("ISO ");
    oled1306.println(ISO);
  }
}

void drawEV() {
  if (EV < 0) {
    oled1306.setCursor(70, 1);
    oled1306.print("EV  ");
    oled1306.println("####");
  } else {
    oled1306.setCursor(70, 1);
    oled1306.print("EV  ");
    oled1306.println(EV, 2);
  }
}

void drawLx() {
  oled1306.setCursor(70, 3);
  oled1306.print(lux);
  oled1306.println(" Lx");
}

void drawCursor() {
  if (menuArrow == "aperture") {
    oled1306.setCursor(0, 1);
  } else if (menuArrow == "iso") {
    oled1306.setCursor(60, 0);
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

void saveSettings() {
  // Save lightmeter setting into EEPROM.
  EEPROM.update(ISOIndexAddr, ISOIndex);
  EEPROM.update(apertureIndexAddr, apertureIndex);
}

/****************
 * CODE OPTIONS *
 ****************/
#define DEBBYUG 1
#define SERIAL_BAUD 9600
#define SCREENLIGHT 9
#define EXTLIGHT 10
#define IR 2
#define LCD_RS 3
#define LCD_E 4
#define LCD_D4 5
#define LCD_D5 6
#define LCD_D6 7
#define LCD_D7 8

/****************
 * DEBUG MACROS *
 ****************/
#if DEBBYUG == 1
#define NDEBUGPRINT(x) Serial.print(millis()); \
    Serial.print(": "); \
    Serial.print(__PRETTY_FUNCTION__); \
    Serial.print(' '); \
    Serial.print(__FILE__); \
    Serial.print(':'); \
    Serial.print(__LINE__); \
    Serial.print(' '); \
    Serial.println(x)
#define DEBUGPRINT(x) Serial.println(x)
#else
#define NDEBUGPRINT(x)
#define DEBUGPRINT(x)
#endif

/*****************
 *   LIBRARIES   *
 *****************/
#include "Arduino.h"
#include <IRremote.h>
#include <DS3232RTC.h>
#include <TimeLib.h>
#include <Wire.h>
#include <LiquidCrystal.h> // includes the LiquidCrystal Library

/********************
 * GLOBAL VARIABLES *
 ********************/
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
IRrecv ir(IR);
decode_results irResult;
char prBuffer[20];
char sPrBuffer[5];
bool mainProspective = 1;
bool alarmStatus = 0;
int alarmHour = 7;
int alarmMinute = 0;
int alarmAdvance = 30;
unsigned long lastAlarmRung = 0;

/*******************
 *   FUNCTIONS     *
 *******************/
// Reset the screen
void lcdReset() {
    lcd.setCursor(0, 0);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
}

// Reset the screen and prepare to write
void lcdFullReset() {
    lcdReset();
    lcd.setCursor(0, 0);
}

// Prepare to write to a line
void cursorReset(int line) {
    lcd.setCursor(0, line);
}

// Set RTC time
void setInsertTime(int ho, int mi, int se, int da, int mo, int ye) {
    // ho mi se da mo ye
    setTime(ho, mi, se, da, mo, ye);
    RTC.set(now());
}

// Change screen status light
void changeScreenStatus() {
    if (digitalRead(SCREENLIGHT) == LOW) {
        lcd.display();
        digitalWrite(SCREENLIGHT, HIGH);
    }
    else {
        lcd.noDisplay();
        digitalWrite(SCREENLIGHT, LOW);
    }
}

// Must always called twice
int changeScreenStatusWithMemory() {
    static int call = 1;
    static int previusLightStatus;
    if (call) {
        previusLightStatus = digitalRead(SCREENLIGHT);

        if (previusLightStatus == LOW) {
            lcd.display();
            digitalWrite(SCREENLIGHT, HIGH);
        }
        call = 0;
    }
    else {
        if (previusLightStatus == LOW) {
            lcd.noDisplay();
            digitalWrite(SCREENLIGHT, LOW);
        }
        call = 1;
    }
    return call;
}

// Set outlight
void setLight(int percent) {
    if (percent <= 0) {
        NDEBUGPRINT("Set light LOW");
        digitalWrite(EXTLIGHT, LOW);
    }
    else if (percent >= 100) {
        NDEBUGPRINT("Set light HIGH");
        digitalWrite(EXTLIGHT, HIGH);
    }
    else {
        analogWrite(EXTLIGHT, map(percent, 0, 100, 0, 255));
        NDEBUGPRINT("Set light LOW as");
        DEBUGPRINT(map(percent, 0, 100, 0, 255));
    }
}

/*******************
 *     SETUP       *
 *******************/
void setup() {
#if DEBBYUG == 1
    Serial.begin(SERIAL_BAUD);
#endif

    pinMode(SCREENLIGHT, OUTPUT);
    digitalWrite(SCREENLIGHT, LOW);

    pinMode(EXTLIGHT, OUTPUT);
    digitalWrite(EXTLIGHT, LOW);

    // Load wire
    Wire.begin();

    // Load Screen
    lcd.begin(16, 2);
    //cursorReset(0);
    digitalWrite(SCREENLIGHT, HIGH);
    //lcd.print("   Loading...");
    //delay(500);
    //cursorReset(1);
    //lcd.print("   Ver. 0.0.1");
    //delay(500);

    // Load clock
    setSyncProvider(RTC.get);

    // Load IR
    ir.enableIRIn();

}

/*******************
 *      LOOP       *
 *******************/
void loop() {
    // Show the standard view
    updateStandardViewTrigger();

    // Alarm check
    checkAlarmTrigger();

    if (ir.decode(&irResult)) {
        // View change
        //up 0xFF906F down 0xFFE01F
        if (irResult.value == 0xFF906F || irResult.value == 0xFFE01F) {
            mainProspective = mainProspective ? 0 : 1;
        }

        // Alarm On Off
        // pause/play
        else if (irResult.value == 0xFF02FD) {
            alarmStatus = alarmStatus ? 0 : 1;
            if (alarmStatus) {
                for (int i = 0; i < 10; i++) {
                    changeScreenStatus();
                    delay(100);
                    updateStandardViewTrigger();
                }
            }
            else {
                for (int i = 0; i < 4; i++) {
                    changeScreenStatus();
                    delay(500);
                    updateStandardViewTrigger();
                }
            }
        }

        // Light triggers
        else if (irResult.value == 0xFFA25D) {
            // If screen light is off i turn on
            changeScreenStatusWithMemory();

            lightOnTrigger();

            // If needed turn off light
            changeScreenStatusWithMemory();
        }

        // SCREENLIGHT
        else if (irResult.value == 0xFF629D) {
            changeScreenStatus();
        }

        // FUNC/STOP
        else if (irResult.value == 0xFFE21D) {
            // If screen light is off i turn on
            changeScreenStatusWithMemory();

            menuTrigger();

            // If needed turn off light
            changeScreenStatusWithMemory();
        }

#if DEBBYUG == 1
        //DEBUGPRINT(irResult.value);
#endif

        ir.resume();
    }
}

/*******************
 *    TRIGGERS     *
 *******************/

void updateStandardViewTrigger() {
    // Default view
    if (mainProspective) {
        cursorReset(0);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour(), minute(), second());
        lcd.print(prBuffer);
        cursorReset(1);
        sprintf(prBuffer, "   %02d/%02d/%04d   ", day(), month(), year());
        lcd.print(prBuffer);
    }
    // Alarm view
    else {
        cursorReset(0);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour(), minute(), second());
        lcd.print(prBuffer);
        cursorReset(1);

        sprintf(prBuffer, " ALR %s %02d:%02d  ", alarmStatus ? "ON " : "OFF", alarmHour, alarmMinute);
        lcd.print(prBuffer);
    }
}

void checkAlarmTrigger() {
    if (alarmStatus) {
        int lAlarmMinute = alarmMinute;
        int lAlarmHour = alarmHour;

        if (lAlarmMinute - alarmAdvance >= 0) {
            lAlarmMinute -= alarmAdvance;
        }
        else {
            lAlarmHour--;
            if (lAlarmHour < 0) {
                lAlarmHour = 23;
            }
            lAlarmMinute += (60 - alarmAdvance);
        }
        if (hour() == lAlarmHour && minute() == lAlarmMinute && (now() - lastAlarmRung) > 60 * alarmAdvance) {
            DEBUGPRINT(now());
            DEBUGPRINT(lastAlarmRung);
            lastAlarmRung = now();
            //alarmStatus = 0;
            alarmRingTrigger();
        }
    }
}

void alarmRingTrigger() {
    int light = 0;
    lcdFullReset();
    lcd.print("     ALARM!     ");
    setLight(light);
    int startMinute = minute();
    int countMinutes = 0;
    while (true) {
        // If next minute
        if (startMinute != minute()) {
            startMinute = minute();
            countMinutes++;
            NDEBUGPRINT("countMinutes:");
            DEBUGPRINT(countMinutes);
        }
        // light change if needed
        //int nLight = (float) countMinutes/alarmAdvance*100;
        int nLight = map(countMinutes, 0, alarmAdvance, 0, 100);
        if (light != nLight && nLight <= alarmAdvance) {
            light = nLight;
            setLight(light);
            NDEBUGPRINT("Changing light to:");
            DEBUGPRINT(light);

        }
        ir.resume();
        delay(200);
        // hour print
        cursorReset(1);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour(), minute(), second());
        lcd.print(prBuffer);

        // exit
        if (ir.decode(&irResult)) {
            if (irResult.value == 0xFFA25D) {
                setLight(0);
                ir.resume();
                delay(200);
                return;
            }
        }
    }
}

void lightOnTrigger() {
    static int lightPercent = 100;
    lcdFullReset();
    lcd.print("   Light ");
    if (lightPercent < 100)
        lcd.print("0");
    lcd.print(lightPercent);
    lcd.print("%   ");
    setLight(lightPercent);

    int cycle = 1;
    while (cycle) {
        ir.resume();
        delay(200);
        if (ir.decode(&irResult)) {
            // if poweroff button
            if (irResult.value == 0xFFA25D) {
                cycle = 0;
            }
            else if (irResult.value == 0xFF906F && lightPercent <= 90) {
                lightPercent += 10;
                cursorReset(0);
                lcd.print("   Light ");
                if (lightPercent < 100)
                    lcd.print("0");
                lcd.print(lightPercent);
                lcd.print("%   ");
                setLight(lightPercent);
            }
            else if (irResult.value == 0xFFE01F && lightPercent >= 10) {
                lightPercent -= 10;
                cursorReset(0);
                lcd.print("   Light ");
                if (lightPercent < 100)
                    lcd.print("0");
                lcd.print(lightPercent);
                lcd.print("%   ");
                setLight(lightPercent);
            }
        }
    }

    // Turn off light
    setLight(0);
}

void menuTrigger() {
    //MAGIC
    int menuLen = 2; // items excluding exit :D

    int menuStatus = 0;

    while (menuStatus >= 0) {
        cursorReset(0);
        lcd.print("MENU:           ");

        ir.resume();
        delay(200);
        cursorReset(1);

        bool decode = ir.decode(&irResult);

        // menu navigation
        // down
        if (decode && menuStatus > 0 && irResult.value == 0xFFE01F) {
            DEBUGPRINT("--");
            menuStatus--;
        }
        // up
        else if (decode && menuStatus < menuLen && irResult.value == 0xFF906F) {
            menuStatus++;
            DEBUGPRINT("++");
        }

        // if up overflow reset
        else if (decode && menuStatus == menuLen && irResult.value == 0xFF906F) {
            menuStatus = 0;
        }

        // if down overflow reset
        else if (decode && menuStatus == 0 && irResult.value == 0xFFE01F) {
            menuStatus = menuLen;
        }

        // MENU
        // exit 0
        if (menuStatus == 0) {
            lcd.print("exit            ");
            // if pressed func (enter)
            if (decode && irResult.value == 0xFFE21D) {
                menuStatus--;
            }
        }

        // Set alarm
        else if (menuStatus == 1) {
            lcd.print("Set alarm       ");

            // ip pressed func (enter)
            if (decode && irResult.value == 0xFFE21D) {
                setAlarmTrigger();
            }
        }

        // Set date and hour
        else if (menuStatus == 2) {
            lcd.print("Set date & hour ");

            // ip pressed func (enter)
            if (decode && irResult.value == 0xFFE21D) {
                setDateAndHourTrigger();
            }
        }
    }
}

void setAlarmTrigger() {
    cursorReset(0);
    lcd.print("   Alarm  Set   ");
    cursorReset(1);
    lcd.print("     --:--      ");
    int inputData[4];
    lcd.setCursor(5, 1);
    for (int i = 0; i < 4; i++) {
        inputData[i] = getInputNumber();

        if (i == 2) {
            lcd.print(":");
        }
        lcd.print(inputData[i]);
    }

    // Here compute
    int hour = 10 * inputData[0] + inputData[1];
    int minute = 10 * inputData[2] + inputData[3];

    //check if accettable
    if (hour > 24 or minute > 60) {
        cursorReset(0);
        lcd.print("ERROR!! Input is");
        cursorReset(1);
        lcd.print("not accettable! ");
        delay(2000);
        return;
    }

    alarmHour = hour;
    alarmMinute = minute;
    lastAlarmRung = 0;

    // Print
    cursorReset(0);
    lcd.print("     Done...    ");
    delay(500);
    cursorReset(1);
    lcd.print("      :-)       ");
    delay(1000);

}

void setDateAndHourTrigger() {
    // Beauty
    cursorReset(0);
    lcd.print("Time: --:--:--  ");
    cursorReset(1);
    lcd.print("Date: --/--/----");

    int inputData[14];

    cursorReset(0);

    // Select time
    lcd.print("Time: ");
    for (int i = 0; i < 6; i++) {
        inputData[i] = getInputNumber();

        if (i == 2 || i == 4) {
            lcd.print(":");
        }

        lcd.print(inputData[i]);
    }

    // Select date
    cursorReset(1);

    lcd.print("Date: ");
    for (int i = 6; i < 14; i++) {
        inputData[i] = getInputNumber();

        if (i == 8 || i == 10) {
            lcd.print("/");
        }

        lcd.print(inputData[i]);
    }

    // Here compute
    int hour = 10 * inputData[0] + inputData[1];
    int minute = 10 * inputData[2] + inputData[3];
    int second = 10 * inputData[4] + inputData[5];
    int day = 10 * inputData[6] + inputData[7];
    int month = 10 * inputData[8] + inputData[9];
    int year = 1000 * inputData[10] + 100 * inputData[11] + 10 * inputData[12] + inputData[13];

    //check if accettable
    if (hour > 24 or minute > 60 or second > 60 or day > 31 or month > 12) {
        cursorReset(0);
        lcd.print("ERROR!! Input is");
        cursorReset(1);
        lcd.print("not accettable! ");
        delay(2000);
        return;
    }

    // Confirm?
    delay(2000);
    cursorReset(0);
    lcd.print("Up to ok down to");
    cursorReset(1);
    lcd.print("    discard     ");

    while (true) {
        ir.resume();
        delay(200);
        bool decode = ir.decode(&irResult);
        if (decode) {
            // accept UP
            if (irResult.value == 0xFF906F) {
                setInsertTime(hour, minute, second, day, month, year);
                cursorReset(0);
                lcd.print("     Done...    ");
                delay(500);
                cursorReset(1);
                lcd.print("      :-)       ");
                delay(1000);
                return;
            }
            // refuse DOWN
            else if (irResult.value == 0xFFE01F) {
                return;
            }
        }
    }

}

int getInputNumber() {
    while (true) {
        ir.resume();
        delay(200);
        bool decode = ir.decode(&irResult);

        if (decode) {
            switch (irResult.value) {
                // 0
                case 0xFF6897:
                    return 0;
                    break;
                case 0xFF30CF:
                    return 1;
                    break;
                case 0xFF18E7:
                    return 2;
                    break;
                case 0xFF7A85:
                    return 3;
                    break;
                case 0xFF10EF:
                    return 4;
                    break;
                case 0xFF38C7:
                    return 5;
                    break;
                case 0xFF5AA5:
                    return 6;
                    break;
                case 0xFF42BD:
                    return 7;
                    break;
                case 0xFF4AB5:
                    return 8;
                    break;
                case 0xFF52AD:
                    return 9;
                    break;
            }
        }

    }
}

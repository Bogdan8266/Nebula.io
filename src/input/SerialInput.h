#pragma once
/**
 * SerialInput.h — Serial Monitor button simulation
 * 1 = PREV, 2 = PLAY/PAUSE, 3 = NEXT, 4 = MENU
 */
#include <Arduino.h>

enum class Button { NONE, PREV, PLAY, NEXT, MENU };

class SerialInput {
public:
    static Button read() {
        if (!Serial.available()) return Button::NONE;
        char c = Serial.read();
        // flush rest of line
        while (Serial.available()) Serial.read();
        switch (c) {
            case '1': Serial.println("[BTN] PREV");  return Button::PREV;
            case '2': Serial.println("[BTN] PLAY");  return Button::PLAY;
            case '3': Serial.println("[BTN] NEXT");  return Button::NEXT;
            case '4': Serial.println("[BTN] MENU");  return Button::MENU;
            default:  return Button::NONE;
        }
    }
};

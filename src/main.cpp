#include "M5StickCPlus2.h"

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Display.setRotation(1);

    // Clear screen
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Display title
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(20, 50);
    StickCP2.Display.print("Safe Bite");

    // Subtitle
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setTextColor(TFT_GREEN);
    StickCP2.Display.setCursor(35, 80);
    StickCP2.Display.print("Hello World!");
}

void loop() {
    StickCP2.update();
    delay(100);
}

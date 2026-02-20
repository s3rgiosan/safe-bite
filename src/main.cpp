#include "M5StickCPlus2.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "language.h"
#include "wifi_manager.h"
#include "audio_manager.h"

// Language state
uint8_t currentLang = LANG_EN;
Preferences prefs;

// String arrays [EN, PT]
const char* STR_LOADING[] = {"Loading...", "A carregar..."};
const char* STR_TITLE[] = {"Safe Bite - Categories", "Safe Bite - Categorias"};
const char* STR_NAV_NEXT_SEL[] = {"PWR:Next  M5:Select", "PWR:Prox  M5:Escolher"};
const char* STR_NAV_BACK[] = {"B:Back", "B:Voltar"};
const char* STR_YES[] = {"YES", "SIM"};
const char* STR_NO[] = {"NO", "NAO"};
const char* STR_FODMAP_LOW[] = {"LOW", "BAIXO"};
const char* STR_FODMAP_MOD[] = {"MOD", "MOD"};
const char* STR_FODMAP_HIGH[] = {"HIGH", "ALTO"};
const char* STR_SETTINGS[] = {"Settings", "Definicoes"};
const char* STR_LANGUAGE[] = {"Language", "Idioma"};
const char* STR_LANG_NAME[] = {"English", "Portugues"};
const char* STR_NAV_SETTINGS[] = {"M5:Change  B:Back", "M5:Mudar  B:Voltar"};

// WiFi status strings
const char* STR_WIFI_CONNECTING[] = {"Connecting...", "A ligar..."};
const char* STR_WIFI_CONNECTED[] = {"Connected", "Ligado"};
const char* STR_WIFI_OFFLINE[] = {"Offline", "Offline"};

// Recording strings
const char* STR_RECORDING[] = {"Recording...", "A gravar..."};
const char* STR_SPEAK_NOW[] = {"Speak now!", "Fale agora!"};
const char* STR_PROCESSING[] = {"Processing...", "A processar..."};
const char* STR_VOICE_SEARCH[] = {"Voice Search", "Pesquisa Voz"};

// Main menu strings
const char* STR_BROWSE_FOODS[] = {"Browse Foods", "Ver Alimentos"};

// Language functions
void loadLanguage() {
    prefs.begin("safebite", true);  // read-only
    currentLang = prefs.getUChar("lang", LANG_EN);
    prefs.end();
}

void saveLanguage() {
    prefs.begin("safebite", false);  // read-write
    prefs.putUChar("lang", currentLang);
    prefs.end();
}

void toggleLanguage() {
    currentLang = (currentLang + 1) % LANG_COUNT;
    saveLanguage();
}

// Menu states
enum MenuState {
    STATE_MAIN_MENU,
    STATE_CATEGORIES,
    STATE_FOODS,
    STATE_RESULT,
    STATE_SETTINGS,
    STATE_RECORDING
};

// Food structure
struct Food {
    String name_pt;
    String name_en;
    String category;
    String fodmap;
    bool gluten;
};

// Category structure
struct Category {
    String id;
    String name_pt;
    String name_en;
};

// Global state
MenuState currentState = STATE_MAIN_MENU;
int currentIndex = 0;
int itemCount = 0;
String selectedCategory = "";

// Data storage
Category categories[8];
int categoryCount = 0;
Food foods[200];
int foodCount = 0;
Food* filteredFoods[200];
int filteredCount = 0;

// Display colors
const uint16_t COLOR_LOW = TFT_GREEN;
const uint16_t COLOR_MODERATE = TFT_YELLOW;
const uint16_t COLOR_HIGH = TFT_RED;
const uint16_t COLOR_UNKNOWN = TFT_BLUE;

// Scroll state for long text
String scrollText = "";
int scrollPos = 0;
unsigned long lastScrollTime = 0;
const int SCROLL_DELAY = 300;      // ms between scroll steps
const int SCROLL_PAUSE = 1500;     // ms pause at start/end
const int MAX_DISPLAY_CHARS = 14;  // chars that fit at text size 2
bool scrollPaused = true;
int lastFoodIndex = -1;  // Track food index for scroll reset

// Inactivity timeout for auto-shutdown
unsigned long lastActivityTime = 0;
const unsigned long INACTIVITY_TIMEOUT = 5 * 60 * 1000;  // 5 minutes in ms

// Function declarations
void loadFoodsDatabase();
void drawMainMenu();
void drawCategories();
void drawFoods();
void drawResult();
void drawSettings();
void filterFoodsByCategory(const String& categoryId);
uint16_t getFodmapColor(const String& level);
String getFodmapLabel(const String& level);
void resetScroll(const String& text);
String getScrolledText(const String& text);
bool updateScroll();

// Language-aware name accessors
inline const String& getName(const Category& c) {
    return (currentLang == LANG_PT) ? c.name_pt : c.name_en;
}
inline const String& getName(const Food& f) {
    return (currentLang == LANG_PT) ? f.name_pt : f.name_en;
}

// Reset scroll state when changing items
void resetScroll(const String& text) {
    scrollText = text;
    scrollPos = 0;
    lastScrollTime = millis();
    scrollPaused = true;
}

// Get the visible portion of scrolling text
String getScrolledText(const String& text) {
    if (text.length() <= MAX_DISPLAY_CHARS) {
        return text;
    }

    // Add padding for smooth loop
    String padded = text + "   " + text;
    return padded.substring(scrollPos, scrollPos + MAX_DISPLAY_CHARS);
}

// Update scroll position (call from loop)
bool updateScroll() {
    if (scrollText.length() <= MAX_DISPLAY_CHARS) {
        return false;  // No scroll needed
    }

    unsigned long now = millis();
    unsigned long delay = scrollPaused ? SCROLL_PAUSE : SCROLL_DELAY;

    if (now - lastScrollTime >= delay) {
        lastScrollTime = now;
        scrollPaused = false;
        scrollPos++;

        // Reset when we've scrolled through original text + padding
        if (scrollPos >= (int)scrollText.length() + 3) {
            scrollPos = 0;
            scrollPaused = true;
        }
        return true;  // Needs redraw
    }
    return false;
}

// Power button state tracking
bool lastPwrState = true;  // Start as true to require release before first press
unsigned long lastPwrPress = 0;
const unsigned long PWR_DEBOUNCE = 200;  // 200ms debounce

void setup() {
    auto cfg = M5.config();
    StickCP2.begin(cfg);
    StickCP2.Display.setRotation(1);
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Splash screen
    StickCP2.Display.setTextColor(TFT_GREEN);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.setCursor(35, 50);
    StickCP2.Display.print("Safe Bite");

    // Tagline
    StickCP2.Display.setTextColor(TFT_LIGHTGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(55, 90);
    StickCP2.Display.print("Food Safety Checker");

    delay(1500);  // Show for 1.5 seconds
    StickCP2.Display.fillScreen(TFT_BLACK);  // Clear before loading screen

    // Initialize WiFi (non-blocking)
    wifiInit();

    // Initialize audio recording (allocate buffer)
    if (!audioInit()) {
        Serial.println("WARNING: Audio buffer allocation failed - voice search unavailable");
    }

    // Configure power button GPIO (GPIO35) as input
    pinMode(35, INPUT);

    // Load saved language preference
    loadLanguage();

    // Show loading screen
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(20, 50);
    StickCP2.Display.print(STR(STR_LOADING));

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        StickCP2.Display.fillScreen(TFT_RED);
        StickCP2.Display.setCursor(10, 50);
        StickCP2.Display.print("FS Error!");
        while (1) delay(1000);
    }

    // Load food database
    loadFoodsDatabase();

    // Initial display - main menu
    currentState = STATE_MAIN_MENU;
    currentIndex = 0;
    drawMainMenu();

    // Initialize power button state to avoid false trigger on startup
    lastPwrState = (digitalRead(35) == LOW);

    // Initialize inactivity timer
    lastActivityTime = millis();
}

void loop() {
    StickCP2.update();

    // Update WiFi state (non-blocking)
    wifiUpdate();

    // Redraw WiFi indicator if state changed
    static WifiState lastWifiState = WIFI_STATE_IDLE;
    WifiState wifiState = getWifiState();
    if (wifiState != lastWifiState || wifiState == WIFI_STATE_CONNECTING) {
        drawWifiIndicator();
        lastWifiState = wifiState;
    }

    // Power button (GPIO35, left side): Next item (short press)
    // Read directly with debounce for reliable detection
    bool pwrPressed = (digitalRead(35) == LOW);
    unsigned long now = millis();

    if (pwrPressed && !lastPwrState && (now - lastPwrPress > PWR_DEBOUNCE)) {
        lastPwrPress = now;
        lastActivityTime = millis();  // Reset inactivity timer
        switch (currentState) {
            case STATE_MAIN_MENU: {
                // Online: Voice Search, Browse Foods, Settings (3 items)
                // Offline: Browse Foods, Settings (2 items)
                int totalItems = isOnline() ? 3 : 2;
                currentIndex = (currentIndex + 1) % totalItems;
                drawMainMenu();
                break;
            }
            case STATE_CATEGORIES: {
                currentIndex = (currentIndex + 1) % categoryCount;
                drawCategories();
                break;
            }
            case STATE_FOODS:
                currentIndex = (currentIndex + 1) % itemCount;
                resetScroll(getName(*filteredFoods[currentIndex]));
                drawFoods();
                break;
            case STATE_SETTINGS:
            case STATE_RESULT:
            case STATE_RECORDING:
                // No next action on these screens
                break;
        }
    }
    lastPwrState = pwrPressed;

    // Button A (big M5): Select / Change
    if (StickCP2.BtnA.wasPressed()) {
        lastActivityTime = millis();  // Reset inactivity timer
        switch (currentState) {
            case STATE_MAIN_MENU: {
                // Online: 0=Voice Search, 1=Browse Foods, 2=Settings
                // Offline: 0=Browse Foods, 1=Settings
                if (isOnline()) {
                    if (currentIndex == 0) {
                        // Voice Search selected - start recording
                        if (audioStartRecording()) {
                            currentState = STATE_RECORDING;
                        } else {
                            // Audio init failed - show error briefly
                            StickCP2.Display.fillScreen(TFT_BLACK);
                            StickCP2.Display.setTextColor(TFT_RED);
                            StickCP2.Display.setTextSize(2);
                            StickCP2.Display.setCursor(30, 50);
                            StickCP2.Display.print("Audio Error!");
                            delay(1500);
                            drawMainMenu();
                        }
                    } else if (currentIndex == 1) {
                        // Browse Foods selected
                        currentState = STATE_CATEGORIES;
                        currentIndex = 0;
                        drawCategories();
                    } else {
                        // Settings selected
                        currentState = STATE_SETTINGS;
                        drawSettings();
                    }
                } else {
                    if (currentIndex == 0) {
                        // Browse Foods selected
                        currentState = STATE_CATEGORIES;
                        currentIndex = 0;
                        drawCategories();
                    } else {
                        // Settings selected
                        currentState = STATE_SETTINGS;
                        drawSettings();
                    }
                }
                break;
            }
            case STATE_CATEGORIES: {
                // Select category -> show foods
                selectedCategory = categories[currentIndex].id;
                filterFoodsByCategory(selectedCategory);
                if (filteredCount > 0) {
                    currentState = STATE_FOODS;
                    currentIndex = 0;
                    itemCount = filteredCount;
                    resetScroll(getName(*filteredFoods[0]));
                    drawFoods();
                }
                break;
            }
            case STATE_FOODS:
                // Select food -> show result
                currentState = STATE_RESULT;
                resetScroll(getName(*filteredFoods[currentIndex]));
                drawResult();
                break;
            case STATE_SETTINGS:
                // Toggle language
                toggleLanguage();
                drawSettings();
                break;
            case STATE_RESULT:
                // No action
                break;
            case STATE_RECORDING:
                // No action
                break;
        }
    }

    // Button B (right side): Back
    if (StickCP2.BtnB.wasPressed()) {
        lastActivityTime = millis();  // Reset inactivity timer
        switch (currentState) {
            case STATE_MAIN_MENU:
                // Already at top level, no action
                break;
            case STATE_CATEGORIES:
                // Back to main menu
                currentState = STATE_MAIN_MENU;
                currentIndex = 0;
                drawMainMenu();
                break;
            case STATE_FOODS:
                // Back to categories
                currentState = STATE_CATEGORIES;
                currentIndex = 0;
                drawCategories();
                break;
            case STATE_RESULT:
                // Back to foods list
                currentState = STATE_FOODS;
                itemCount = filteredCount;
                drawFoods();
                break;
            case STATE_SETTINGS:
                // Back to main menu
                currentState = STATE_MAIN_MENU;
                currentIndex = 0;
                drawMainMenu();
                break;
            case STATE_RECORDING:
                // Cancel recording, back to main menu
                audioReset();
                currentState = STATE_MAIN_MENU;
                currentIndex = 0;
                drawMainMenu();
                break;
        }
    }

    // Handle recording state
    if (currentState == STATE_RECORDING) {
        audioUpdate();

        AudioState audioState = getAudioState();
        if (audioState == AUDIO_COMPLETE) {
            // Recording done - for now, just go back to main menu
            // In Phase 5, this will trigger API call
            audioReset();
            currentState = STATE_MAIN_MENU;
            currentIndex = 0;
            drawMainMenu();
        } else if (audioState == AUDIO_ERROR) {
            // Audio error during recording - recover gracefully
            audioReset();
            StickCP2.Display.fillScreen(TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_RED);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.setCursor(30, 50);
            StickCP2.Display.print("Audio Error!");
            delay(1500);
            currentState = STATE_MAIN_MENU;
            currentIndex = 0;
            drawMainMenu();
        }
    }

    // Update scrolling text
    if (currentState == STATE_RESULT) {
        if (updateScroll()) {
            // Redraw just the name area
            StickCP2.Display.fillRect(10, 8, 220, 20, TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_WHITE);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.setCursor(10, 8);
            StickCP2.Display.print(getScrolledText(scrollText));
        }
    } else if (currentState == STATE_FOODS) {
        if (updateScroll()) {
            // Calculate Y position of highlighted item
            int startIdx = 0;
            if (currentIndex >= 3) {
                startIdx = currentIndex - 2;
                if (startIdx + 5 > filteredCount) {
                    startIdx = filteredCount - 5;
                    if (startIdx < 0) startIdx = 0;
                }
            }
            int highlightRow = currentIndex - startIdx;
            int y = 25 + (highlightRow * 22);

            // Redraw just the highlighted item's text area
            StickCP2.Display.fillRect(10, y, 220, 16, TFT_BLUE);
            StickCP2.Display.setTextColor(TFT_WHITE);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.setCursor(10, y);
            StickCP2.Display.print(getScrolledText(scrollText));
        }
    }

    // Check for inactivity timeout
    if (millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
        // Visual feedback before sleep
        StickCP2.Display.fillScreen(TFT_BLACK);
        StickCP2.Display.setTextColor(TFT_DARKGREY);
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(40, 55);
        StickCP2.Display.print("Sleeping...");
        delay(1000);

        // Disable WiFi to save power
        wifiDisable();

        // Turn off display
        StickCP2.Display.sleep();

        // Configure GPIO35 (power button) as wake source - wake on LOW (button press)
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);

        // Enter deep sleep
        esp_deep_sleep_start();
    }

    delay(50);
}

void loadFoodsDatabase() {
    File file = LittleFS.open("/foods.json", "r");
    if (!file) {
        StickCP2.Display.fillScreen(TFT_RED);
        StickCP2.Display.setCursor(10, 50);
        StickCP2.Display.print("No foods.json!");
        while (1) delay(1000);
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        StickCP2.Display.fillScreen(TFT_RED);
        StickCP2.Display.setCursor(10, 30);
        StickCP2.Display.print("JSON Error!");
        StickCP2.Display.setCursor(10, 60);
        StickCP2.Display.setTextSize(1);
        StickCP2.Display.print(error.c_str());
        while (1) delay(1000);
    }

    // Load categories
    JsonArray cats = doc["categories"];
    categoryCount = 0;
    for (JsonObject cat : cats) {
        if (categoryCount < 8) {
            categories[categoryCount].id = cat["id"].as<String>();
            categories[categoryCount].name_pt = cat["name_pt"].as<String>();
            categories[categoryCount].name_en = cat["name_en"].as<String>();
            categoryCount++;
        }
    }

    // Load foods
    JsonArray foodsArray = doc["foods"];
    foodCount = 0;
    for (JsonObject food : foodsArray) {
        if (foodCount < 200) {
            foods[foodCount].name_pt = food["name_pt"].as<String>();
            foods[foodCount].name_en = food["name_en"].as<String>();
            foods[foodCount].category = food["category"].as<String>();
            foods[foodCount].fodmap = food["fodmap"].as<String>();
            foods[foodCount].gluten = food["gluten"].as<bool>();
            foodCount++;
        }
    }
}

void filterFoodsByCategory(const String& categoryId) {
    filteredCount = 0;
    for (int i = 0; i < foodCount; i++) {
        if (foods[i].category == categoryId) {
            filteredFoods[filteredCount] = &foods[i];
            filteredCount++;
        }
    }
}

void drawMainMenu() {
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Title
    StickCP2.Display.setTextColor(TFT_GREEN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(5, 5);
    StickCP2.Display.print("Safe Bite");

    // Draw line under title
    StickCP2.Display.drawLine(0, 25, 240, 25, TFT_DARKGREY);

    // Calculate total items
    // Online: Voice Search, Browse Foods, Settings (3 items)
    // Offline: Browse Foods, Settings (2 items)
    bool online = isOnline();
    int totalItems = online ? 3 : 2;

    // Draw menu items
    int y = 35;
    for (int i = 0; i < totalItems; i++) {
        if (i == currentIndex) {
            // Highlighted item - blue background
            StickCP2.Display.fillRect(0, y - 2, 240, 20, TFT_BLUE);
            StickCP2.Display.setTextColor(TFT_WHITE);
        } else {
            StickCP2.Display.setTextColor(TFT_LIGHTGREY);
        }

        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, y);

        if (online) {
            // Online mode: Voice Search, Browse Foods, Settings
            if (i == 0) {
                StickCP2.Display.print(STR(STR_VOICE_SEARCH));
            } else if (i == 1) {
                StickCP2.Display.print(STR(STR_BROWSE_FOODS));
            } else {
                StickCP2.Display.print(STR(STR_SETTINGS));
            }
        } else {
            // Offline mode: Browse Foods, Settings
            if (i == 0) {
                StickCP2.Display.print(STR(STR_BROWSE_FOODS));
            } else {
                StickCP2.Display.print(STR(STR_SETTINGS));
            }
        }
        y += 25;
    }

    // Navigation hint
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print(STR(STR_NAV_NEXT_SEL));

    // WiFi indicator
    drawWifiIndicator();
}

void drawCategories() {
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Title
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 5);
    StickCP2.Display.print(STR(STR_TITLE));

    // Draw line under title
    StickCP2.Display.drawLine(0, 18, 240, 18, TFT_DARKGREY);

    // Calculate visible items (show 5 items max)
    int startIdx = 0;
    if (currentIndex >= 3) {
        startIdx = currentIndex - 2;
        if (startIdx + 5 > categoryCount) {
            startIdx = categoryCount - 5;
            if (startIdx < 0) startIdx = 0;
        }
    }

    // Draw category items
    int y = 25;
    for (int i = startIdx; i < min(startIdx + 5, categoryCount); i++) {
        if (i == currentIndex) {
            // Highlighted item
            StickCP2.Display.fillRect(0, y - 2, 240, 20, TFT_BLUE);
            StickCP2.Display.setTextColor(TFT_WHITE);
        } else {
            StickCP2.Display.setTextColor(TFT_LIGHTGREY);
        }

        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, y);
        StickCP2.Display.print(getName(categories[i]));
        y += 22;
    }

    // Navigation hint
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print(STR(STR_NAV_NEXT_SEL));
    StickCP2.Display.print("  ");
    StickCP2.Display.print(STR(STR_NAV_BACK));

    // WiFi indicator
    drawWifiIndicator();
}

void drawFoods() {
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Title - show category name
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 5);

    // Find category name
    for (int i = 0; i < categoryCount; i++) {
        if (categories[i].id == selectedCategory) {
            StickCP2.Display.print(getName(categories[i]));
            break;
        }
    }

    // Item count
    StickCP2.Display.setCursor(180, 5);
    StickCP2.Display.printf("%d/%d", currentIndex + 1, filteredCount);

    // Draw line under title
    StickCP2.Display.drawLine(0, 18, 240, 18, TFT_DARKGREY);

    // Calculate visible items
    int startIdx = 0;
    if (currentIndex >= 3) {
        startIdx = currentIndex - 2;
        if (startIdx + 5 > filteredCount) {
            startIdx = filteredCount - 5;
            if (startIdx < 0) startIdx = 0;
        }
    }

    // Draw foods
    int y = 25;
    for (int i = startIdx; i < min(startIdx + 5, filteredCount); i++) {
        if (i == currentIndex) {
            // Highlighted item
            StickCP2.Display.fillRect(0, y - 2, 240, 20, TFT_BLUE);
            StickCP2.Display.setTextColor(TFT_WHITE);
        } else {
            StickCP2.Display.setTextColor(TFT_LIGHTGREY);
        }

        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, y);

        String name = getName(*filteredFoods[i]);
        if (i == currentIndex) {
            // Highlighted item: use scrolling
            StickCP2.Display.print(getScrolledText(name));
        } else {
            // Non-highlighted: truncate as before
            if (name.length() > MAX_DISPLAY_CHARS) {
                name = name.substring(0, MAX_DISPLAY_CHARS - 1) + "..";
            }
            StickCP2.Display.print(name);
        }
        y += 22;
    }

    // Navigation hint
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print(STR(STR_NAV_NEXT_SEL));

    // WiFi indicator
    drawWifiIndicator();
}

void drawResult() {
    Food* food = filteredFoods[currentIndex];

    StickCP2.Display.fillScreen(TFT_BLACK);

    // Food name at top
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 8);

    String name = getName(*food);
    StickCP2.Display.print(getScrolledText(name));

    // FODMAP section
    uint16_t fodmapColor = getFodmapColor(food->fodmap);
    StickCP2.Display.fillRect(0, 35, 240, 40, fodmapColor);

    // FODMAP text - dark text on light backgrounds
    if (fodmapColor == TFT_YELLOW || fodmapColor == TFT_GREEN) {
        StickCP2.Display.setTextColor(TFT_BLACK);
    } else {
        StickCP2.Display.setTextColor(TFT_WHITE);
    }
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 45);
    StickCP2.Display.print(STR_FODMAP_LABEL);
    StickCP2.Display.print(getFodmapLabel(food->fodmap));

    // Gluten section
    uint16_t glutenColor = food->gluten ? TFT_RED : TFT_GREEN;
    StickCP2.Display.fillRect(0, 80, 240, 40, glutenColor);

    // Dark text on green background for readability
    StickCP2.Display.setTextColor(food->gluten ? TFT_WHITE : TFT_BLACK);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 90);
    StickCP2.Display.print(STR_GLUTEN_LABEL);
    StickCP2.Display.print(food->gluten ? STR(STR_YES) : STR(STR_NO));

    // Back hint
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print(STR(STR_NAV_BACK));

    // WiFi indicator
    drawWifiIndicator();
}

void drawSettings() {
    StickCP2.Display.fillScreen(TFT_BLACK);

    // Title
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 5);
    StickCP2.Display.print(STR(STR_SETTINGS));

    StickCP2.Display.drawLine(0, 18, 240, 18, TFT_DARKGREY);

    // Language option (highlighted)
    StickCP2.Display.fillRect(0, 25, 240, 20, TFT_BLUE);
    StickCP2.Display.setTextColor(TFT_WHITE);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 27);
    StickCP2.Display.print(STR(STR_LANGUAGE));

    // Current language value
    StickCP2.Display.setTextColor(TFT_CYAN);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(10, 55);
    StickCP2.Display.print("> ");
    StickCP2.Display.print(STR(STR_LANG_NAME));

    // Navigation hint
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print(STR(STR_NAV_SETTINGS));

    // WiFi indicator
    drawWifiIndicator();
}

uint16_t getFodmapColor(const String& level) {
    if (level == "low") return COLOR_LOW;
    if (level == "moderate") return COLOR_MODERATE;
    if (level == "high") return COLOR_HIGH;
    return COLOR_UNKNOWN;
}

String getFodmapLabel(const String& level) {
    if (level == "low") return STR(STR_FODMAP_LOW);
    if (level == "moderate") return STR(STR_FODMAP_MOD);
    if (level == "high") return STR(STR_FODMAP_HIGH);
    return "?";
}

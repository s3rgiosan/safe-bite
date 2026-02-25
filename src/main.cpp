#include <M5Unified.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "language.h"
#include "wifi_manager.h"
#include "audio_manager.h"
#include "mistral_client.h"
#include "fonts/DejaVuSans6pt_Latin.h"
#include "fonts/DejaVuSans8pt_Latin.h"
#include "fonts/DejaVuSans9pt_Latin.h"
#include "fonts/DejaVuSans14pt_Latin.h"

// DejaVu proportional font aliases (custom: ASCII + Latin-1, U+0020–U+00FF)
#define FONT_SMALL  &DejaVuSans6pt8b
#define FONT_HEADER &DejaVuSans8pt8b
#define FONT_MEDIUM &DejaVuSans9pt8b
#define FONT_LARGE  &DejaVuSans14pt8b

// Language state
uint8_t currentLang = LANG_EN;
Preferences prefs;

// String arrays [EN, PT]
const char* STR_LOADING[] = {"Loading...", "A carregar..."};
const char* STR_TITLE[] = {"Safe Bite - Categories", "Safe Bite - Categorias"};
const char* STR_NAV_NEXT_SEL[] = {"PWR:Next  M5:Select", "PWR:Próx  M5:Escolher"};
const char* STR_NAV_BACK[] = {"B:Back", "B:Voltar"};
const char* STR_YES[] = {"YES", "SIM"};
const char* STR_NO[] = {"NO", "NÃO"};
const char* STR_FODMAP_LOW[] = {"LOW", "BAIXO"};
const char* STR_FODMAP_MOD[] = {"MOD", "MOD"};
const char* STR_FODMAP_HIGH[] = {"HIGH", "ALTO"};
const char* STR_SETTINGS[] = {"Settings", "Definições"};
const char* STR_LANGUAGE[] = {"Language", "Idioma"};
const char* STR_LANG_NAME[] = {"English", "Português"};
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
const char* STR_ERROR_API[]    = {"API Error", "Erro de API"};
const char* STR_NOT_FOOD[]     = {"Not a food", "Não é alimento"};
const char* STR_TRY_AGAIN[]    = {"Try again", "Tente novamente"};

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
    STATE_RECORDING,
    STATE_AI_PROCESSING
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

// Voice search result state
static Food voiceResultFood;
static bool voiceResultActive = false;

// Display colors
const uint16_t COLOR_LOW = TFT_GREEN;
const uint16_t COLOR_MODERATE = TFT_YELLOW;
const uint16_t COLOR_HIGH = TFT_RED;
const uint16_t COLOR_UNKNOWN = TFT_BLUE;

// Scroll state for long text
String scrollText = "";
int scrollPos = 0;
unsigned long lastScrollTime = 0;
const int SCROLL_DELAY = 300;        // ms between scroll steps
const int SCROLL_PAUSE = 1500;       // ms pause at start/end
const int SCROLL_AREA_WIDTH = 220;   // pixels available for scrolling text
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
void drawProcessing();
void drawError(const char* title, const char* detail);
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

// Get the visible portion of scrolling text (pixel-based for proportional fonts)
String getScrolledText(const String& text) {
    if (M5.Display.textWidth(text.c_str()) <= SCROLL_AREA_WIDTH) {
        return text;
    }

    // Add padding for smooth loop
    String padded = text + "   " + text;
    String visible = padded.substring(scrollPos);
    int len = M5.Display.textLength(visible.c_str(), SCROLL_AREA_WIDTH);
    return visible.substring(0, len);
}

// Update scroll position (call from loop)
bool updateScroll() {
    if (M5.Display.textWidth(scrollText.c_str()) <= SCROLL_AREA_WIDTH) {
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

// Power button debounce
unsigned long lastPwrPress = 0;
const unsigned long PWR_DEBOUNCE = 200;  // 200ms debounce

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::top_left);

    // Splash screen — vertically and horizontally centered
    M5.Display.setFont(FONT_LARGE);
    int titleW = M5.Display.textWidth("Safe Bite");
    int titleH = M5.Display.fontHeight();
    M5.Display.setFont(FONT_SMALL);
    int tagW = M5.Display.textWidth("Food Safety Checker");
    int tagH = M5.Display.fontHeight();
    int gap = 8;
    int totalH = titleH + gap + tagH;
    int startY = (135 - totalH) / 2;

    M5.Display.setFont(FONT_LARGE);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setCursor((240 - titleW) / 2, startY);
    M5.Display.print("Safe Bite");

    M5.Display.setFont(FONT_SMALL);
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.setCursor((240 - tagW) / 2, startY + titleH + gap);
    M5.Display.print("Food Safety Checker");

    delay(1500);  // Show for 1.5 seconds
    M5.Display.fillScreen(TFT_BLACK);  // Clear before loading screen

    // Initialize WiFi (non-blocking)
    wifiInit();

    // Initialize audio recording (allocate buffer)
    if (!audioInit()) {
        Serial.println("WARNING: Audio buffer allocation failed - voice search unavailable");
    }

    // Power button is handled by M5Unified via M5.BtnPWR

    // Load saved language preference
    loadLanguage();

    // Show loading screen
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setCursor(20, 50);
    M5.Display.print(STR(STR_LOADING));

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setFont(FONT_MEDIUM);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setCursor(10, 50);
        M5.Display.print("FS Error!");
        while (1) delay(1000);
    }

    // Load food database
    loadFoodsDatabase();

    // Initial display - main menu
    currentState = STATE_MAIN_MENU;
    currentIndex = 0;
    drawMainMenu();

    // Initialize inactivity timer
    lastActivityTime = millis();
}

void loop() {
    M5.update();

    // Update WiFi state (non-blocking)
    wifiUpdate();

    // Redraw WiFi indicator if state changed (main menu only)
    static WifiState lastWifiState = WIFI_STATE_IDLE;
    WifiState wifiState = getWifiState();
    if ((wifiState != lastWifiState || wifiState == WIFI_STATE_CONNECTING)
        && currentState == STATE_MAIN_MENU) {
        drawWifiIndicator();
        lastWifiState = wifiState;
    }

    // Power button (left side): Next item (short press)
    unsigned long now = millis();

    if (M5.BtnPWR.wasClicked() && (now - lastPwrPress > PWR_DEBOUNCE)) {
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
            case STATE_AI_PROCESSING:
                // No next action on these screens
                break;
        }
    }

    // Button A (big M5): Select / Change
    if (M5.BtnA.wasPressed()) {
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
                            drawError("Audio Error", STR(STR_TRY_AGAIN));
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
                // M5 confirms and sends audio early
                audioStopRecording();
                break;
            case STATE_AI_PROCESSING:
                // Blocking call in progress; unreachable but keeps switch exhaustive
                break;
        }
    }

    // Button B (right side): Back
    if (M5.BtnB.wasPressed()) {
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
                if (voiceResultActive) {
                    voiceResultActive = false;
                    currentState = STATE_MAIN_MENU;
                    currentIndex = 0;
                    drawMainMenu();
                } else {
                    currentState = STATE_FOODS;
                    itemCount = filteredCount;
                    drawFoods();
                }
                break;
            case STATE_AI_PROCESSING:
                // Blocking call in progress; unreachable but keeps switch exhaustive
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
            uint8_t* wavData = getWavBuffer();
            size_t   wavSize = getWavBufferSize();
            audioReset();  // stops mic, preserves heap buffer

            currentState = STATE_AI_PROCESSING;
            drawProcessing();

            Serial.printf("[VOICE] WAV size: %u bytes\n", wavSize);

            // Save WAV to temp file so we can free the 96KB buffer before TLS
            static const char* TMP_WAV = "/tmp.wav";
            bool saved = false;
            File tmpFile = LittleFS.open(TMP_WAV, "w");
            if (tmpFile) {
                size_t written = tmpFile.write(wavData, wavSize);
                saved = (written == wavSize);
                tmpFile.close();
                Serial.printf("[VOICE] Saved to LittleFS: %u/%u bytes\n", written, wavSize);
            } else {
                Serial.println("[VOICE] Failed to open tmp.wav for writing");
            }

            // Free audio buffer — ~96KB back for TLS
            audioFreeBuffer();
            Serial.printf("[VOICE] Buffer freed. Free heap: %u\n", ESP.getFreeHeap());

            MistralResult res;
            res.success = false;
            res.notFood = false;
            res.fodmap = "unknown";
            res.gluten = false;

            if (!saved) {
                res.errorMsg = "File save err";
            } else {
                // Step 1: Transcribe (streams from file, buffer is freed)
                String sttError;
                String transcript = mistralTranscribeFile(TMP_WAV, wavSize, sttError);
                LittleFS.remove(TMP_WAV);

                if (transcript.length() == 0) {
                    res.errorMsg = sttError.length() > 0 ? sttError : "No transcript";
                } else {
                    res.transcribedText = transcript;
                    // Step 2: Classify (only needs text string)
                    String classifyError;
                    String fodmapOut;
                    bool glutenOut = false;
                    bool notFood = false;
                    if (mistralClassify(transcript, fodmapOut, glutenOut, notFood, classifyError)) {
                        if (notFood) {
                            res.notFood = true;
                        } else {
                            res.fodmap = fodmapOut;
                            res.gluten = glutenOut;
                            res.success = true;
                        }
                    } else {
                        res.errorMsg = classifyError;
                    }
                }
            }

            if (res.success) {
                voiceResultFood = { res.transcribedText, res.transcribedText, "", res.fodmap, res.gluten };
                filteredFoods[0] = &voiceResultFood;
                currentIndex = 0;
                voiceResultActive = true;
                currentState = STATE_RESULT;
                resetScroll(res.transcribedText);
                drawResult();
            } else if (res.notFood) {
                drawError(STR(STR_NOT_FOOD), STR(STR_TRY_AGAIN));
                delay(2500);
                currentState = STATE_MAIN_MENU;
                currentIndex = 0;
                drawMainMenu();
            } else {
                drawError(STR(STR_ERROR_API), res.errorMsg.c_str());
                delay(2500);
                currentState = STATE_MAIN_MENU;
                currentIndex = 0;
                drawMainMenu();
            }
        } else if (audioState == AUDIO_ERROR) {
            // Audio error during recording - recover gracefully
            audioReset();
            drawError("Audio Error", STR(STR_TRY_AGAIN));
            delay(1500);
            currentState = STATE_MAIN_MENU;
            currentIndex = 0;
            drawMainMenu();
        }
    }

    // Update scrolling text (font must be set before pixel measurement)
    if (currentState == STATE_RESULT) {
        M5.Display.setFont(FONT_HEADER);
        if (updateScroll()) {
            // Redraw just the name area
            M5.Display.fillRect(10, 8, 220, 20, TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE);
            M5.Display.setCursor(10, 8);
            M5.Display.print(getScrolledText(scrollText));
        }
    } else if (currentState == STATE_FOODS) {
        M5.Display.setFont(FONT_MEDIUM);
        if (updateScroll()) {
            // Calculate Y position of highlighted item
            int startIdx = 0;
            if (currentIndex >= 2) {
                startIdx = currentIndex - 1;
                if (startIdx + 4 > filteredCount) {
                    startIdx = filteredCount - 4;
                    if (startIdx < 0) startIdx = 0;
                }
            }
            int highlightRow = currentIndex - startIdx;
            int y = 32 + (highlightRow * 22);

            // Redraw just the highlighted item's text area
            M5.Display.fillRect(10, y + 1, 220, 21, TFT_BLUE);
            M5.Display.setTextColor(TFT_WHITE);
            M5.Display.setCursor(10, y);
            M5.Display.print(getScrolledText(scrollText));
        }
    }

    // Check for inactivity timeout
    if (millis() - lastActivityTime > INACTIVITY_TIMEOUT) {
        // Visual feedback before sleep
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_DARKGREY);
        M5.Display.setFont(FONT_MEDIUM);
        M5.Display.setCursor(40, 55);
        M5.Display.print("Sleeping...");
        delay(1000);

        // Disable WiFi to save power
        wifiDisable();

        // Turn off display
        M5.Display.sleep();

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
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setCursor(10, 50);
        M5.Display.print("No foods.json!");
        while (1) delay(1000);
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setFont(FONT_MEDIUM);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setCursor(10, 30);
        M5.Display.print("JSON Error!");
        M5.Display.setFont(FONT_SMALL);
        M5.Display.setCursor(10, 60);
        M5.Display.print(error.c_str());
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
    M5.Display.fillScreen(TFT_BLACK);

    // Title
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setCursor(5, 8);
    M5.Display.print("Safe Bite");

    // Draw line under title
    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // Calculate total items
    // Online: Voice Search, Browse Foods, Settings (3 items)
    // Offline: Browse Foods, Settings (2 items)
    bool online = isOnline();
    int totalItems = online ? 3 : 2;

    // Draw menu items
    int y = 32;
    for (int i = 0; i < totalItems; i++) {
        if (i == currentIndex) {
            // Highlighted item - blue background
            M5.Display.fillRect(0, y + 1, 240, 21, TFT_BLUE);
            M5.Display.setTextColor(TFT_WHITE);
        } else {
            M5.Display.setTextColor(TFT_LIGHTGREY);
        }

        M5.Display.setFont(FONT_MEDIUM);
        M5.Display.setCursor(10, y);

        if (online) {
            // Online mode: Voice Search, Browse Foods, Settings
            if (i == 0) {
                M5.Display.print(STR(STR_VOICE_SEARCH));
            } else if (i == 1) {
                M5.Display.print(STR(STR_BROWSE_FOODS));
            } else {
                M5.Display.print(STR(STR_SETTINGS));
            }
        } else {
            // Offline mode: Browse Foods, Settings
            if (i == 0) {
                M5.Display.print(STR(STR_BROWSE_FOODS));
            } else {
                M5.Display.print(STR(STR_SETTINGS));
            }
        }
        y += 22;
    }

    // Navigation hint
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_NEXT_SEL));

    // WiFi indicator
    drawWifiIndicator();
}

void drawCategories() {
    M5.Display.fillScreen(TFT_BLACK);

    // Title
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setCursor(5, 8);
    M5.Display.print(STR(STR_TITLE));

    // Draw line under title
    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // Calculate visible items (show 4 items max)
    int startIdx = 0;
    if (currentIndex >= 2) {
        startIdx = currentIndex - 1;
        if (startIdx + 4 > categoryCount) {
            startIdx = categoryCount - 4;
            if (startIdx < 0) startIdx = 0;
        }
    }

    // Draw category items
    int y = 32;
    for (int i = startIdx; i < min(startIdx + 4, categoryCount); i++) {
        if (i == currentIndex) {
            // Highlighted item
            M5.Display.fillRect(0, y + 1, 240, 21, TFT_BLUE);
            M5.Display.setTextColor(TFT_WHITE);
        } else {
            M5.Display.setTextColor(TFT_LIGHTGREY);
        }

        M5.Display.setFont(FONT_MEDIUM);
        M5.Display.setCursor(10, y);
        M5.Display.print(getName(categories[i]));
        y += 22;
    }

    // Navigation hint
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_NEXT_SEL));
    M5.Display.print("  ");
    M5.Display.print(STR(STR_NAV_BACK));
}

void drawFoods() {
    M5.Display.fillScreen(TFT_BLACK);

    // Title - show category name
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setCursor(5, 8);

    // Find category name
    for (int i = 0; i < categoryCount; i++) {
        if (categories[i].id == selectedCategory) {
            M5.Display.print(getName(categories[i]));
            break;
        }
    }

    // Item count (right-aligned)
    char countBuf[10];
    snprintf(countBuf, sizeof(countBuf), "%d/%d", currentIndex + 1, filteredCount);
    int countW = M5.Display.textWidth(countBuf);
    M5.Display.setCursor(235 - countW, 8);
    M5.Display.print(countBuf);

    // Draw line under title
    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // Calculate visible items (show 4 items max)
    int startIdx = 0;
    if (currentIndex >= 2) {
        startIdx = currentIndex - 1;
        if (startIdx + 4 > filteredCount) {
            startIdx = filteredCount - 4;
            if (startIdx < 0) startIdx = 0;
        }
    }

    // Draw foods
    M5.Display.setFont(FONT_MEDIUM);
    int y = 32;
    for (int i = startIdx; i < min(startIdx + 4, filteredCount); i++) {
        if (i == currentIndex) {
            // Highlighted item
            M5.Display.fillRect(0, y + 1, 240, 21, TFT_BLUE);
            M5.Display.setTextColor(TFT_WHITE);
        } else {
            M5.Display.setTextColor(TFT_LIGHTGREY);
        }

        M5.Display.setCursor(10, y);

        String name = getName(*filteredFoods[i]);
        if (i == currentIndex) {
            // Highlighted item: use scrolling
            M5.Display.print(getScrolledText(name));
        } else {
            // Non-highlighted: truncate with ellipsis if too wide
            if (M5.Display.textWidth(name.c_str()) > SCROLL_AREA_WIDTH) {
                int ellipsisW = M5.Display.textWidth("..");
                int len = M5.Display.textLength(name.c_str(), SCROLL_AREA_WIDTH - ellipsisW);
                name = name.substring(0, len) + "..";
            }
            M5.Display.print(name);
        }
        y += 22;
    }

    // Navigation hint
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_NEXT_SEL));
}

void drawProcessing() {
    M5.Display.fillScreen(TFT_BLACK);

    // Header - consistent with other screens
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setCursor(5, 8);
    M5.Display.print(STR(STR_VOICE_SEARCH));

    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // "Processing..." centred in cyan
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(10, 55);
    M5.Display.print(STR(STR_PROCESSING));
}

void drawError(const char* title, const char* detail) {
    M5.Display.fillScreen(TFT_BLACK);

    // Header - consistent with other screens
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setCursor(5, 8);
    M5.Display.print(title);

    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // Detail text
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(10, 40);
    M5.Display.print(detail);

    // Hint - consistent with other screens
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_BACK));
}

void drawResult() {
    Food* food = filteredFoods[currentIndex];

    M5.Display.fillScreen(TFT_BLACK);

    // Food name at top
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(10, 8);

    String name = getName(*food);
    M5.Display.print(getScrolledText(name));

    // FODMAP section
    uint16_t fodmapColor = getFodmapColor(food->fodmap);
    M5.Display.fillRect(0, 35, 240, 40, fodmapColor);

    // FODMAP text - dark text on light backgrounds
    if (fodmapColor == TFT_YELLOW || fodmapColor == TFT_GREEN) {
        M5.Display.setTextColor(TFT_BLACK);
    } else {
        M5.Display.setTextColor(TFT_WHITE);
    }
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setCursor(10, 45);
    M5.Display.print(STR_FODMAP_LABEL);
    M5.Display.print(getFodmapLabel(food->fodmap));

    // Gluten section
    uint16_t glutenColor = food->gluten ? TFT_RED : TFT_GREEN;
    M5.Display.fillRect(0, 80, 240, 40, glutenColor);

    // Dark text on green background for readability
    M5.Display.setTextColor(food->gluten ? TFT_WHITE : TFT_BLACK);
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setCursor(10, 90);
    M5.Display.print(STR_GLUTEN_LABEL);
    M5.Display.print(food->gluten ? STR(STR_YES) : STR(STR_NO));

    // Back hint
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_BACK));
}

void drawSettings() {
    M5.Display.fillScreen(TFT_BLACK);

    // Title
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_HEADER);
    M5.Display.setCursor(5, 8);
    M5.Display.print(STR(STR_SETTINGS));

    M5.Display.drawLine(0, 28, 240, 28, TFT_DARKGREY);

    // Language option (highlighted)
    M5.Display.fillRect(0, 35, 240, 18, TFT_BLUE);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setCursor(10, 32);
    M5.Display.print(STR(STR_LANGUAGE));

    // Current language value
    M5.Display.setFont(FONT_MEDIUM);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(10, 55);
    M5.Display.print("> ");
    M5.Display.print(STR(STR_LANG_NAME));

    // Navigation hint
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print(STR(STR_NAV_SETTINGS));
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

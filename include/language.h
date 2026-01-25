#ifndef LANGUAGE_H
#define LANGUAGE_H

#include <Preferences.h>

// Language constants
#define LANG_EN 0
#define LANG_PT 1
#define LANG_COUNT 2

// Global language state (defined in main.cpp)
extern uint8_t currentLang;
extern Preferences prefs;

// String accessor macro (index into arrays)
#define STR(arr) (arr[currentLang])

// UI Strings - [EN, PT]
extern const char* STR_LOADING[];
extern const char* STR_TITLE[];
extern const char* STR_NAV_NEXT_SEL[];
extern const char* STR_NAV_BACK[];
extern const char* STR_YES[];
extern const char* STR_NO[];
extern const char* STR_FODMAP_LOW[];
extern const char* STR_FODMAP_MOD[];
extern const char* STR_FODMAP_HIGH[];
extern const char* STR_SETTINGS[];
extern const char* STR_LANGUAGE[];
extern const char* STR_LANG_NAME[];
extern const char* STR_NAV_SETTINGS[];

// WiFi status strings
extern const char* STR_WIFI_CONNECTING[];
extern const char* STR_WIFI_CONNECTED[];
extern const char* STR_WIFI_OFFLINE[];

// Shared labels (same in both languages)
#define STR_FODMAP_LABEL  "FODMAP: "
#define STR_GLUTEN_LABEL  "GLUTEN: "

// Language functions
void loadLanguage();
void saveLanguage();
void toggleLanguage();

#endif

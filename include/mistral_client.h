#ifndef MISTRAL_CLIENT_H
#define MISTRAL_CLIENT_H

#include <Arduino.h>

struct MistralResult {
    bool   success;
    bool   notFood;   // true if input was not recognized as food
    String transcribedText;
    String fodmap;    // "low" | "moderate" | "high" | "unknown"
    bool   gluten;
    String errorMsg;
};

// Transcribe from a WAV file stored in LittleFS (avoids keeping audio buffer in heap during TLS)
String        mistralTranscribeFile(const char* wavPath, size_t wavSize, String& errorOut);
bool          mistralClassify(const String& text, String& fodmapOut, bool& glutenOut, bool& notFoodOut, String& errorOut);

#endif

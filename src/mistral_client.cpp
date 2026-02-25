#include "mistral_client.h"
#include "language.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>

#if __has_include("config.h")
    #include "config.h"
#endif

#if defined(MISTRAL_API_KEY)
    #define HAS_MISTRAL_CONFIG
#endif

static const char MISTRAL_HOST[] = "api.mistral.ai";
static const int  MISTRAL_PORT   = 443;
static const char BOUNDARY[]     = "safebite1234";

static const char SYSTEM_PROMPT[] =
    "You are a dietary assistant for people with FODMAP and gluten restrictions.\n"
    "The user input is a speech-to-text transcription of a food or meal description. "
    "It may contain transcription errors, partial words, or be in any language.\n"
    "The input can be a single food, an ingredient, or a composite meal "
    "(e.g. \"hamburger with cheese and onion in a bun\").\n"
    "First, determine if the input refers to real food(s) or ingredient(s).\n"
    "If it IS food, analyze ALL ingredients and respond EXACTLY in this format (two lines only):\n"
    "FODMAP: [LOW | MODERATE | HIGH]\n"
    "GLUTEN: [NO | YES]\n"
    "For composite meals: FODMAP = the HIGHEST level among all ingredients, "
    "GLUTEN = YES if ANY ingredient contains gluten.\n"
    "If it is NOT recognizable food (gibberish, greeting, unrelated phrase), respond:\n"
    "NOT_FOOD\n"
    "Never provide explanations.";

// Parse "FODMAP: LOW\nGLUTEN: YES" or "NOT_FOOD" response
// Returns false if NOT_FOOD
static bool parseClassifyResponse(const String& content, String& fodmapOut, bool& glutenOut) {
    String upper = content;
    upper.toUpperCase();

    if (upper.indexOf("NOT_FOOD") >= 0 || upper.indexOf("NOT FOOD") >= 0) {
        return false;
    }

    fodmapOut = "unknown";
    glutenOut = false;

    int fi = upper.indexOf("FODMAP:");
    if (fi >= 0) {
        String after = upper.substring(fi + 7);
        after.trim();
        if (after.startsWith("LOW"))  fodmapOut = "low";
        else if (after.startsWith("MOD")) fodmapOut = "moderate";
        else if (after.startsWith("HIG")) fodmapOut = "high";
    }

    int gi = upper.indexOf("GLUTEN:");
    if (gi >= 0) {
        String after = upper.substring(gi + 7);
        after.trim();
        if (after.startsWith("YES")) glutenOut = true;
    }
    return true;
}

// Skip HTTP response headers; return status code, set chunked flag
static int skipHeaders(WiFiClientSecure& client, bool& chunked) {
    chunked = false;

    // Read status line, skipping any leading blank lines
    String statusLine;
    while (client.connected() || client.available()) {
        statusLine = client.readStringUntil('\n');
        statusLine.trim();
        if (statusLine.length() > 0) break;
    }
    Serial.printf("[HTTP] Status: %s\n", statusLine.c_str());

    // Extract status code (e.g. "HTTP/1.1 200 OK" -> 200)
    int statusCode = 0;
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx > 0) {
        statusCode = statusLine.substring(spaceIdx + 1).toInt();
    }

    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
            chunked = true;
        }
        line.trim();
        if (line.length() == 0) break;
    }
    return statusCode;
}

// Read HTTP body, transparently handling chunked transfer encoding
static String readBodyStr(WiFiClientSecure& client, bool chunked) {
    if (!chunked) {
        return client.readString();
    }

    String result;
    while (client.connected() || client.available()) {
        String sizeLine = client.readStringUntil('\n');
        sizeLine.trim();
        if (sizeLine.length() == 0) continue;
        long sz = strtol(sizeLine.c_str(), nullptr, 16);
        if (sz == 0) break;
        while (sz > 0 && (client.connected() || client.available())) {
            char buf[64];
            int n = client.readBytes(buf, (size_t)min((long)64, sz));
            result += String(buf, n);
            sz -= n;
        }
        client.readStringUntil('\n');  // trailing \r\n after chunk data
    }
    return result;
}

String mistralTranscribeFile(const char* wavPath, size_t wavSize, String& errorOut) {
#ifndef HAS_MISTRAL_CONFIG
    errorOut = "No API key";
    return "";
#else
    Serial.printf("[STT] Free heap: %u, largest block: %u\n",
                  ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    Serial.println("[STT] Connecting...");
    if (!client.connect(MISTRAL_HOST, MISTRAL_PORT)) {
        Serial.printf("[STT] Connect failed. Free heap: %u, largest block: %u\n",
                      ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        errorOut = "Connect failed";
        return "";
    }
    Serial.println("[STT] Connected.");

    // Build multipart preamble (model + language + file field headers)
    const char* langCode = (currentLang == LANG_PT) ? "pt" : "en";

    String preamble = "--";
    preamble += BOUNDARY;
    preamble += "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nvoxtral-mini-latest\r\n";
    preamble += "--";
    preamble += BOUNDARY;
    preamble += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n";
    preamble += langCode;
    preamble += "\r\n";
    preamble += "--";
    preamble += BOUNDARY;
    preamble += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";

    String closing = "\r\n--";
    closing += BOUNDARY;
    closing += "--\r\n";

    size_t contentLength = preamble.length() + wavSize + closing.length();

    // Send HTTP request headers
    client.print("POST /v1/audio/transcriptions HTTP/1.1\r\n");
    client.print("Host: api.mistral.ai\r\n");
    client.print("Authorization: Bearer ");
    client.print(MISTRAL_API_KEY);
    client.print("\r\n");
    client.print("Content-Type: multipart/form-data; boundary=");
    client.print(BOUNDARY);
    client.print("\r\n");
    client.print("Content-Length: ");
    client.print((unsigned int)contentLength);
    client.print("\r\n");
    client.print("Connection: close\r\n\r\n");

    // Send multipart body: preamble
    client.print(preamble);

    // Stream WAV from file in small chunks (no large heap buffer needed)
    File f = LittleFS.open(wavPath, "r");
    if (!f) {
        errorOut = "File read err";
        client.stop();
        return "";
    }
    uint8_t chunk[512];
    while (f.available()) {
        int n = f.read(chunk, sizeof(chunk));
        if (n > 0) client.write(chunk, n);
    }
    f.close();

    client.print(closing);

    Serial.println("[STT] Data sent, waiting for response...");

    // Read and validate response
    bool chunked = false;
    int status = skipHeaders(client, chunked);
    if (status != 200) {
        String errBody = readBodyStr(client, chunked);
        Serial.printf("[STT] Error %d: %s\n", status, errBody.c_str());
        errorOut = "STT HTTP " + String(status);
        client.stop();
        return "";
    }

    String body = readBodyStr(client, chunked);
    client.stop();

    Serial.printf("[STT] Response: %s\n", body.c_str());

    JsonDocument filter;
    filter["text"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        errorOut = "STT JSON err";
        return "";
    }

    return doc["text"].as<String>();
#endif
}

bool mistralClassify(const String& text, String& fodmapOut, bool& glutenOut, bool& notFoodOut, String& errorOut) {
#ifndef HAS_MISTRAL_CONFIG
    errorOut = "No API key";
    return false;
#else
    // Build JSON request body once (reused on retry)
    JsonDocument reqDoc;
    reqDoc["model"] = "mistral-small-latest";
    reqDoc["max_tokens"] = 50;
    JsonArray messages = reqDoc["messages"].to<JsonArray>();
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = SYSTEM_PROMPT;
    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = text;

    String body;
    serializeJson(reqDoc, body);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            Serial.printf("[LLM] Retry %d after rate limit...\n", attempt);
            delay(2000 * attempt);  // 2s, 4s backoff
        }

        Serial.printf("[LLM] Free heap: %u, largest block: %u\n",
                      ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(15);

        Serial.println("[LLM] Connecting...");
        if (!client.connect(MISTRAL_HOST, MISTRAL_PORT)) {
            Serial.printf("[LLM] Connect failed. Free heap: %u, largest block: %u\n",
                          ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            errorOut = "Connect failed";
            return false;
        }
        Serial.println("[LLM] Connected.");

        // Send HTTP request headers + body
        client.print("POST /v1/chat/completions HTTP/1.1\r\n");
        client.print("Host: api.mistral.ai\r\n");
        client.print("Authorization: Bearer ");
        client.print(MISTRAL_API_KEY);
        client.print("\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print("Content-Length: ");
        client.print(body.length());
        client.print("\r\n");
        client.print("Connection: close\r\n\r\n");
        client.print(body);

        // Read and validate response
        bool chunked = false;
        int status = skipHeaders(client, chunked);

        if (status == 429) {
            readBodyStr(client, chunked);  // drain body
            client.stop();
            continue;  // retry
        }

        if (status != 200) {
            errorOut = "LLM HTTP " + String(status);
            client.stop();
            return false;
        }

        String respBody = readBodyStr(client, chunked);
        client.stop();

        JsonDocument filter;
        filter["choices"][0]["message"]["content"] = true;
        JsonDocument respDoc;
        DeserializationError err = deserializeJson(respDoc, respBody, DeserializationOption::Filter(filter));
        if (err) {
            errorOut = "LLM JSON err";
            return false;
        }

        String content = respDoc["choices"][0]["message"]["content"].as<String>();
        Serial.printf("[LLM] Response: %s\n", content.c_str());
        notFoodOut = !parseClassifyResponse(content, fodmapOut, glutenOut);
        return true;
    }

    errorOut = "Rate limited";
    return false;
#endif
}


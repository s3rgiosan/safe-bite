#include "audio_manager.h"
#include "M5StickCPlus2.h"
#include "wifi_manager.h"
#include "language.h"
#include <esp_heap_caps.h>

// Recording state
static AudioState currentAudioState = AUDIO_IDLE;
static uint8_t* wavBuffer = nullptr;
static size_t wavBufferSize = 0;
static size_t samplesRecorded = 0;
static unsigned long recordingStartTime = 0;

// Chunk recording
static const int SAMPLES_PER_CHUNK = 240;  // M5 mic recommended chunk size
static int16_t tempBuffer[SAMPLES_PER_CHUNK];

// UI state for blinking REC dot
static bool recDotVisible = true;
static unsigned long lastBlinkTime = 0;
static const unsigned long BLINK_INTERVAL = 500;  // 500ms toggle

// Tracking state for partial screen updates (to avoid flickering)
static int lastSecondsDisplayed = -1;
static bool lastRecDotState = false;

// Audio level bar visualizer
static const int NUM_BARS = 16;
static const int BAR_WIDTH = 12;
static const int BAR_GAP = 3;
static const int BAR_AREA_Y = 52;
static const int BAR_AREA_HEIGHT = 48;
static const int BAR_BASELINE = BAR_AREA_Y + BAR_AREA_HEIGHT;  // y=100
static uint8_t barLevels[NUM_BARS] = {0};
static unsigned long lastBarUpdateTime = 0;
static const unsigned long BAR_UPDATE_INTERVAL = 80;  // ms

// Forward declarations
static void writeWavHeader();
static void drawRecordingScreenInitial();
static void updateRecordingScreen(bool updateDot, bool updateSeconds, bool updateBars);

bool audioInit() {
    // Calculate total buffer size (WAV header + audio data)
    wavBufferSize = WAV_HEADER_SIZE + (AUDIO_BUFFER_SAMPLES * sizeof(int16_t));

    // Allocate buffer in PSRAM if available, otherwise in regular heap
    wavBuffer = (uint8_t*)heap_caps_malloc(wavBufferSize, MALLOC_CAP_8BIT);

    if (wavBuffer == nullptr) {
        currentAudioState = AUDIO_ERROR;
        return false;
    }

    // Clear buffer
    memset(wavBuffer, 0, wavBufferSize);

    currentAudioState = AUDIO_IDLE;
    return true;
}

static void writeWavHeader() {
    if (wavBuffer == nullptr) return;

    uint32_t dataSize = AUDIO_BUFFER_SAMPLES * sizeof(int16_t);
    uint32_t fileSize = dataSize + WAV_HEADER_SIZE - 8;
    uint32_t byteRate = AUDIO_SAMPLE_RATE * sizeof(int16_t);
    uint16_t blockAlign = sizeof(int16_t);

    // RIFF header
    wavBuffer[0] = 'R';
    wavBuffer[1] = 'I';
    wavBuffer[2] = 'F';
    wavBuffer[3] = 'F';

    // File size - 8
    wavBuffer[4] = (fileSize >> 0) & 0xFF;
    wavBuffer[5] = (fileSize >> 8) & 0xFF;
    wavBuffer[6] = (fileSize >> 16) & 0xFF;
    wavBuffer[7] = (fileSize >> 24) & 0xFF;

    // WAVE
    wavBuffer[8] = 'W';
    wavBuffer[9] = 'A';
    wavBuffer[10] = 'V';
    wavBuffer[11] = 'E';

    // fmt chunk
    wavBuffer[12] = 'f';
    wavBuffer[13] = 'm';
    wavBuffer[14] = 't';
    wavBuffer[15] = ' ';

    // fmt chunk size (16 for PCM)
    wavBuffer[16] = 16;
    wavBuffer[17] = 0;
    wavBuffer[18] = 0;
    wavBuffer[19] = 0;

    // Audio format (1 = PCM)
    wavBuffer[20] = 1;
    wavBuffer[21] = 0;

    // Channels (1 = mono)
    wavBuffer[22] = 1;
    wavBuffer[23] = 0;

    // Sample rate
    wavBuffer[24] = (AUDIO_SAMPLE_RATE >> 0) & 0xFF;
    wavBuffer[25] = (AUDIO_SAMPLE_RATE >> 8) & 0xFF;
    wavBuffer[26] = (AUDIO_SAMPLE_RATE >> 16) & 0xFF;
    wavBuffer[27] = (AUDIO_SAMPLE_RATE >> 24) & 0xFF;

    // Byte rate
    wavBuffer[28] = (byteRate >> 0) & 0xFF;
    wavBuffer[29] = (byteRate >> 8) & 0xFF;
    wavBuffer[30] = (byteRate >> 16) & 0xFF;
    wavBuffer[31] = (byteRate >> 24) & 0xFF;

    // Block align (2)
    wavBuffer[32] = blockAlign & 0xFF;
    wavBuffer[33] = (blockAlign >> 8) & 0xFF;

    // Bits per sample (16)
    wavBuffer[34] = 16;
    wavBuffer[35] = 0;

    // data chunk
    wavBuffer[36] = 'd';
    wavBuffer[37] = 'a';
    wavBuffer[38] = 't';
    wavBuffer[39] = 'a';

    // Data size
    wavBuffer[40] = (dataSize >> 0) & 0xFF;
    wavBuffer[41] = (dataSize >> 8) & 0xFF;
    wavBuffer[42] = (dataSize >> 16) & 0xFF;
    wavBuffer[43] = (dataSize >> 24) & 0xFF;
}

bool audioStartRecording() {
    if (wavBuffer == nullptr) {
        currentAudioState = AUDIO_ERROR;
        return false;
    }

    // Clear audio data portion
    memset(wavBuffer + WAV_HEADER_SIZE, 0, AUDIO_BUFFER_SAMPLES * sizeof(int16_t));

    // Write WAV header
    writeWavHeader();

    // Reset recording state
    samplesRecorded = 0;
    recordingStartTime = millis();
    recDotVisible = true;
    lastBlinkTime = millis();

    // Reset tracking state for partial updates
    lastSecondsDisplayed = -1;
    lastRecDotState = false;
    memset(barLevels, 0, sizeof(barLevels));
    lastBarUpdateTime = 0;

    // Start microphone
    auto mic_cfg = StickCP2.Mic.config();
    mic_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    mic_cfg.magnification = 32;  // Amplification (higher to compensate for 8kHz sample rate)
    StickCP2.Mic.config(mic_cfg);
    StickCP2.Mic.begin();

    currentAudioState = AUDIO_RECORDING;

    // Draw initial recording screen (full draw, only done once)
    drawRecordingScreenInitial();

    return true;
}

void audioUpdate() {
    if (currentAudioState != AUDIO_RECORDING) {
        return;
    }

    // Check if we've recorded enough samples
    if (samplesRecorded >= AUDIO_BUFFER_SAMPLES) {
        StickCP2.Mic.end();
        currentAudioState = AUDIO_COMPLETE;
        return;
    }

    // Calculate how many samples we can still record
    size_t remaining = AUDIO_BUFFER_SAMPLES - samplesRecorded;
    size_t toRecord = (remaining < SAMPLES_PER_CHUNK) ? remaining : SAMPLES_PER_CHUNK;

    // Record a chunk
    if (StickCP2.Mic.record(tempBuffer, toRecord, AUDIO_SAMPLE_RATE)) {
        // Copy to WAV buffer
        int16_t* audioData = (int16_t*)(wavBuffer + WAV_HEADER_SIZE);
        memcpy(&audioData[samplesRecorded], tempBuffer, toRecord * sizeof(int16_t));
        samplesRecorded += toRecord;
    }

    // Compute peak amplitude from this chunk
    int16_t peak = 0;
    for (size_t i = 0; i < toRecord; i++) {
        int16_t val = tempBuffer[i] < 0 ? -tempBuffer[i] : tempBuffer[i];
        if (val > peak) peak = val;
    }
    // Normalize to 0-255 range (int16 max is 32767)
    uint8_t level = (uint8_t)((peak * 255L) / 32767);

    // Update blinking state
    unsigned long now = millis();
    if (now - lastBlinkTime >= BLINK_INTERVAL) {
        recDotVisible = !recDotVisible;
        lastBlinkTime = now;
    }

    // Calculate what changed
    bool dotChanged = (recDotVisible != lastRecDotState);
    int seconds = (AUDIO_BUFFER_SAMPLES - samplesRecorded) / AUDIO_SAMPLE_RATE;
    if (seconds < 0) seconds = 0;
    if (seconds > AUDIO_DURATION_SEC) seconds = AUDIO_DURATION_SEC;
    bool secondsChanged = (seconds != lastSecondsDisplayed);

    // Check if bars need updating
    bool barsChanged = false;
    if (now - lastBarUpdateTime >= BAR_UPDATE_INTERVAL) {
        // Shift bars left, add new level on right
        for (int i = 0; i < NUM_BARS - 1; i++) {
            barLevels[i] = barLevels[i + 1];
        }
        barLevels[NUM_BARS - 1] = level;
        lastBarUpdateTime = now;
        barsChanged = true;
    }

    // Only update screen if something changed (prevents flickering)
    if (dotChanged || secondsChanged || barsChanged) {
        updateRecordingScreen(dotChanged, secondsChanged, barsChanged);
    }
}

bool isRecording() {
    return currentAudioState == AUDIO_RECORDING;
}

AudioState getAudioState() {
    return currentAudioState;
}

uint8_t* getWavBuffer() {
    return wavBuffer;
}

size_t getWavBufferSize() {
    return wavBufferSize;
}

void audioReset() {
    if (currentAudioState == AUDIO_RECORDING) {
        StickCP2.Mic.end();
    }
    samplesRecorded = 0;
    recDotVisible = true;
    currentAudioState = AUDIO_IDLE;
}

float getRecordingProgress() {
    if (currentAudioState != AUDIO_RECORDING) {
        return 0.0f;
    }
    return (float)samplesRecorded / (float)AUDIO_BUFFER_SAMPLES;
}

// Initial full screen draw - called once when recording starts
static void drawRecordingScreenInitial() {
    StickCP2.Display.fillScreen(TFT_BLACK);

    // REC indicator (red dot + text) - initial state
    StickCP2.Display.fillCircle(15, 12, 6, TFT_RED);
    StickCP2.Display.setTextColor(TFT_RED);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(25, 8);
    StickCP2.Display.print("REC");
    lastRecDotState = true;

    // WiFi indicator
    drawWifiIndicator();

    // Bar area is left empty on initial draw (all zeros, just black)

    // Initial seconds display
    int secondsRemaining = (AUDIO_BUFFER_SAMPLES - samplesRecorded) / AUDIO_SAMPLE_RATE;
    if (secondsRemaining < 0) secondsRemaining = 0;
    if (secondsRemaining > AUDIO_DURATION_SEC) secondsRemaining = AUDIO_DURATION_SEC;

    StickCP2.Display.setTextColor(TFT_YELLOW);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.setCursor(90, 108);
    StickCP2.Display.print(secondsRemaining);
    StickCP2.Display.print("s");
    lastSecondsDisplayed = secondsRemaining;

    // Cancel hint - static, drawn once
    StickCP2.Display.setTextColor(TFT_DARKGREY);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setCursor(5, 125);
    StickCP2.Display.print("B: Cancel");
}

// Partial screen update - only redraws changed elements (prevents flickering)
static void updateRecordingScreen(bool updateDot, bool updateSeconds, bool updateBars) {
    if (updateDot) {
        // Clear just the dot area (small rectangle around the circle)
        StickCP2.Display.fillRect(9, 6, 14, 14, TFT_BLACK);

        if (recDotVisible) {
            StickCP2.Display.fillCircle(15, 12, 6, TFT_RED);
            StickCP2.Display.setTextColor(TFT_RED);
        } else {
            StickCP2.Display.setTextColor(TFT_DARKGREY);
        }
        // Redraw "REC" text with updated color
        StickCP2.Display.setTextSize(1);
        StickCP2.Display.setCursor(25, 8);
        StickCP2.Display.print("REC");

        lastRecDotState = recDotVisible;
    }

    if (updateBars) {
        for (int i = 0; i < NUM_BARS; i++) {
            int x = i * (BAR_WIDTH + BAR_GAP);
            int barHeight = (barLevels[i] * BAR_AREA_HEIGHT) / 255;
            // Clear this bar's column
            StickCP2.Display.fillRect(x, BAR_AREA_Y, BAR_WIDTH, BAR_AREA_HEIGHT, TFT_BLACK);
            // Draw the bar (from baseline upward)
            if (barHeight > 0) {
                StickCP2.Display.fillRect(x, BAR_BASELINE - barHeight, BAR_WIDTH, barHeight, TFT_GREEN);
            }
        }
    }

    if (updateSeconds) {
        // Clear just the seconds area (enough space for "6s" in size 2 font)
        StickCP2.Display.fillRect(90, 108, 50, 16, TFT_BLACK);

        int seconds = (AUDIO_BUFFER_SAMPLES - samplesRecorded) / AUDIO_SAMPLE_RATE;
        if (seconds < 0) seconds = 0;
        if (seconds > AUDIO_DURATION_SEC) seconds = AUDIO_DURATION_SEC;

        StickCP2.Display.setTextColor(TFT_YELLOW);
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(90, 108);
        StickCP2.Display.print(seconds);
        StickCP2.Display.print("s");

        lastSecondsDisplayed = seconds;
    }
}

// Public wrapper for compatibility
void drawRecordingScreen() {
    drawRecordingScreenInitial();
}

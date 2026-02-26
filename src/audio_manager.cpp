#include "audio_manager.h"
#include <M5Unified.h>
#include <LittleFS.h>
#include "language.h"
#include <esp_heap_caps.h>
#include "fonts/DejaVuSans6pt_Latin.h"
#include "fonts/DejaVuSans8pt_Latin.h"
#include "fonts/DejaVuSans9pt_Latin.h"

// DejaVu proportional font aliases (custom: ASCII + Latin-1, U+0020–U+00FF)
#define FONT_SMALL  &DejaVuSans6pt8b
#define FONT_HEADER &DejaVuSans8pt8b
#define FONT_MEDIUM &DejaVuSans9pt8b

// Recording state
static AudioState currentAudioState = AUDIO_IDLE;
static int16_t* chunkBuffer = nullptr;  // 32KB total, split into two 16KB halves
static File wavFile;
static size_t totalSamplesWritten = 0;
static unsigned long chunkStartTime = 0;
static size_t wavFileSize = 0;
static const char* WAV_FILE_PATH = "/tmp.wav";

static size_t samplesRecorded = 0;  // total progress across all chunks
static unsigned long recordingStartTime = 0;

// Double-buffer: ping-pong two halves so DMA and flash write overlap
static const size_t HALF_SAMPLES = AUDIO_CHUNK_SAMPLES / 2;   // 8000 (0.5s)
static const size_t HALF_SIZE    = HALF_SAMPLES * sizeof(int16_t);  // 16000 bytes
static const int    TOTAL_HALVES = AUDIO_TOTAL_CHUNKS * 2;    // 10
static int16_t* recBuf  = nullptr;  // half currently being recorded into
static int16_t* writeBuf = nullptr; // half just completed, pending flash write
static int halvesCompleted = 0;

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

// Audio level: number of samples to scan for peak (~30ms window)
static const size_t LEVEL_WINDOW = AUDIO_SAMPLE_RATE / 33;

// Forward declarations
static void writeWavHeader(size_t dataSize);
static void drawRecordingScreenInitial();
static void updateRecordingScreen(bool updateDot, bool updateSeconds, bool updateBars);

bool audioInit() {
    currentAudioState = AUDIO_IDLE;
    return true;
}

static void writeWavHeader(size_t dataSize) {
    if (!wavFile) return;

    uint8_t header[WAV_HEADER_SIZE];
    uint32_t fileSize = dataSize + WAV_HEADER_SIZE - 8;
    uint32_t byteRate = AUDIO_SAMPLE_RATE * sizeof(int16_t);
    uint16_t blockAlign = sizeof(int16_t);

    // RIFF header
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = (fileSize >> 0) & 0xFF;
    header[5] = (fileSize >> 8) & 0xFF;
    header[6] = (fileSize >> 16) & 0xFF;
    header[7] = (fileSize >> 24) & 0xFF;

    // WAVE
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmt chunk
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;  // chunk size
    header[20] = 1; header[21] = 0;  // PCM format
    header[22] = 1; header[23] = 0;  // mono

    // Sample rate
    header[24] = (AUDIO_SAMPLE_RATE >> 0) & 0xFF;
    header[25] = (AUDIO_SAMPLE_RATE >> 8) & 0xFF;
    header[26] = (AUDIO_SAMPLE_RATE >> 16) & 0xFF;
    header[27] = (AUDIO_SAMPLE_RATE >> 24) & 0xFF;

    // Byte rate
    header[28] = (byteRate >> 0) & 0xFF;
    header[29] = (byteRate >> 8) & 0xFF;
    header[30] = (byteRate >> 16) & 0xFF;
    header[31] = (byteRate >> 24) & 0xFF;

    // Block align
    header[32] = blockAlign & 0xFF;
    header[33] = (blockAlign >> 8) & 0xFF;

    // Bits per sample
    header[34] = 16; header[35] = 0;

    // data chunk
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = (dataSize >> 0) & 0xFF;
    header[41] = (dataSize >> 8) & 0xFF;
    header[42] = (dataSize >> 16) & 0xFF;
    header[43] = (dataSize >> 24) & 0xFF;

    wavFile.seek(0);
    wavFile.write(header, WAV_HEADER_SIZE);
}

bool audioStartRecording() {
    // Allocate chunk buffer (32KB)
    if (chunkBuffer == nullptr) {
        Serial.printf("[AUDIO] Requesting chunk buffer %u bytes, free heap=%u, largest block=%u\n",
                      (unsigned)AUDIO_CHUNK_BUFFER_SIZE, ESP.getFreeHeap(),
                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        chunkBuffer = (int16_t*)heap_caps_malloc(AUDIO_CHUNK_BUFFER_SIZE, MALLOC_CAP_8BIT);
        if (chunkBuffer == nullptr) {
            Serial.println("[AUDIO] Chunk buffer allocation FAILED");
            currentAudioState = AUDIO_ERROR;
            return false;
        }
        Serial.printf("[AUDIO] Chunk buffer allocated at %p\n", chunkBuffer);
    }

    // Open WAV file for writing
    wavFile = LittleFS.open(WAV_FILE_PATH, "w");
    if (!wavFile) {
        Serial.println("[AUDIO] Failed to open tmp.wav for writing");
        currentAudioState = AUDIO_ERROR;
        return false;
    }

    // Write placeholder WAV header (will be rewritten at end with actual size)
    size_t maxDataSize = AUDIO_TOTAL_SAMPLES * sizeof(int16_t);
    writeWavHeader(maxDataSize);

    // Reset recording state
    totalSamplesWritten = 0;
    halvesCompleted = 0;
    samplesRecorded = 0;
    recordingStartTime = millis();
    recDotVisible = true;
    lastBlinkTime = millis();

    // Reset tracking state for partial updates
    lastSecondsDisplayed = -1;
    lastRecDotState = false;
    memset(barLevels, 0, sizeof(barLevels));
    lastBarUpdateTime = 0;

    // Configure and start microphone
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = AUDIO_SAMPLE_RATE;
    mic_cfg.magnification = 16;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();

    // Set up double-buffer halves and start first DMA
    int16_t* bufA = chunkBuffer;
    recBuf = bufA;
    writeBuf = nullptr;
    memset(chunkBuffer, 0, AUDIO_CHUNK_BUFFER_SIZE);
    M5.Mic.record(recBuf, HALF_SAMPLES, AUDIO_SAMPLE_RATE);
    chunkStartTime = millis();

    currentAudioState = AUDIO_RECORDING;

    // Draw initial recording screen (full draw, only done once)
    drawRecordingScreenInitial();

    return true;
}

void audioUpdate() {
    if (currentAudioState != AUDIO_RECORDING) {
        return;
    }

    // Compute audio level from current buffer (for visualizer)
    int16_t peak = 0;
    uint8_t level = 0;

    if (!M5.Mic.isRecording()) {
        // Half-buffer DMA complete — swap buffers immediately to minimize gap
        int16_t* completedBuf = recBuf;
        int16_t* bufA = chunkBuffer;
        int16_t* bufB = chunkBuffer + HALF_SAMPLES;
        recBuf = (completedBuf == bufA) ? bufB : bufA;

        // Start DMA on the other half right away (continuous recording)
        if (halvesCompleted + 1 < TOTAL_HALVES) {
            M5.Mic.record(recBuf, HALF_SAMPLES, AUDIO_SAMPLE_RATE);
            chunkStartTime = millis();
        }

        // Compute level from the completed half
        for (size_t i = (HALF_SAMPLES > LEVEL_WINDOW ? HALF_SAMPLES - LEVEL_WINDOW : 0);
             i < HALF_SAMPLES; i++) {
            int16_t val = completedBuf[i] < 0 ? -completedBuf[i] : completedBuf[i];
            if (val > peak) peak = val;
        }
        level = (uint8_t)((peak * 255L) / 32767);

        // Write completed half to flash (blocking ~20-50ms, but DMA runs in parallel)
        wavFile.write((uint8_t*)completedBuf, HALF_SIZE);
        totalSamplesWritten += HALF_SAMPLES;
        halvesCompleted++;

        Serial.printf("[AUDIO] Half %d/%d written\n", halvesCompleted, TOTAL_HALVES);

        samplesRecorded = totalSamplesWritten;

        if (halvesCompleted >= TOTAL_HALVES) {
            // All halves done — finalize WAV
            M5.Mic.end();
            size_t actualDataSize = totalSamplesWritten * sizeof(int16_t);
            writeWavHeader(actualDataSize);
            wavFileSize = WAV_HEADER_SIZE + actualDataSize;
            wavFile.close();
            Serial.printf("[AUDIO] Recording complete: %u samples, WAV file %u bytes\n",
                          (unsigned)totalSamplesWritten, (unsigned)wavFileSize);
            currentAudioState = AUDIO_COMPLETE;
            return;
        }

    } else {
        // DMA in progress — estimate progress within current half from elapsed time
        unsigned long chunkElapsed = millis() - chunkStartTime;
        size_t chunkProgress = (size_t)(chunkElapsed * AUDIO_SAMPLE_RATE / 1000);
        if (chunkProgress > HALF_SAMPLES) chunkProgress = HALF_SAMPLES;
        samplesRecorded = totalSamplesWritten + chunkProgress;

        // Read audio level from in-progress buffer
        if (chunkProgress > 0) {
            size_t start = (chunkProgress > LEVEL_WINDOW) ? chunkProgress - LEVEL_WINDOW : 0;
            for (size_t i = start; i < chunkProgress; i++) {
                int16_t val = recBuf[i] < 0 ? -recBuf[i] : recBuf[i];
                if (val > peak) peak = val;
            }
            level = (uint8_t)((peak * 255L) / 32767);
        }
    }

    // Update blinking state
    unsigned long now = millis();
    if (now - lastBlinkTime >= BLINK_INTERVAL) {
        recDotVisible = !recDotVisible;
        lastBlinkTime = now;
    }

    // Calculate what changed
    bool dotChanged = (recDotVisible != lastRecDotState);
    int seconds = (AUDIO_TOTAL_SAMPLES - samplesRecorded) / AUDIO_SAMPLE_RATE;
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

size_t getWavFileSize() {
    return wavFileSize;
}

const char* getWavFilePath() {
    return WAV_FILE_PATH;
}

void audioStopRecording() {
    if (currentAudioState != AUDIO_RECORDING) return;
    M5.Mic.end();

    // Estimate partial samples in the active half-buffer from elapsed time
    unsigned long chunkElapsed = millis() - chunkStartTime;
    size_t partialSamples = (size_t)(chunkElapsed * AUDIO_SAMPLE_RATE / 1000);
    if (partialSamples > HALF_SAMPLES) partialSamples = HALF_SAMPLES;

    // Write partial half-buffer to flash
    if (partialSamples > 0) {
        size_t partialBytes = partialSamples * sizeof(int16_t);
        wavFile.write((uint8_t*)recBuf, partialBytes);
        totalSamplesWritten += partialSamples;
    }

    // Rewrite WAV header with actual total size
    size_t actualDataSize = totalSamplesWritten * sizeof(int16_t);
    writeWavHeader(actualDataSize);
    wavFileSize = WAV_HEADER_SIZE + actualDataSize;
    wavFile.close();

    Serial.printf("[AUDIO] Early stop: %u samples, WAV file %u bytes\n",
                  (unsigned)totalSamplesWritten, (unsigned)wavFileSize);

    currentAudioState = AUDIO_COMPLETE;
}

void audioReset() {
    if (currentAudioState == AUDIO_RECORDING) {
        M5.Mic.end();
    }
    if (wavFile) {
        wavFile.close();
    }
    totalSamplesWritten = 0;
    halvesCompleted = 0;
    samplesRecorded = 0;
    wavFileSize = 0;
    recBuf = nullptr;
    writeBuf = nullptr;
    recDotVisible = true;
    currentAudioState = AUDIO_IDLE;
}

void audioFreeBuffer() {
    if (chunkBuffer != nullptr) {
        heap_caps_free(chunkBuffer);
        chunkBuffer = nullptr;
    }
    if (wavFile) {
        wavFile.close();
    }
}

float getRecordingProgress() {
    if (currentAudioState != AUDIO_RECORDING) {
        return 0.0f;
    }
    return (float)samplesRecorded / (float)AUDIO_TOTAL_SAMPLES;
}

// Initial full screen draw - called once when recording starts
static void drawRecordingScreenInitial() {
    M5.Display.fillScreen(TFT_BLACK);

    // REC indicator (red dot + text) - initial state
    M5.Display.fillCircle(15, 12, 6, TFT_RED);
    M5.Display.setTextColor(TFT_RED);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(25, 5);
    M5.Display.print("REC");
    lastRecDotState = true;

    // Bar area is left empty on initial draw (all zeros, just black)

    // Initial seconds display (top-right, aligned with REC row)
    int secondsRemaining = AUDIO_DURATION_SEC;

    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(220, 5);
    M5.Display.print(secondsRemaining);
    M5.Display.print("s");
    lastSecondsDisplayed = secondsRemaining;

    // Hints - static, drawn once
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setFont(FONT_SMALL);
    M5.Display.setCursor(5, 120);
    M5.Display.print("M5:Send  B:Cancel");
}

// Partial screen update - only redraws changed elements (prevents flickering)
static void updateRecordingScreen(bool updateDot, bool updateSeconds, bool updateBars) {
    if (updateDot) {
        // Clear dot + REC text area
        M5.Display.fillRect(9, 5, 42, 14, TFT_BLACK);

        if (recDotVisible) {
            M5.Display.fillCircle(15, 12, 6, TFT_RED);
            M5.Display.setTextColor(TFT_RED);
        } else {
            M5.Display.setTextColor(TFT_DARKGREY);
        }
        // Redraw "REC" text with updated color
        M5.Display.setFont(FONT_SMALL);
        M5.Display.setCursor(25, 5);
        M5.Display.print("REC");

        lastRecDotState = recDotVisible;
    }

    if (updateBars) {
        for (int i = 0; i < NUM_BARS; i++) {
            int x = i * (BAR_WIDTH + BAR_GAP);
            int barHeight = (barLevels[i] * BAR_AREA_HEIGHT) / 255;
            // Clear this bar's column
            M5.Display.fillRect(x, BAR_AREA_Y, BAR_WIDTH, BAR_AREA_HEIGHT, TFT_BLACK);
            // Draw the bar (from baseline upward)
            if (barHeight > 0) {
                M5.Display.fillRect(x, BAR_BASELINE - barHeight, BAR_WIDTH, barHeight, TFT_GREEN);
            }
        }
    }

    if (updateSeconds) {
        // Clear just the seconds area (top-right corner)
        M5.Display.fillRect(215, 4, 25, 14, TFT_BLACK);

        int seconds = (AUDIO_TOTAL_SAMPLES - samplesRecorded) / AUDIO_SAMPLE_RATE;
        if (seconds < 0) seconds = 0;
        if (seconds > AUDIO_DURATION_SEC) seconds = AUDIO_DURATION_SEC;

        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.setFont(FONT_SMALL);
        M5.Display.setCursor(220, 5);
        M5.Display.print(seconds);
        M5.Display.print("s");

        lastSecondsDisplayed = seconds;
    }
}

// Public wrapper for compatibility
void drawRecordingScreen() {
    drawRecordingScreenInitial();
}

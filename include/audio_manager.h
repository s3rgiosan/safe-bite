#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <stdint.h>
#include <stddef.h>

// Recording states
enum AudioState {
    AUDIO_IDLE,
    AUDIO_RECORDING,
    AUDIO_COMPLETE,
    AUDIO_ERROR
};

// Recording configuration
#define AUDIO_SAMPLE_RATE     16000
#define AUDIO_DURATION_MS     5000
#define AUDIO_DURATION_SEC    (AUDIO_DURATION_MS / 1000)
#define WAV_HEADER_SIZE       44

// Chunk-based streaming to flash
#define AUDIO_CHUNK_DURATION_MS  1000
#define AUDIO_CHUNK_SAMPLES      (AUDIO_SAMPLE_RATE * AUDIO_CHUNK_DURATION_MS / 1000)  // 16000
#define AUDIO_CHUNK_BUFFER_SIZE  (AUDIO_CHUNK_SAMPLES * sizeof(int16_t))               // 32000
#define AUDIO_TOTAL_CHUNKS       (AUDIO_DURATION_MS / AUDIO_CHUNK_DURATION_MS)          // 5
#define AUDIO_TOTAL_SAMPLES      (AUDIO_SAMPLE_RATE * AUDIO_DURATION_MS / 1000)         // 80000

// Function declarations
bool audioInit();
bool audioStartRecording();
void audioUpdate();
bool isRecording();
AudioState getAudioState();
void audioStopRecording();
void audioReset();
void audioFreeBuffer();
float getRecordingProgress();  // 0.0 to 1.0
void drawRecordingScreen();

// WAV file on flash (written incrementally during recording)
size_t getWavFileSize();
const char* getWavFilePath();

#endif

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
#define AUDIO_SAMPLE_RATE     8000
#define AUDIO_DURATION_MS     6000
#define AUDIO_DURATION_SEC    (AUDIO_DURATION_MS / 1000)
#define AUDIO_BUFFER_SAMPLES  (AUDIO_SAMPLE_RATE * AUDIO_DURATION_MS / 1000)
#define WAV_HEADER_SIZE       44

// Function declarations
bool audioInit();
bool audioStartRecording();
void audioUpdate();
bool isRecording();
AudioState getAudioState();
uint8_t* getWavBuffer();
size_t getWavBufferSize();
void audioReset();
float getRecordingProgress();  // 0.0 to 1.0
void drawRecordingScreen();

#endif

#include "wifi_manager.h"
#include "M5StickCPlus2.h"
#include <WiFi.h>

// Try to include config.h - if it doesn't exist, WiFi will be disabled
#if __has_include("config.h")
    #include "config.h"
#endif

// Check if WiFi credentials are actually defined (not just if file exists)
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    #define HAS_WIFI_CONFIG
#endif

// Module state
static WifiState currentWifiState = WIFI_STATE_IDLE;
static ConnectionMode connectionMode = MODE_OFFLINE;
static unsigned long connectionStartTime = 0;
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastBlinkTime = 0;
static bool indicatorVisible = true;
static WifiState lastDrawnState = WIFI_STATE_IDLE;

// Forward declaration for event handler
static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

#ifdef HAS_WIFI_CONFIG
static void startConnection() {
    currentWifiState = WIFI_STATE_CONNECTING;
    connectionStartTime = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
#endif

void wifiInit() {
#ifdef HAS_WIFI_CONFIG
    // WiFi credentials available
    connectionMode = MODE_ONLINE;

    // Set WiFi mode
    WiFi.mode(WIFI_STA);

    // Register event handler
    WiFi.onEvent(onWifiEvent);

    // Start non-blocking connection
    startConnection();
#else
    // No config.h or no credentials - stay in offline mode
    connectionMode = MODE_OFFLINE;
    currentWifiState = WIFI_STATE_OFF;
#endif
}

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            currentWifiState = WIFI_STATE_CONNECTED;
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (currentWifiState != WIFI_STATE_OFF) {
                currentWifiState = WIFI_STATE_DISCONNECTED;
                lastReconnectAttempt = millis();
            }
            break;

        default:
            break;
    }
}

void wifiUpdate() {
    if (connectionMode == MODE_OFFLINE || currentWifiState == WIFI_STATE_OFF) {
        return;
    }

#ifdef HAS_WIFI_CONFIG
    unsigned long now = millis();

    switch (currentWifiState) {
        case WIFI_STATE_CONNECTING:
            // Check for connection timeout
            if (now - connectionStartTime > WIFI_CONNECTION_TIMEOUT) {
                currentWifiState = WIFI_STATE_DISCONNECTED;
                lastReconnectAttempt = now;
                WiFi.disconnect();
            }
            break;

        case WIFI_STATE_DISCONNECTED:
            // Attempt reconnection after interval
            if (now - lastReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
                startConnection();
            }
            break;

        case WIFI_STATE_CONNECTED:
            // Verify still connected
            if (WiFi.status() != WL_CONNECTED) {
                currentWifiState = WIFI_STATE_DISCONNECTED;
                lastReconnectAttempt = now;
            }
            break;

        default:
            break;
    }

    // Update blink state for connecting indicator
    if (currentWifiState == WIFI_STATE_CONNECTING) {
        if (now - lastBlinkTime > WIFI_INDICATOR_BLINK_RATE) {
            lastBlinkTime = now;
            indicatorVisible = !indicatorVisible;
        }
    } else {
        indicatorVisible = true;
    }
#endif
}

bool isOnline() {
    return currentWifiState == WIFI_STATE_CONNECTED;
}

void wifiDisable() {
    if (connectionMode == MODE_ONLINE) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        currentWifiState = WIFI_STATE_OFF;
    }
}

WifiState getWifiState() {
    return currentWifiState;
}

ConnectionMode getConnectionMode() {
    return connectionMode;
}

void drawWifiIndicator() {
    // Don't draw if WiFi is off or in offline mode
    if (connectionMode == MODE_OFFLINE || currentWifiState == WIFI_STATE_OFF) {
        return;
    }

    // Clear previous indicator
    StickCP2.Display.fillCircle(WIFI_INDICATOR_X, WIFI_INDICATOR_Y + WIFI_INDICATOR_RADIUS,
                                 WIFI_INDICATOR_RADIUS + 1, TFT_BLACK);

    switch (currentWifiState) {
        case WIFI_STATE_CONNECTED:
            // Green filled circle
            StickCP2.Display.fillCircle(WIFI_INDICATOR_X, WIFI_INDICATOR_Y + WIFI_INDICATOR_RADIUS,
                                        WIFI_INDICATOR_RADIUS, TFT_GREEN);
            break;

        case WIFI_STATE_CONNECTING:
            // Yellow blinking circle
            if (indicatorVisible) {
                StickCP2.Display.fillCircle(WIFI_INDICATOR_X, WIFI_INDICATOR_Y + WIFI_INDICATOR_RADIUS,
                                            WIFI_INDICATOR_RADIUS, TFT_YELLOW);
            }
            break;

        case WIFI_STATE_DISCONNECTED:
            // Red outline circle
            StickCP2.Display.drawCircle(WIFI_INDICATOR_X, WIFI_INDICATOR_Y + WIFI_INDICATOR_RADIUS,
                                        WIFI_INDICATOR_RADIUS, TFT_RED);
            break;

        default:
            break;
    }

    lastDrawnState = currentWifiState;
}

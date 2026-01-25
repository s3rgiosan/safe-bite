#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>

// WiFi connection states
enum WifiState {
    WIFI_STATE_IDLE,         // Not started
    WIFI_STATE_CONNECTING,   // Attempting connection
    WIFI_STATE_CONNECTED,    // Successfully connected
    WIFI_STATE_DISCONNECTED, // Lost connection
    WIFI_STATE_OFF           // WiFi disabled
};

// Connection mode
enum ConnectionMode {
    MODE_OFFLINE,  // No WiFi available (config.h missing or disabled)
    MODE_ONLINE    // WiFi enabled
};

// Timing constants
#define WIFI_CONNECTION_TIMEOUT  10000  // 10 seconds
#define WIFI_RECONNECT_INTERVAL  30000  // 30 seconds
#define WIFI_INDICATOR_BLINK_RATE  500  // 500ms

// Indicator position
#define WIFI_INDICATOR_X  225
#define WIFI_INDICATOR_Y  5
#define WIFI_INDICATOR_RADIUS  5

// Function declarations
void wifiInit();
void wifiUpdate();
bool isOnline();
void wifiDisable();
void drawWifiIndicator();

// State accessors
WifiState getWifiState();
ConnectionMode getConnectionMode();

#endif

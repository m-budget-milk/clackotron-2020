#include <Arduino.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "config.h"
#include "ct_logging.h"
#include "ct_webserver.h"
#include "ct_webserver_async.h"
#include "ct_preferences.h"
#include "ct_peripherals.h"
#include "ct_module.h"
#include "ct_rtc.h"
#include "ct_stationboard.h"

CTWebserver webserver;
CTWebServerAsync webserverAsync;
CTPreferences preferences;
CTPeripherals peripherals;
CTModule module;
CTRTC rtc;
CTStationboard stationboard;

// Boolean flag of whether or not WiFi was available at least once
bool wifiSetupComplete = false;

// List of addresses of connected modules
uint8_t moduleAddresses[MAX_CONNECTED_MODULES] = {0};

// Lists of names for weekdays (sunday is day 1)
char dayNamesShort[8][3] = {"  ", "SO", "MO", "DI", "MI", "DO", "FR", "SA"};
char dayNamesLong[8][4] = {"  ", "SON", "MON", "DIE", "MIT", "DON", "FRE", "SAM"};

// Timezone change rules
TimeChangeRule timeDefault = { "ZHD", Last, Sun, Mar, 2, 120 };
TimeChangeRule timeDst = { "ZHS", Last, Sun, Oct, 3, 60 };
Timezone timezone(timeDefault, timeDst);

// NTP server for syncing time
char ntpServer[50] = NTP_DEFAULT_SERVER;
NTPClient *ntpClient = nullptr;
WiFiUDP ntpUdp;

// WiFi manager to allow user to connect to WiFi
WiFiManager wifiManager;

// Internal cache of last loop duration
unsigned long lastLoopTime = 0;

// Internal cache of the number of loops the device has performed
unsigned long loopCount = 0;

// Internal cache of buttons pressed for loops
unsigned long buttonOnePressedFor = 0;
unsigned long buttonTwoPressedFor = 0;

// Internal cache of last module output for de-duplication
uint8_t lastModuleOutputs[MAX_CONNECTED_MODULES] = {' '};

// Internal cache for board positions
uint8_t boardPositions[MAX_CONNECTED_MODULES] = {0};
bool needsToLoadConfig = true;
String displayMode = "manual";
uint32_t randomShuffleIntervalSeconds = 60;
unsigned long lastRandomShuffleTime = 0;

// Mirror mode state
struct ModuleMirrorMapping {
    uint8_t addr;
    char field[24];
};
ModuleMirrorMapping mirrorMappings[MAX_CONNECTED_MODULES];
int mirrorMappingCount = 0;
String mirrorStationId = "";
String mirrorStationQuery = "";
String mirrorPlatform = "";
uint32_t mirrorRefreshIntervalSeconds = MIRROR_DEFAULT_REFRESH_SECONDS;
unsigned long lastMirrorFetchTime = 0;
bool mirrorEnabled = false;
bool mirrorDepartureWindowEnabled = false;
uint32_t mirrorDepartureWindowMinutes = 10;

// Internal cache whether or not WiFi is connected
bool wifiIsConnected = false;

void randomizeBoardPositions() {
    int boardCount = preferences.getBoardModuleCount();
    module.zeroAll(moduleAddresses, MAX_CONNECTED_MODULES);

    int randomized = 0;

    for (int i = 0; i < boardCount; i++) {
        uint8_t addr = preferences.getBoardModuleAddress(i);
        int randomPos = preferences.getRandomNonEmptyBoardPosition(addr);
        if (randomPos < 0) {
            boardPositions[i] = (uint8_t)preferences.getBoardDefaultPosition(addr);
            lastModuleOutputs[i] = 0xFF;
            continue;
        }

        boardPositions[i] = (uint8_t)randomPos;
        lastModuleOutputs[i] = 0xFF;
        randomized++;
    }

    if (randomized > 0) {
        CTLog::info("main: random mode generated positions for " + String(randomized) + " modules");
    }
}

void setup() {
    // Set up serial communication for debugging
    CTLog::setup();
    randomSeed(micros());

    // Set up hardware peripherals
    peripherals.setup();

    // Set up serial communication for rtc
    rtc.setup();

    // Set up serial communication for modules
    module.setup();

    // Set up our own preferences provider
    preferences.setup();

    // Load board module definitions from filesystem
    preferences.loadBoardModules();

    // Set LED to yellow while booting
    peripherals.setLEDColor(CTLedColor::Yellow);

    // Load module configuration from preferences provider
    preferences.loadModuleAddresses(moduleAddresses);

    // Load time zone and time server configuration
    preferences.loadTimeZoneData(&timeDefault, &timeDst, ntpServer, dayNamesShort, dayNamesLong);
    timezone.setRules(timeDefault, timeDst);

    // If this is the first boot, write WIFI to modules
    // If not, load time from RTC since we assume we've been connected once.
    if (preferences.isFirstBoot()) {
        CTLog::info("main: first boot detected, writing default positions to modules");
        int boardCount = preferences.getBoardModuleCount();
        for (int i = 0; i < boardCount; i++) {
            uint8_t addr = preferences.getBoardModuleAddress(i);
            module.writeRaw(addr, (uint8_t)preferences.getBoardDefaultPosition(addr));
        }
        needsToLoadConfig = false;
    } else {
        // Load time from RTC once in the beginning
        uint32_t timestamp = rtc.readTimeFromRTC();

        if (timestamp >= EARLIEST_VALID_TIMESTAMP) {
            CTLog::debug("main: loaded timestamp from rtc initially: " + String(timestamp));
            setTime(timestamp);
        } else {
            CTLog::error("main: invalid timestamp loaded from rtc: " + String(timestamp));
        }

        // Read module configuration from preferences on first loop
        needsToLoadConfig = true;
    }

    // Set up wifi manager to allow user to connect to wifi
    wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setWiFiAutoReconnect(true);
    wifiManager.autoConnect(WIFI_AP_NAME);
    wifiManager.setSaveConfigCallback([]() {
        CTLog::info("main: wifi manager save config callback called, restarting device");
        ESP.restart();
    });

    // Set up webserver to allow user to configure modules
    webserver.setup(&preferences, &module, moduleAddresses, &needsToLoadConfig);

    CTLog::info("main: clackotron2000 setup complete");
}

void loop() {
    unsigned long currentLoopTime = millis();

    // Certain things should only be done if WiFi is available
    if (WiFi.status() == WL_CONNECTED) {
        // Set LED to white once we're connected to WiFi
        if (!wifiIsConnected) {
            peripherals.setLEDColor(CTLedColor::White);
            wifiIsConnected = true;
        }

        // If Wifi was not available before, set flag and do initial setup
        if (!wifiSetupComplete) {
            CTLog::info("main: wifi connected for the first time this boot, running initial setup");

            // Set first boot flag to false after first ever connection to WiFi
            if (preferences.isFirstBoot()) {
                CTLog::info("main: connected to WiFi for the first time, setting first boot flag to false");
                preferences.setFirstBoot(false);
            }

            ntpClient = new NTPClient(ntpUdp, ntpServer, NTP_TIME_OFFSET, NTP_UPDATE_INTERVAL);
            CTLog::debug("main: started ntp client with update interval of " + String(NTP_UPDATE_INTERVAL) + "ms");

            wifiSetupComplete = true;
        }

        // Periodically sync time and store it to RTC
        if (ntpClient != nullptr) {
            bool didUpdateTime = ntpClient->update();
            if (didUpdateTime) {
                CTLog::debug("main: updated time from ntp server, writing to rtc");
                rtc.writeTimeToRTC(timezone.toLocal(ntpClient->getEpochTime()));
                setTime(timezone.toLocal(ntpClient->getEpochTime()));
            }
        }

        // Handle webserver client
        webserver.handle();
    } else {
        // Set LED to white once we're connected to WiFi
        if (wifiIsConnected) {
            peripherals.setLEDColor(CTLedColor::Yellow);
            wifiIsConnected = false;
        }
    }

    // Process WiFi manager
    wifiManager.process();

    // Template configuration and module update is only done every aprox. tick
    if (currentLoopTime - lastLoopTime > IDEAL_TICK_TIME) {
        lastLoopTime = currentLoopTime;

        if (needsToLoadConfig) {
            displayMode = preferences.getDisplayMode();
            randomShuffleIntervalSeconds = preferences.getRandomShuffleIntervalSeconds();

            int boardCount = preferences.getBoardModuleCount();
            if (displayMode != "mirror") {
                for (int i = 0; i < boardCount; i++) {
                    uint8_t addr = preferences.getBoardModuleAddress(i);
                    boardPositions[i] = (uint8_t)preferences.getBoardSavedPosition(addr);
                    lastModuleOutputs[i] = 0xFF; // Force update on next output cycle
                }
            }

            lastRandomShuffleTime = currentLoopTime;

            if (displayMode == "random") {
                randomizeBoardPositions();
            }

            // Load mirror configuration
            {
                String mirrorJson = preferences.getMirrorConfig();
                DynamicJsonDocument mDoc(4096);
                if (!deserializeJson(mDoc, mirrorJson)) {
                    mirrorEnabled = mDoc["enabled"] | false;
                    mirrorStationId    = mDoc["stationId"].as<String>();
                    mirrorStationQuery = mDoc["stationQuery"].as<String>();
                    mirrorPlatform     = mDoc["platform"].as<String>();
                    int interval = mDoc["refreshIntervalSeconds"] | MIRROR_DEFAULT_REFRESH_SECONDS;
                    if (interval < MIRROR_MIN_REFRESH_SECONDS) interval = MIRROR_MIN_REFRESH_SECONDS;
                    if (interval > MIRROR_MAX_REFRESH_SECONDS) interval = MIRROR_MAX_REFRESH_SECONDS;
                    mirrorRefreshIntervalSeconds = (uint32_t)interval;

                    mirrorMappingCount = 0;
                    if (mDoc.containsKey("mappings")) {
                        JsonObject mappings = mDoc["mappings"].as<JsonObject>();
                        for (JsonPair kv : mappings) {
                            if (mirrorMappingCount >= MAX_CONNECTED_MODULES) break;
                            mirrorMappings[mirrorMappingCount].addr = (uint8_t)String(kv.key().c_str()).toInt();
                            strncpy(mirrorMappings[mirrorMappingCount].field, kv.value().as<const char*>(), 23);
                            mirrorMappings[mirrorMappingCount].field[23] = '\0';
                            mirrorMappingCount++;
                        }
                    }

                    mirrorDepartureWindowEnabled = mDoc["departureWindowEnabled"] | false;
                    int windowMinutes = mDoc["departureWindowMinutes"] | 10;
                    if (windowMinutes < 1) windowMinutes = 1;
                    mirrorDepartureWindowMinutes = (uint32_t)windowMinutes;

                    // Force an immediate fetch after mirror settings are reloaded.
                    lastMirrorFetchTime = 0;
                    CTLog::info("main: loaded mirror config, enabled=" + String(mirrorEnabled) + " mappings=" + String(mirrorMappingCount));
                }
            }

            CTLog::info("main: loaded board positions");
            needsToLoadConfig = false;
        }

        if (displayMode == "random") {
            unsigned long intervalMs = randomShuffleIntervalSeconds * 1000UL;
            if (currentLoopTime - lastRandomShuffleTime >= intervalMs) {
                randomizeBoardPositions();
                lastRandomShuffleTime = currentLoopTime;
            }
        }

        // Mirror mode: periodically fetch stationboard and update mapped modules
        if (displayMode == "mirror" && mirrorEnabled && wifiIsConnected) {
            unsigned long mirrorIntervalMs = mirrorRefreshIntervalSeconds * 1000UL;
            if (mirrorMappingCount > 0 &&
                (lastMirrorFetchTime == 0 || currentLoopTime - lastMirrorFetchTime >= mirrorIntervalMs)) {

                lastMirrorFetchTime = currentLoopTime;
                StationboardEntry entry = stationboard.fetchNextDeparture(
                    mirrorStationId, mirrorStationQuery, mirrorPlatform
                );

                int boardCount = preferences.getBoardModuleCount();
                auto resetMappedAddrToDefault = [&](uint8_t addr) {
                    for (int i = 0; i < boardCount; i++) {
                        if (preferences.getBoardModuleAddress(i) != addr) continue;
                        boardPositions[i] = (uint8_t)preferences.getBoardDefaultPosition(addr);
                        lastModuleOutputs[i] = 0xFF;
                        break;
                    }
                };

                auto resetAllMappedToDefault = [&]() {
                    for (int m = 0; m < mirrorMappingCount; m++) {
                        resetMappedAddrToDefault(mirrorMappings[m].addr);
                    }
                };

                if (entry.valid) {
                    // Check if departure is within the configured window
                    bool withinWindow = true;
                    if (mirrorDepartureWindowEnabled) {
                        int nowMinutes = hour() * 60 + minute();
                        int depMinutes = entry.departureHour.toInt() * 60 + entry.departureMinute.toInt();
                        int diff = depMinutes - nowMinutes;
                        if (diff < 0) diff += 24 * 60; // handle midnight wraparound
                        withinWindow = (diff <= (int)mirrorDepartureWindowMinutes);
                        CTLog::debug("mirror: window check diff=" + String(diff) + "min limit=" + String(mirrorDepartureWindowMinutes) + " within=" + String(withinWindow));
                    }

                    if (!withinWindow) {
                        // No departure within window — reset all mapped modules to default position
                        resetAllMappedToDefault();
                        CTLog::info("mirror: departure outside window, reset to default positions");
                    } else {

                    for (int m = 0; m < mirrorMappingCount; m++) {
                        uint8_t addr = mirrorMappings[m].addr;
                        String fieldKey = String(mirrorMappings[m].field);

                        if (fieldKey == MIRROR_FIELD_NONE) continue;

                        int matchedPassListPos = -1;

                        // Resolve field value from entry
                        String value;
                        if      (fieldKey == MIRROR_FIELD_DESTINATION) {
                            String transformedDest = preferences.resolveMirrorDestinationOverride(entry.destination);
                            value = transformedDest.length() > 0 ? transformedDest : entry.destination;
                            CTLog::debug(
                                "mirror: destination=" + entry.destination +
                                " transformed=" + transformedDest +
                                " selected=" + value
                            );
                        }
                        else if (fieldKey == MIRROR_FIELD_DEPARTURE_HOUR)   value = entry.departureHour;
                        else if (fieldKey == MIRROR_FIELD_DEPARTURE_MINUTE) value = entry.departureMinute;
                        else if (fieldKey == MIRROR_FIELD_CATEGORY) {
                            String transformed = preferences.resolveMirrorTransformedValue(entry.category, entry.operatorCode);
                            value = transformed.length() > 0 ? transformed : entry.category;
                            CTLog::debug(
                                "mirror: category=" + entry.category +
                                " operator=" + entry.operatorCode +
                                " transformed=" + transformed +
                                " selected=" + value
                            );
                        }
                        else if (fieldKey == MIRROR_FIELD_NUMBER)           value = entry.number;
                        else if (fieldKey == MIRROR_FIELD_DELAY)            value = entry.delay;
                        else if (fieldKey == MIRROR_FIELD_PLATFORM)         value = entry.platform;
                        else if (fieldKey == MIRROR_FIELD_NEXT_STATIONS) {
                            for (int passIdx = 0; passIdx < entry.passListCount; passIdx++) {
                                int posIdx = preferences.findBoardPositionByLabel(addr, entry.passList[passIdx]);
                                if (posIdx >= 0) {
                                    value = entry.passList[passIdx];
                                    matchedPassListPos = posIdx;
                                    break;
                                }
                            }
                        }

                        // Find position index in the module's positions array
                        if (value.isEmpty()) {
                            resetMappedAddrToDefault(addr);
                            continue;
                        }

                        // Locate module index in boardPositions array
                        for (int i = 0; i < boardCount; i++) {
                            if (preferences.getBoardModuleAddress(i) != addr) continue;

                            int posIdx = matchedPassListPos >= 0
                                ? matchedPassListPos
                                : preferences.findBoardPositionByLabel(addr, value);
                            CTLog::debug(
                                "mirror: addr=" + String(addr) +
                                " field=" + fieldKey +
                                " value=" + value +
                                " pos=" + String(posIdx)
                            );
                            if (posIdx >= 0) {
                                boardPositions[i] = (uint8_t)posIdx;
                                lastModuleOutputs[i] = 0xFF; // force write
                            } else {
                                // No match found, reset to module default position
                                resetMappedAddrToDefault(addr);
                            }
                            break;
                        }
                    }
                    } // end withinWindow
                } else {
                    // No valid departure found for this fetch; reset mapped modules.
                    resetAllMappedToDefault();
                    CTLog::info("mirror: no valid departure, reset to default positions");
                }
            }
        }


        // Write board positions to modules (only when changed)
        int numChanged = 0;
        int boardCount = preferences.getBoardModuleCount();
        for (int i = 0; i < boardCount; i++) {
            uint8_t addr = preferences.getBoardModuleAddress(i);
            if (boardPositions[i] != lastModuleOutputs[i]) {
                module.writeRaw(addr, boardPositions[i]);
                lastModuleOutputs[i] = boardPositions[i];
                numChanged++;
            }
        }
        if (numChanged > 0) CTLog::info("main: wrote board positions, " + String(numChanged) + " changed");

        // Check button press states and increment loop counter
        if (peripherals.isButtonOnePressed()) buttonOnePressedFor++;
        if (peripherals.isButtonTwoPressed()) buttonTwoPressedFor++;
        
        // If button one was pressed for a while, allow additional functions
        if (IDEAL_TICK_TIME * buttonOnePressedFor > BUTTON_ONE_PRESS_TIME) {
            CTLog::info("main: button one pressed for " + String(buttonOnePressedFor) + " cycles, activating additional functions");
            peripherals.setLEDColor(CTLedColor::Blue);
            
            webserverAsync.setup(&module, moduleAddresses);

            buttonOnePressedFor = 0;
        }

        // If button two was pressed for a while, reset settings
        if (IDEAL_TICK_TIME * buttonTwoPressedFor > BUTTON_TWO_PRESS_TIME) {
            CTLog::info("main: button two pressed for " + String(buttonTwoPressedFor) + " cycles, resetting");
            peripherals.setLEDColor(CTLedColor::Red);

            preferences.clear();
            wifiManager.resetSettings();

            delay(2000);
            ESP.restart();

            buttonTwoPressedFor = 0;
        }

        if (IDEAL_TICK_TIME * loopCount > RTC_UNDRIFT_INTERVAL) {
            CTLog::info("main: correcting rtc drift by re-reading time from rtc");
            uint32_t timstamp = rtc.readTimeFromRTC();
            setTime(timstamp);

            loopCount = 0;
        }

        // Increment loop counter (currently only used to correct drift)
        loopCount++;
    }
}

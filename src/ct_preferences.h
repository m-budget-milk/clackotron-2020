#ifndef _CLACKOTRON_PREFERENCES_H_
#define _CLACKOTRON_PREFERENCES_H_

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Timezone.h>

/// @brief A class for managing preferences that are stored in flash or file system
class CTPreferences {
  public:
    /// @brief Set up preferences provider
    /// @return True if setup was successful, false otherwise
    bool setup();

    /// @brief Check if this is the first boot of the device
    /// @return True if this is the first boot, false otherwise
    bool isFirstBoot();

    /// @brief  Set the first boot flag
    /// @param firstBoot - The boolean value to set the first boot flag to
    void setFirstBoot(bool firstBoot);

    /// @brief Load the list of module addresses from the config
    /// @param moduleAddresses - The array to load the module addresses into
    void loadModuleAddresses(uint8_t *moduleAddresses);

    /// @brief Load the timezone data from the config
    /// @param timeDefault - Pointer to the TimeChangeRule for the default time
    /// @param timeDst - Pointer to the TimeChangeRule for the DST time
    /// @param ntpServer - Pointer to the NTP server address
    /// @param dayNamesShort - Pointer to the array of short day names 
    /// @param dayNamesLong - Pointer to the array of long day names
    void loadTimeZoneData(
      TimeChangeRule *timeDefault, 
      TimeChangeRule *timeDst, 
      char* ntpServer,
      char dayNamesShort[8][3],
      char dayNamesLong[8][4]
    );

    /// @brief Get the current config JSON object
    /// @return The current config JSON object as string
    String getConfig();

    /// @brief Get the the current config template string
    /// @return The config template as string
    String getConfigTemplate();

    /// @brief Get a text parameter from the current config
    /// @param key - The key of the parameter to get
    /// @return The text parameter as string
    String getConfigTextParam(String key);

    /// @brief Get a number parameter from the current config
    /// @param key - The key of the parameter to get
    /// @return The number parameter as uint32_t
    uint32_t getConfigNumberParam(String key);

    /// @brief Set the current config JSON object
    /// @param config - The config JSON object as string
    void setConfig(String config);

    /// @brief Load board module definitions from board_modules.json into memory
    /// @return True if loaded successfully
    bool loadBoardModules();

    /// @brief Get the number of board modules defined in board_modules.json
    /// @return Number of modules
    int getBoardModuleCount();

    /// @brief Get the RS485 address of board module at index i
    /// @param i - Index into the modules array
    /// @return RS485 address byte
    uint8_t getBoardModuleAddress(int i);

    /// @brief Check whether an address exists in board_modules.json
    /// @param addr - RS485 address to check
    /// @return True if address is configured
    bool isValidBoardAddress(uint8_t addr);

    /// @brief Check whether a position index is valid for the given address
    /// @param addr - RS485 address
    /// @param pos - Position index to validate
    /// @return True if position is within the positions array bounds
    bool isValidBoardPosition(uint8_t addr, int pos);

    /// @brief Get the default blade position for a board module
    /// @param addr - RS485 address
    /// @return defaultPosition from board_modules.json, or 0 if not set
    int getBoardDefaultPosition(uint8_t addr);

    /// @brief Get the currently saved blade position for a board module
    /// @param addr - RS485 address
    /// @return Saved position, or getBoardDefaultPosition if not saved
    int getBoardSavedPosition(uint8_t addr);

    /// @brief Persist a new set of board positions
    /// @param json - JSON object string keyed by address, e.g. {"7":14}
    void setBoardPositions(String json);

    /// @brief Build the full board positions response with fallback resolution
    /// @return JSON string e.g. {"modulePositions":{"7":14}}
    String getBoardPositionsJson();

    /// @brief Get current runtime board mode
    /// @return "manual" or "random"
    String getDisplayMode();

    /// @brief Persist runtime board mode
    /// @param mode - "manual" or "random"
    void setDisplayMode(String mode);

    /// @brief Get random reshuffle interval in seconds
    /// @return Interval in range 1..86400
    uint32_t getRandomShuffleIntervalSeconds();

    /// @brief Persist random reshuffle interval
    /// @param seconds - Interval in range 1..86400
    void setRandomShuffleIntervalSeconds(uint32_t seconds);

    /// @brief Get number of available positions for a board module
    /// @param addr - RS485 address
    /// @return Number of positions for the module or 0 if unknown
    int getBoardModulePositionCount(uint8_t addr);

    /// @brief Pick a random position index whose label is non-empty
    /// @param addr - RS485 address
    /// @return Position index, or -1 if no non-empty label exists
    int getRandomNonEmptyBoardPosition(uint8_t addr);

      /// @brief Find the best-matching position index for a value string by
      ///        scanning the module's position labels. Tries exact, then prefix
      ///        matches in both directions (see findPositionByLabel in
      ///        ct_stationboard.h for the matching algorithm).
      /// @param addr  - RS485 address of the module
      /// @param value - String to match against labels
      /// @return position index >= 0 on match, -1 if not found
      int findBoardPositionByLabel(uint8_t addr, const String& value);

    /// @brief Clear all preferences
    void clear();

    /// @brief Get the mirror configuration as JSON string
    /// @return JSON string with mirror config (defaults when not set)
    String getMirrorConfig();

    /// @brief Persist a new mirror configuration
    /// @param json - JSON string with mirror config fields
    void setMirrorConfig(String json);

    /// @brief Resolve optional stationboard transformations, e.g.
    ///        category=B + operator=PAG -> Postauto.
    /// @param category - stationboard category value
    /// @param operatorCode - stationboard operator value
    /// @return transformed display value, or empty string when no rule matches
    String resolveMirrorTransformedValue(const String& category, const String& operatorCode);

    /// @brief Resolve optional destination override transformations, e.g.
    ///        to="Wil SG" -> "Goeschenen".
    /// @param toValue - stationboard destination value from "to"
    /// @return transformed destination, or empty string when no rule matches
    String resolveMirrorDestinationOverride(const String& toValue);

  private:
    Preferences preferences;
    DynamicJsonDocument* boardModulesDoc = nullptr;

    void overrideTimeChangeRule(const char* tag, TimeChangeRule *rule, JsonArray data);
    void copyLittleFsFile(const char* src, const char* dst);
};

#endif // _CLACKOTRON_PREFERENCES_H_

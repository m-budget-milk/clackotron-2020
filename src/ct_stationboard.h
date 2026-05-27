#ifndef _CLACKOTRON_STATIONBOARD_H_
#define _CLACKOTRON_STATIONBOARD_H_

#include <Arduino.h>
#include <ArduinoJson.h>

// Names of API field keys that can be mapped to a module
#define MIRROR_FIELD_NONE             "none"
#define MIRROR_FIELD_DESTINATION      "destination"
#define MIRROR_FIELD_DEPARTURE_HOUR   "departure_hour"
#define MIRROR_FIELD_DEPARTURE_MINUTE "departure_minute"
#define MIRROR_FIELD_CATEGORY         "category"
#define MIRROR_FIELD_NUMBER           "number"
#define MIRROR_FIELD_DELAY            "delay"
#define MIRROR_FIELD_PLATFORM         "platform"
#define MIRROR_FIELD_NEXT_STATIONS    "next_stations"

#define STATIONBOARD_MAX_PASSLIST_ENTRIES 12

/// @brief Normalized stationboard departure entry
struct StationboardEntry {
    String destination;      // e.g. "Disentis/Mustér"
    String departureHour;    // e.g. "10"
    String departureMinute;  // e.g. "45"
    String category;         // e.g. "R"
    String number;           // e.g. "310"
    String delay;            // e.g. "+5" or "0"
    String platform;         // e.g. "4"
    String passList[STATIONBOARD_MAX_PASSLIST_ENTRIES];
    int passListCount;       // number of populated passList station names
    bool valid;              // false when no matching departure was found
    String operatorCode;     // e.g. "PAG" for Postauto buses
};

/// @brief Station search result
struct StationResult {
    String id;
    String name;
};

/// @brief Client for the opendata.ch transport API (stationboard + location search)
class CTStationboard {
  public:
    /// @brief Fetch the next departure from a platform and return normalized fields.
    ///        Prefers stationId over stationQuery when both are non-empty.
    /// @param stationId    opendata.ch station ID, e.g. "8509070"
    /// @param stationQuery Human-readable station name, used as fallback
    /// @param platform     Platform/track string to filter on, e.g. "4"
    /// @return Normalized StationboardEntry; check .valid before using
    StationboardEntry fetchNextDeparture(
        const String& stationId,
        const String& stationQuery,
        const String& platform
    );

    /// @brief Search for stations by name. Returns up to 10 results.
    /// @param query Human-readable search string, e.g. "Andermatt"
    /// @param outResults Array to fill with results
    /// @param maxResults  Maximum number of results to populate
    /// @return Number of results written to outResults
    int searchStations(
        const String& query,
        StationResult* outResults,
        int maxResults
    );

  private:
    /// @brief Return the string value for a given fieldKey from an entry
    String resolveField(const StationboardEntry& entry, const String& fieldKey);

    /// @brief Parse ISO8601-ish departure string ("2026-04-08T10:45:00+0200")
    ///        and extract zero-padded hour and minute strings
    void parseDepartureTime(const char* ts, String& hour, String& minute);
};

/// @brief Find the best matching position index for a given string value in a
///        module's position labels array.  Returns -1 when no match found.
/// @param positions  ArduinoJson array of position objects {"label": "..."}
/// @param value      String to match against labels
int findPositionByLabel(JsonArray positions, const String& value);

#endif // _CLACKOTRON_STATIONBOARD_H_

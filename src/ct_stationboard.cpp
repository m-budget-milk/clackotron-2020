#include "ct_stationboard.h"
#include "ct_logging.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static String urlEncode(const String& input) {
    String out;
    out.reserve(input.length() * 3);

    for (size_t i = 0; i < input.length(); i++) {
        const uint8_t c = static_cast<uint8_t>(input[i]);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Blocking stream wrapper
// WiFiClientSecure::read() returns -1 immediately when no data is buffered
// (between TLS records). ArduinoJson interprets -1 as end-of-stream and stops
// parsing, producing an empty result even though more data is on the way.
// This wrapper waits up to STATIONBOARD_HTTP_TIMEOUT_MS for the next byte.
// ---------------------------------------------------------------------------

class BlockingStream : public Stream {
public:
    explicit BlockingStream(WiFiClient* c) : _c(c) {}

    int available() override { return _c->available(); }
    int peek()      override { return _c->peek(); }
    size_t write(uint8_t) override { return 0; }

    int read() override {
        unsigned long start = millis();
        while (!_c->available()) {
            if (millis() - start > STATIONBOARD_HTTP_TIMEOUT_MS) return -1;
            delay(1);
        }
        return _c->read();
    }

private:
    WiFiClient* _c;
};

void CTStationboard::parseDepartureTime(const char* ts, String& hour, String& minute) {
    // Expected format: "2026-04-08T10:45:00+0200" or "2026-04-08T10:45:00+02:00"
    // Find the 'T' separator; everything after is the time part.
    hour = "";
    minute = "";
    if (ts == nullptr || strlen(ts) < 16) return;

    const char* t = strchr(ts, 'T');
    if (t == nullptr) return;
    t++; // skip 'T'

    // Extract hh and mm
    char hh[3] = {t[0], t[1], '\0'};
    char mm[3] = {t[3], t[4], '\0'};

    // Normalize zero-padded values (e.g. "08" -> "8") so label matching
    // works with modules that store numeric labels without leading zeros.
    hour   = String(atoi(hh));
    minute = String(atoi(mm));
}

// ---------------------------------------------------------------------------
// Position matching (also used from main.cpp via the free function below)
// ---------------------------------------------------------------------------

int findPositionByLabel(JsonArray positions, const String& value) {
    if (value.isEmpty()) return -1;

    // Pass 1: case-insensitive exact match
    for (int i = 0; i < (int)positions.size(); i++) {
        String label = positions[i]["label"].as<String>();
        if (label.equalsIgnoreCase(value)) return i;
    }

    // Pass 2: value is a prefix of label  (e.g. value="10" matches label="10.")
    for (int i = 0; i < (int)positions.size(); i++) {
        String label = positions[i]["label"].as<String>();
        if (label.length() > 0 && label.length() >= value.length()) {
            if (label.substring(0, value.length()).equalsIgnoreCase(value)) return i;
        }
    }

    // Pass 3: label is a prefix of value  (e.g. label="Disentis" matches value="Disentis/Mustér")
    for (int i = 0; i < (int)positions.size(); i++) {
        String label = positions[i]["label"].as<String>();
        if (label.length() > 0 && value.length() >= label.length()) {
            if (value.substring(0, label.length()).equalsIgnoreCase(label)) return i;
        }
    }

    return -1;
}

// ---------------------------------------------------------------------------
// Stationboard fetch
// ---------------------------------------------------------------------------

StationboardEntry CTStationboard::fetchNextDeparture(
    const String& stationId,
    const String& stationQuery,
    const String& platform
) {
    StationboardEntry result;
    result.valid = false;
    result.passListCount = 0;

    String stationParam = stationId.length() > 0 ? stationId : stationQuery;
    if (stationParam.isEmpty()) {
        CTLog::error("stationboard: no station id or query configured");
        return result;
    }

    // Build URL: use id= for numeric IDs, station= for names
    bool isNumericId = stationId.length() > 0;
    String url = String(STATIONBOARD_API_BASE) + "/stationboard?limit=20&";
    if (isNumericId) {
        url += "id=" + urlEncode(stationId);
    } else {
        url += "station=" + urlEncode(stationQuery);
    }

    CTLog::info("stationboard: fetching " + url);

    WiFiClientSecure client;
    client.setInsecure(); // opendata.ch uses a well-known CA; no sensitive data in transit

    HTTPClient http;
    http.setTimeout(STATIONBOARD_HTTP_TIMEOUT_MS);
    if (!http.begin(client, url)) {
        CTLog::error("stationboard: http.begin failed");
        return result;
    }
    http.useHTTP10(true);
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    int httpCode = http.GET();
    if (httpCode != 200) {
        CTLog::error("stationboard: HTTP " + String(httpCode));
        http.end();
        return result;
    }

    int expectedLen = http.getSize();
    CTLog::debug("stationboard: response size=" + String(expectedLen));

    BlockingStream stream(http.getStreamPtr());

    DynamicJsonDocument filter(1024);
    JsonObject entryFilter = filter["stationboard"][0].to<JsonObject>();
    entryFilter["to"] = true;
    entryFilter["category"] = true;
    entryFilter["operator"] = true;
    entryFilter["number"] = true;
    entryFilter["stop"]["platform"] = true;
    entryFilter["stop"]["departure"] = true;
    entryFilter["stop"]["delay"] = true;
    for (int i = 0; i < STATIONBOARD_MAX_PASSLIST_ENTRIES; i++) {
        entryFilter["passList"][i]["station"]["name"] = true;
    }

    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        CTLog::error("stationboard: JSON parse error: " + String(err.c_str()));
        return result;
    }

    JsonArray board = doc["stationboard"].as<JsonArray>();
    CTLog::debug("stationboard: parsed entries=" + String(board.isNull() ? -1 : (int)board.size()));
    if (board.isNull() || board.size() == 0) {
        CTLog::info("stationboard: empty stationboard response");
        return result;
    }

    // Find the first entry that matches the requested platform
    for (JsonObject entry : board) {
        const char* entryPlatform = entry["stop"]["platform"];

        // If a platform filter is set, skip non-matching entries
        if (platform.length() > 0) {
            if (entryPlatform == nullptr) continue;
            if (!String(entryPlatform).equalsIgnoreCase(platform)) continue;
        }

        const char* depTs = entry["stop"]["departure"];
        result.destination = entry["to"].as<String>();
        result.category    = entry["category"].as<String>();
        result.operatorCode = entry["operator"].as<String>();
        result.number      = entry["number"].as<String>();
        result.platform    = entryPlatform != nullptr ? String(entryPlatform) : "";

        JsonArray passList = entry["passList"].as<JsonArray>();
        result.passListCount = 0;
        if (!passList.isNull()) {
            for (JsonObject passEntry : passList) {
                if (result.passListCount >= STATIONBOARD_MAX_PASSLIST_ENTRIES) break;
                String stationName = passEntry["station"]["name"].as<String>();
                stationName.trim();
                if (stationName.length() == 0) continue;
                result.passList[result.passListCount++] = stationName;
            }
        }

        int delaySeconds = entry["stop"]["delay"].as<int>();
        result.delay = (delaySeconds > 0)
            ? "+" + String(delaySeconds / 60)
            : String(delaySeconds / 60);

        parseDepartureTime(depTs, result.departureHour, result.departureMinute);

        result.valid = true;
        CTLog::info(
            "stationboard: next on platform " + platform +
            " -> " + result.destination +
            " " + result.departureHour + ":" + result.departureMinute +
            " (cat=" + result.category + ")"
        );
        break;
    }

    if (!result.valid) {
        CTLog::info("stationboard: no departure found for platform " + platform);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Station search
// ---------------------------------------------------------------------------

int CTStationboard::searchStations(
    const String& query,
    StationResult* outResults,
    int maxResults
) {
    if (query.isEmpty() || outResults == nullptr || maxResults <= 0) return 0;

    String url = String(STATIONBOARD_API_BASE) + "/locations?type=station&query=" + urlEncode(query);
    CTLog::info("stationboard: searching stations: " + url);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(STATIONBOARD_HTTP_TIMEOUT_MS);
    if (!http.begin(client, url)) {
        CTLog::error("stationboard: station search http.begin failed");
        return 0;
    }
    http.useHTTP10(true);
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");

    int httpCode = http.GET();
    if (httpCode != 200) {
        CTLog::error("stationboard: station search HTTP " + String(httpCode));
        http.end();
        return 0;
    }

    CTLog::debug("stationboard: station search response size=" + String(http.getSize()));

    BlockingStream stream(http.getStreamPtr());

    DynamicJsonDocument filter(512);
    JsonObject stationFilter = filter["stations"][0].to<JsonObject>();
    stationFilter["id"] = true;
    stationFilter["name"] = true;

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        CTLog::error("stationboard: station search JSON error: " + String(err.c_str()));
        return 0;
    }

    JsonArray stations = doc["stations"].as<JsonArray>();
    if (stations.isNull()) return 0;

    int count = 0;
    for (JsonObject s : stations) {
        if (count >= maxResults) break;
        const char* id   = s["id"];
        const char* name = s["name"];
        if (id == nullptr || name == nullptr) continue;
        outResults[count].id   = String(id);
        outResults[count].name = String(name);
        count++;
    }

    return count;
}

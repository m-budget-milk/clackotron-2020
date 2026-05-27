#include <ArduinoJson.h>
#include "ct_preferences.h"
#include "ct_logging.h"
#include "config.h"
#include "ct_stationboard.h"

bool CTPreferences::setup() {
    
    // Set up LittleFS for config and serving static files
    if (LittleFS.begin()) {
        CTLog::info("preferences: successfully mounted LittleFS");
    } else {
        CTLog::error("preferences: failed to mount LittleFS");
        return false;
    }

    // Set up preferences provider as r/s store
    this->preferences = Preferences();
    this->preferences.begin(PREFERENCES_NAMESPACE, false);

    // If force clear on boot flag is set for development, clear preferences.
    #if FORCE_CLEAR_ON_BOOT == 1
        CTLog::info("preferences: force-clearing preferences due to compile flag");
        this->clear();
    #endif

    // Create factory reset files by copying config files if a backup doesn't exist
    if (!LittleFS.exists(MODULE_CONFIG_BKP_PATH)) {
        this->copyLittleFsFile(MODULE_CONFIG_FILE_PATH, MODULE_CONFIG_BKP_PATH);
    }

    if (!LittleFS.exists(TIMEZONE_CONFIG_BKP_PATH)) {
        this->copyLittleFsFile(TIMEZONE_CONFIG_FILE_PATH, TIMEZONE_CONFIG_BKP_PATH);
    }

    if (!LittleFS.exists(WEBINTERFACE_CONFIG_BKP_PATH)) {
        this->copyLittleFsFile(WEBINTERFACE_CONFIG_FILE_PATH, WEBINTERFACE_CONFIG_BKP_PATH);
    }

    if (!LittleFS.exists(BOARD_MODULES_CONFIG_BKP_PATH)) {
        this->copyLittleFsFile(BOARD_MODULES_CONFIG_FILE_PATH, BOARD_MODULES_CONFIG_BKP_PATH);
    }

    if (!LittleFS.exists(MIRROR_TRANSFORM_CONFIG_BKP_PATH) && LittleFS.exists(MIRROR_TRANSFORM_CONFIG_FILE_PATH)) {
        this->copyLittleFsFile(MIRROR_TRANSFORM_CONFIG_FILE_PATH, MIRROR_TRANSFORM_CONFIG_BKP_PATH);
    }

    return true;
}

bool CTPreferences::isFirstBoot() {
    return this->preferences.getBool("notFirstBoot", true);
}

void CTPreferences::setFirstBoot(bool firstBoot) {
    this->preferences.putBool("notFirstBoot", firstBoot);
}

void CTPreferences::loadModuleAddresses(uint8_t *moduleAddresses) {
    File file = LittleFS.open(MODULE_CONFIG_FILE_PATH, "r");

    if (!file) {
        CTLog::error("preferences: failed to open module config file");
        return;
    }

    size_t size = file.size();
    std::unique_ptr<char[]> buf(new char[size]);

    file.readBytes(buf.get(), size);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, buf.get());

    if (error) {
        CTLog::error("preferences: failed to parse module config file");
        return;
    }

    JsonArray modules = doc["modules"];
    int moduleCount = 0;

    for (size_t i = 0; i < modules.size(); i++) {
        JsonArray row = modules[i];
        String dbg = "preferences: found module row with addresses: ";

        for (size_t j = 0; j < row.size(); j++) {
            int intValue = -1;

            if (row[j].is<int>()) {
                intValue = row[j].as<int>();
            } else if (row[j].is<const char*>()) {
                String token = String(row[j].as<const char*>());
                token.trim();

                if (token.equalsIgnoreCase("X")) {
                    dbg += " X";
                    continue;
                }

                bool isNumber = token.length() > 0;
                for (size_t k = 0; k < token.length(); k++) {
                    char c = token.charAt(k);
                    if (c < '0' || c > '9') {
                        isNumber = false;
                        break;
                    }
                }

                if (isNumber) {
                    intValue = token.toInt();
                }
            }

            if (intValue <= 0 || intValue > 255) {
                dbg += " ?";
                continue;
            }

            if (moduleCount >= MAX_CONNECTED_MODULES) {
                CTLog::error("preferences: module address list exceeds MAX_CONNECTED_MODULES");
                break;
            }

            moduleAddresses[moduleCount++] = (uint8_t)intValue;
            dbg += " " + String(intValue);
        }

        CTLog::debug(dbg);
    }
}

void CTPreferences::loadTimeZoneData(
      TimeChangeRule *timeDefault, 
      TimeChangeRule *timeDst, 
      char* ntpServer,
      char dayNamesShort[8][3],
      char dayNamesLong[8][4]
    ) {
    File file = LittleFS.open(TIMEZONE_CONFIG_FILE_PATH, "r");

    if (!file) {
        CTLog::error("preferences: failed to open timezone config file");
        return;
    }

    size_t size = file.size();
    std::unique_ptr<char[]> buf(new char[size]);

    file.readBytes(buf.get(), size);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, buf.get());

    if (error) {
        CTLog::error("preferences: failed to parse timezone config file");
        return;
    }

    JsonArray timeDefaultData = doc["time_normal"];
    JsonArray timeDstData = doc["time_dst"];
    JsonArray dayNamesShortData = doc["days_short"];
    JsonArray dayNamesLongData = doc["days_long"];

    const char * ntpServerData = doc["ntp_server"];

    strcpy(ntpServer, ntpServerData);
    CTLog::debug("preferences: loaded ntp server: " + String(ntpServer));

    String dbgShortNames = "";
    for (int i = 0; i < sizeof(dayNamesShortData); i++) {
        strncpy(dayNamesShort[i], dayNamesShortData[i], 3);
        dbgShortNames += ", " + String(dayNamesShort[i]);
    }
    CTLog::debug("preferences: loaded day names short: " + dbgShortNames);

    String dbgLongNames = "";
    for (int i = 0; i < sizeof(dayNamesLongData); i++) {
        strncpy(dayNamesLong[i], dayNamesLongData[i], 4);
        dbgLongNames += ", " + String(dayNamesLong[i]);
    }
    CTLog::debug("preferences: loaded day names long: " + dbgLongNames);
    
    this->overrideTimeChangeRule("default", timeDefault, timeDefaultData);
    this->overrideTimeChangeRule("dst", timeDst, timeDstData);
    
}

String CTPreferences::getConfig() {
    return this->preferences.getString("config", "{}");
}

String CTPreferences::getConfigTemplate() {
    String configJson = this->preferences.getString("config", "{\"template\": \"\"}");
    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, configJson);

    if (error) {
        CTLog::error("preferences: failed to parse config json object");
        return "";
    }

    return doc["template"];
}

String CTPreferences::getConfigTextParam(String key) {
    String configJson = this->preferences.getString("config", "{}");
    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, configJson);

    if (error) {
        CTLog::error("preferences: failed to parse config json object");
        return "";
    }

    return doc["parameters"][key];
}

uint32_t CTPreferences::getConfigNumberParam(String key) {
    String configJson = this->preferences.getString("config", "{}");
    DynamicJsonDocument doc(1024);

    DeserializationError error = deserializeJson(doc, configJson);

    if (error) {
        CTLog::error("preferences: failed to parse config json object");
        return 0;
    }

    return doc["parameters"][key];
}

void CTPreferences::setConfig(String config) {
    this->preferences.putString("config", config);
}

void CTPreferences::clear() {
    this->preferences.clear();
    
    this->copyLittleFsFile(MODULE_CONFIG_BKP_PATH, MODULE_CONFIG_FILE_PATH);
    this->copyLittleFsFile(TIMEZONE_CONFIG_BKP_PATH, TIMEZONE_CONFIG_FILE_PATH);
    this->copyLittleFsFile(WEBINTERFACE_CONFIG_BKP_PATH, WEBINTERFACE_CONFIG_FILE_PATH);
    this->copyLittleFsFile(BOARD_MODULES_CONFIG_BKP_PATH, BOARD_MODULES_CONFIG_FILE_PATH);
    if (LittleFS.exists(MIRROR_TRANSFORM_CONFIG_BKP_PATH)) {
        this->copyLittleFsFile(MIRROR_TRANSFORM_CONFIG_BKP_PATH, MIRROR_TRANSFORM_CONFIG_FILE_PATH);
    }
    
    CTLog::info("preferences: cleared preferences and config files");
}

void CTPreferences::overrideTimeChangeRule(const char* tag, TimeChangeRule *rule, JsonArray data) {
    if (
        (data[0] < 0 || data[0] > 4) ||
        (data[1] < 1 || data[1] > 7) ||
        (data[2] < 1 || data[2] > 12) ||
        (data[3] < 0 || data[3] > 23)
    ) {
        CTLog::error("preferences: loading " + String(tag) + " time data failed - Invalid values!");
    }

    rule->week = data[0];
    rule->dow = data[1];
    rule->month = data[2];
    rule->hour = data[3];
    rule->offset = data[4];

    String dbg = "";
    dbg += " week=" + String((int)data[0]);
    dbg += " dow=" + String((int)data[1]);
    dbg += " month=" + String((int)data[2]);
    dbg += " hour=" + String((int)data[3]);
    dbg += " offset=" + String((int)data[4]);

    CTLog::debug("preferences: loaded " + String(tag) + " time data: " + dbg);
}


bool CTPreferences::loadBoardModules() {
    if (this->boardModulesDoc != nullptr) {
        delete this->boardModulesDoc;
    }

    File file = LittleFS.open(BOARD_MODULES_CONFIG_FILE_PATH, "r");
    if (!file) {
        CTLog::error("preferences: failed to open board modules config file");
        this->boardModulesDoc = nullptr;
        return false;
    }

    size_t fileSize = file.size();
    size_t capacity = fileSize * 2;
    if (capacity < 8192) {
        capacity = 8192;
    }

    this->boardModulesDoc = new DynamicJsonDocument(capacity);

    DeserializationError error = deserializeJson(*this->boardModulesDoc, file);
    file.close();
    if (error) {
        CTLog::error(
            "preferences: failed to parse board modules config file: " +
            String(error.c_str()) +
            " (size=" + String((unsigned int)fileSize) + ", capacity=" + String((unsigned int)capacity) + ")"
        );
        delete this->boardModulesDoc;
        this->boardModulesDoc = nullptr;
        return false;
    }

    CTLog::info("preferences: loaded board modules with " + String(this->getBoardModuleCount()) + " modules");
    return true;
}

int CTPreferences::getBoardModuleCount() {
    if (this->boardModulesDoc == nullptr) return 0;
    return (*this->boardModulesDoc)["modules"].size();
}

uint8_t CTPreferences::getBoardModuleAddress(int i) {
    if (this->boardModulesDoc == nullptr) return 0;
    return (*this->boardModulesDoc)["modules"][i]["address"].as<uint8_t>();
}

bool CTPreferences::isValidBoardAddress(uint8_t addr) {
    if (this->boardModulesDoc == nullptr) return false;
    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() == addr) return true;
    }
    return false;
}

bool CTPreferences::isValidBoardPosition(uint8_t addr, int pos) {
    if (this->boardModulesDoc == nullptr) return false;
    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() == addr) {
            return pos >= 0 && pos < (int)mod["positions"].size();
        }
    }
    return false;
}

int CTPreferences::getBoardDefaultPosition(uint8_t addr) {
    if (this->boardModulesDoc == nullptr) return 0;
    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() == addr) {
            return mod["defaultPosition"] | 0;
        }
    }
    return 0;
}

int CTPreferences::getBoardSavedPosition(uint8_t addr) {
    if (!this->preferences.isKey("boardPos")) {
        return this->getBoardDefaultPosition(addr);
    }

    String saved = this->preferences.getString("boardPos", "{}");
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, saved);
    String addrStr = String(addr);
    if (!error && doc.containsKey(addrStr)) {
        return doc[addrStr].as<int>();
    }
    return this->getBoardDefaultPosition(addr);
}

void CTPreferences::setBoardPositions(String json) {
    this->preferences.putString("boardPos", json);
}

String CTPreferences::getBoardPositionsJson() {
    if (this->boardModulesDoc == nullptr) return "{\"modulePositions\":{}}";

    DynamicJsonDocument savedDoc(256);
    if (this->preferences.isKey("boardPos")) {
        String saved = this->preferences.getString("boardPos", "{}");
        deserializeJson(savedDoc, saved);
    }

    DynamicJsonDocument result(512);
    JsonObject positions = result.createNestedObject("modulePositions");
    result["mode"] = this->getDisplayMode();
    result["randomIntervalSeconds"] = this->getRandomShuffleIntervalSeconds();

    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        uint8_t addr = mod["address"].as<uint8_t>();
        String addrStr = String(addr);
        int pos = savedDoc.containsKey(addrStr) ? savedDoc[addrStr].as<int>() : (mod["defaultPosition"] | 0);
        positions[addrStr] = pos;
    }

    String output;
    serializeJson(result, output);
    return output;
}

String CTPreferences::getDisplayMode() {
    String mode = this->preferences.getString("displayMode", "manual");
    mode.toLowerCase();
    if (mode != "random" && mode != "mirror") {
        return "manual";
    }

    return mode;
}

void CTPreferences::setDisplayMode(String mode) {
    mode.toLowerCase();
    if (mode != "random" && mode != "mirror") {
        mode = "manual";
    }

    this->preferences.putString("displayMode", mode);
}

uint32_t CTPreferences::getRandomShuffleIntervalSeconds() {
    uint32_t seconds = this->preferences.getUInt("randomIntSec", 60);

    if (seconds < 1) seconds = 1;
    if (seconds > 86400) seconds = 86400;

    return seconds;
}

void CTPreferences::setRandomShuffleIntervalSeconds(uint32_t seconds) {
    if (seconds < 1) seconds = 1;
    if (seconds > 86400) seconds = 86400;

    this->preferences.putUInt("randomIntSec", seconds);
}

int CTPreferences::getBoardModulePositionCount(uint8_t addr) {
    if (this->boardModulesDoc == nullptr) return 0;

    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() == addr) {
            return (int)mod["positions"].size();
        }
    }

    return 0;
}

int CTPreferences::getRandomNonEmptyBoardPosition(uint8_t addr) {
    if (this->boardModulesDoc == nullptr) return -1;

    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() != addr) continue;

        JsonArray positions = mod["positions"].as<JsonArray>();
        if (positions.isNull() || positions.size() == 0) return -1;

        int nonEmptyCount = 0;
        for (JsonObject posObj : positions) {
            String label = posObj["label"].as<String>();
            label.trim();
            if (label.length() > 0) {
                nonEmptyCount++;
            }
        }

        if (nonEmptyCount == 0) return -1;

        int pick = random(nonEmptyCount);
        int seen = 0;
        for (int i = 0; i < (int)positions.size(); i++) {
            String label = positions[i]["label"].as<String>();
            label.trim();
            if (label.length() == 0) continue;

            if (seen == pick) return i;
            seen++;
        }

        return -1;
    }

    return -1;
}

int CTPreferences::findBoardPositionByLabel(uint8_t addr, const String& value) {
    if (this->boardModulesDoc == nullptr) return -1;
    JsonArray modules = (*this->boardModulesDoc)["modules"].as<JsonArray>();
    for (JsonObject mod : modules) {
        if (mod["address"].as<uint8_t>() == addr) {
            return findPositionByLabel(mod["positions"].as<JsonArray>(), value);
        }
    }
    return -1;
}

String CTPreferences::getMirrorConfig() {
    String stored = this->preferences.getString("mirrorCfg", "");
    if (stored.length() > 0) return stored;

    // Return built-in defaults when nothing is stored yet
    return "{\"enabled\":false,\"stationQuery\":\"\",\"stationId\":\"\",\"platform\":\"\",\"refreshIntervalSeconds\":30,\"mappings\":{},\"departureWindowEnabled\":false,\"departureWindowMinutes\":10}";
}

void CTPreferences::setMirrorConfig(String json) {
    this->preferences.putString("mirrorCfg", json);
}

String CTPreferences::resolveMirrorTransformedValue(const String& category, const String& operatorCode) {
    if (category.length() == 0) return "";

    File file = LittleFS.open(MIRROR_TRANSFORM_CONFIG_FILE_PATH, "r");
    if (!file) {
        CTLog::error("preferences: mirror transformations file missing");
        return "";
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        CTLog::error("preferences: failed to parse mirror transformations: " + String(err.c_str()));
        return "";
    }

    JsonArray rules = doc["transformations"].as<JsonArray>();
    if (rules.isNull()) return "";

    // Pass 1: exact category + operator match (most specific)
    for (JsonObject rule : rules) {
        String c = rule["category"].as<String>();
        String o = rule["operator"].as<String>();
        String display = rule["display"].as<String>();

        if (display.length() == 0 || c.length() == 0 || o.length() == 0) continue;
        if (c.equalsIgnoreCase(category) && o.equalsIgnoreCase(operatorCode)) {
            CTLog::debug("preferences: mirror transform exact match " + c + "+" + o + " -> " + display);
            return display;
        }
    }

    // Pass 2: category-only match (operator omitted or empty in rule)
    for (JsonObject rule : rules) {
        String c = rule["category"].as<String>();
        String display = rule["display"].as<String>();
        bool hasOperator = rule.containsKey("operator");
        String o = hasOperator ? rule["operator"].as<String>() : "";

        if (display.length() == 0 || c.length() == 0) continue;
        if ((!hasOperator || o.length() == 0) && c.equalsIgnoreCase(category)) {
            CTLog::debug("preferences: mirror transform category match " + c + " -> " + display);
            return display;
        }
    }

    return "";
}

String CTPreferences::resolveMirrorDestinationOverride(const String& toValue) {
    if (toValue.length() == 0) return "";

    File file = LittleFS.open(MIRROR_TRANSFORM_CONFIG_FILE_PATH, "r");
    if (!file) return "";

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        CTLog::error("preferences: failed to parse mirror transformations: " + String(err.c_str()));
        return "";
    }

    JsonArray rules = doc["transformations"].as<JsonArray>();
    if (rules.isNull()) return "";

    for (JsonObject rule : rules) {
        String toRule = rule["to"].as<String>();
        String display = rule["display"].as<String>();
        if (toRule.length() == 0 || display.length() == 0) continue;
        if (toRule.equalsIgnoreCase(toValue)) {
            CTLog::debug("preferences: mirror destination override " + toRule + " -> " + display);
            return display;
        }
    }

    return "";
}

void CTPreferences::copyLittleFsFile(const char* src, const char* dst) {
    CTLog::info("preferences: copying file " + String(src) + " to " + String(dst) + "...");

    File srcFile = LittleFS.open(src, "r");
    File dstFile = LittleFS.open(dst, "w");

        while (srcFile.available()) {
            dstFile.write(srcFile.read());
        }

    srcFile.close();
    dstFile.close();

    CTLog::info("preferences: copied file " + String(src) + " to " + String(dst) + ".");
}

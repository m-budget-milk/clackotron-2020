#include <WebServer.h>
#include <LittleFS.h>
#include "ct_webserver.h"
#include "ct_logging.h"
#include "config.h"
#include "ct_stationboard.h"
#include <ArduinoJson.h>

static bool parseModuleAddress(const String& raw, uint8_t* outAddr) {
    if (outAddr == nullptr || raw.length() == 0) return false;

    for (size_t i = 0; i < raw.length(); i++) {
        char c = raw.charAt(i);
        if (c < '0' || c > '9') return false;
    }

    long parsed = raw.toInt();
    if (parsed < 1 || parsed > 255) return false;

    *outAddr = (uint8_t)parsed;
    return true;
}

static bool isConfiguredModuleAddress(uint8_t addr, uint8_t* addresses) {
    if (addresses == nullptr) return false;

    for (uint8_t i = 0; i < MAX_CONNECTED_MODULES; i++) {
        if (addresses[i] == 0x00) continue;
        if (addresses[i] == addr) return true;
    }

    return false;
}

CTWebserver::CTWebserver() {
    this->server = nullptr;
}

CTWebserver::~CTWebserver() {
    delete this->server;
}

void CTWebserver::setup(CTPreferences* preferences, CTModule* module, uint8_t* moduleAddresses, bool* needsToLoadConfig) {
    this->preferences = preferences;
    this->module = module;
    this->moduleAddresses = moduleAddresses;
    this->needsToLoadConfig = needsToLoadConfig;

    this->server = new WebServer(WEBSERVER_PORT);

    this->createIndexRoute();
    this->createStaticRoutes();
    this->createConfigGetRoute();
    this->createConfigSetRoute();
    this->createZeroRoute();
    this->createStepRoute();
    this->createTypeRoute();
    this->createAddrRoute();
    this->createMirrorConfigGetRoute();
    this->createMirrorConfigSetRoute();
    this->createStationSearchRoute();

    this->server->begin();
}

void CTWebserver::handle() {
    if (this->server == nullptr) return;

    this->server->handleClient();
}

void CTWebserver::createIndexRoute() {
    this->server->on("/", HTTP_GET, [this]() {
        this->server->sendHeader("Connection", "close");
        this->server->sendHeader("Location", "/ui/index.html");
        this->server->send(301, "text/plain", "/ui/index.html");
    });
}

void CTWebserver::createStaticRoutes() {
    this->server->serveStatic("/ui", LittleFS, "/", "");
}

void CTWebserver::createConfigGetRoute() {
    this->server->on("/config", HTTP_GET, [this]() {
        if (this->preferences == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "No preferences object set");
            return;
        }

        String config = this->preferences->getBoardPositionsJson();

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", config);
    });
}

void CTWebserver::createConfigSetRoute() {
    this->server->on("/config", HTTP_POST, [this]() {
        if (!this->server->hasArg("payload")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\": false}");
            return;
        }

        if (this->preferences == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "No preferences object set");
            return;
        }
        
        String payload = WebServer::urlDecode(this->server->arg("payload"));

        DynamicJsonDocument doc(1024);
        DeserializationError err = deserializeJson(doc, payload);
        if (err || !doc.containsKey("modulePositions")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"invalid payload\"}");
            return;
        }

        JsonObject positions = doc["modulePositions"].as<JsonObject>();
        for (JsonPair kv : positions) {
            uint8_t addr = (uint8_t)String(kv.key().c_str()).toInt();
            int pos = kv.value().as<int>();
            if (!this->preferences->isValidBoardAddress(addr)) {
                this->server->sendHeader("Connection", "close");
                this->server->send(400, "application/json", "{\"success\":false,\"error\":\"unknown address\"}");
                return;
            }
            if (!this->preferences->isValidBoardPosition(addr, pos)) {
                this->server->sendHeader("Connection", "close");
                this->server->send(400, "application/json", "{\"success\":false,\"error\":\"invalid position\"}");
                return;
            }
        }

        String posJson;
        serializeJson(positions, posJson);
        this->preferences->setBoardPositions(posJson);
        CTLog::info("webserver: set board positions to " + posJson);

        if (doc.containsKey("mode")) {
            this->preferences->setDisplayMode(doc["mode"].as<String>());
        }
        if (doc.containsKey("randomIntervalSeconds")) {
            uint32_t interval = doc["randomIntervalSeconds"].as<uint32_t>();
            this->preferences->setRandomShuffleIntervalSeconds(interval);
        }

        *this->needsToLoadConfig = true;

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", "{\"success\": true}");
    });
}

void CTWebserver::createZeroRoute() {
    this->server->on("/zero", HTTP_GET, [this]() {
        if (this->module == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "Internal module is not set");
            return;
        }

        if (this->server->hasArg("addr")) {
            // Zero a specific module
            String addr = this->server->arg("addr");
            uint8_t moduleAddr = 0;
            if (!parseModuleAddress(addr, &moduleAddr)) {
                this->server->sendHeader("Connection", "close");
                this->server->send(400, "text/plain", "Query parameter 'addr' must be an integer in range 1-255\n");
                return;
            }

            if (!isConfiguredModuleAddress(moduleAddr, this->moduleAddresses)) {
                this->server->sendHeader("Connection", "close");
                this->server->send(404, "text/plain", "Module address not configured\n");
                return;
            }

            CTLog::info("webserver: zeroing module " + String(moduleAddr, HEX));
            this->module->zero(moduleAddr);

            this->server->sendHeader("Connection", "close");
            this->server->send(200, "text/plain", "Zeroed module " + addr + "\n");
        } else {
            // Zero all modules
            if (this->moduleAddresses == nullptr) {
                this->server->sendHeader("Connection", "close");
                this->server->send(500, "text/plain", "Module addresses not set");
                return;
            }

            CTLog::info("webserver: zeroing all modules");
            this->module->zeroAll(this->moduleAddresses, MAX_CONNECTED_MODULES);

            this->server->sendHeader("Connection", "close");
            this->server->send(200, "text/plain", "Zeroed all modules\n");
        }
    });
}

void CTWebserver::createStepRoute() {
    this->server->on("/step", HTTP_GET, [this]() {
        if (this->module == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "Internal module is not set");
            return;
        }

        if (!this->server->hasArg("addr")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "text/plain", "Query parameter 'addr' is required");
            return;
        }

        String addr = this->server->arg("addr");
        uint8_t moduleAddr = 0;
        if (!parseModuleAddress(addr, &moduleAddr)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "text/plain", "Query parameter 'addr' must be an integer in range 1-255\n");
            return;
        }

        if (!isConfiguredModuleAddress(moduleAddr, this->moduleAddresses)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(404, "text/plain", "Module address not configured\n");
            return;
        }

        CTLog::info("webserver: stepping module " + String(moduleAddr, HEX));
        this->module->step(moduleAddr);

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "text/plain", "Stepped module " + addr + "\n");
    });
}

void CTWebserver::createTypeRoute() {
    this->server->on("/type", HTTP_GET, [this]() {
        if (this->module == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "application/json", "{\"success\":false,\"error\":\"module not set\"}");
            return;
        }

        if (!this->server->hasArg("addr")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"addr required\"}");
            return;
        }

        String addr = this->server->arg("addr");
        uint8_t moduleAddr = 0;
        if (!parseModuleAddress(addr, &moduleAddr)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"addr must be integer 1-255\"}");
            return;
        }

        if (!isConfiguredModuleAddress(moduleAddr, this->moduleAddresses)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(404, "application/json", "{\"success\":false,\"error\":\"module address not configured\"}");
            return;
        }
        uint8_t moduleType = 0;

        bool success = this->module->getType(moduleAddr, &moduleType);
        if (!success) {
            this->server->sendHeader("Connection", "close");
            this->server->send(504, "application/json", "{\"success\":false,\"error\":\"no response\"}");
            return;
        }

        String typeName = "Unknown";
            if (moduleType == 0x01) typeName = "40 Blades";
            if (moduleType == 0x02) typeName = "62 Blades";
            if (moduleType == 0x42) typeName = "62 Blades";

        String response = "{\"success\":true,\"addr\":" + String(moduleAddr);
        response += ",\"type\":" + String(moduleType);
        response += ",\"typeHex\":\"0x" + String(moduleType, HEX) + "\"";
        response += ",\"typeName\":\"" + typeName + "\"}";

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", response);
    });
}

void CTWebserver::createAddrRoute() {
    this->server->on("/addr", HTTP_GET, [this]() {
        if (this->module == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "application/json", "{\"success\":false,\"error\":\"module not set\"}");
            return;
        }

        if (!this->server->hasArg("oldAddr") || !this->server->hasArg("newAddr")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"oldAddr and newAddr required\"}");
            return;
        }

        String oldAddrStr = this->server->arg("oldAddr");
        String newAddrStr = this->server->arg("newAddr");
        uint8_t oldAddr = 0;
        uint8_t newAddr = 0;

        if (!parseModuleAddress(oldAddrStr, &oldAddr) || !parseModuleAddress(newAddrStr, &newAddr)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"addresses must be integers 1-255\"}");
            return;
        }

        if (!isConfiguredModuleAddress(oldAddr, this->moduleAddresses)) {
            this->server->sendHeader("Connection", "close");
            this->server->send(404, "application/json", "{\"success\":false,\"error\":\"old address not configured\"}");
            return;
        }

        this->module->changeAddress(oldAddr, newAddr);

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", "{\"success\":true,\"oldAddr\":" + String(oldAddr) + ",\"newAddr\":" + String(newAddr) + "}");
    });
}

void CTWebserver::createMirrorConfigGetRoute() {
    this->server->on("/mirror-config", HTTP_GET, [this]() {
        if (this->preferences == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "No preferences object set");
            return;
        }

        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", this->preferences->getMirrorConfig());
    });
}

void CTWebserver::createMirrorConfigSetRoute() {
    this->server->on("/mirror-config", HTTP_POST, [this]() {
        if (!this->server->hasArg("payload")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"missing payload\"}");
            return;
        }

        if (this->preferences == nullptr) {
            this->server->sendHeader("Connection", "close");
            this->server->send(500, "text/plain", "No preferences object set");
            return;
        }

        String payload = WebServer::urlDecode(this->server->arg("payload"));

        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"invalid JSON\"}");
            return;
        }

        // Validate refreshIntervalSeconds bounds
        if (doc.containsKey("refreshIntervalSeconds")) {
            int interval = doc["refreshIntervalSeconds"].as<int>();
            if (interval < MIRROR_MIN_REFRESH_SECONDS || interval > MIRROR_MAX_REFRESH_SECONDS) {
                this->server->sendHeader("Connection", "close");
                this->server->send(400, "application/json", "{\"success\":false,\"error\":\"refreshIntervalSeconds out of range\"}");
                return;
            }
        }

        // Validate mapping field names when mappings object is present
        if (doc.containsKey("mappings")) {
            JsonObject mappings = doc["mappings"].as<JsonObject>();
            const char* validFields[] = {
                MIRROR_FIELD_NONE, MIRROR_FIELD_DESTINATION,
                MIRROR_FIELD_DEPARTURE_HOUR, MIRROR_FIELD_DEPARTURE_MINUTE,
                MIRROR_FIELD_CATEGORY, MIRROR_FIELD_NUMBER,
                MIRROR_FIELD_DELAY, MIRROR_FIELD_PLATFORM,
                MIRROR_FIELD_NEXT_STATIONS
            };
            const int validCount = 9;

            for (JsonPair kv : mappings) {
                // Validate module address key
                uint8_t addr = 0;
                if (!parseModuleAddress(String(kv.key().c_str()), &addr)) {
                    this->server->sendHeader("Connection", "close");
                    this->server->send(400, "application/json", "{\"success\":false,\"error\":\"invalid address key in mappings\"}");
                    return;
                }
                if (!this->preferences->isValidBoardAddress(addr)) {
                    this->server->sendHeader("Connection", "close");
                    this->server->send(400, "application/json", "{\"success\":false,\"error\":\"unknown module address in mappings\"}");
                    return;
                }

                // Validate field value
                String field = kv.value().as<String>();
                bool fieldOk = false;
                for (int i = 0; i < validCount; i++) {
                    if (field == validFields[i]) { fieldOk = true; break; }
                }
                if (!fieldOk) {
                    this->server->sendHeader("Connection", "close");
                    this->server->send(400, "application/json", "{\"success\":false,\"error\":\"unknown field name: " + field + "\"}");
                    return;
                }
            }
        }

        String serialized;
        serializeJson(doc, serialized);
        this->preferences->setMirrorConfig(serialized);

        // Keep runtime mode aligned with mirror enable toggle from the mirror UI.
        bool mirrorEnabled = doc["enabled"] | false;
        this->preferences->setDisplayMode(mirrorEnabled ? "mirror" : "manual");
        *this->needsToLoadConfig = true;

        CTLog::info("webserver: mirror config updated");
        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", "{\"success\":true}");
    });
}

void CTWebserver::createStationSearchRoute() {
    this->server->on("/api/station-search", HTTP_GET, [this]() {
        if (!this->server->hasArg("query")) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"query parameter required\"}");
            return;
        }

        String query = this->server->arg("query");
        if (query.length() < 2) {
            this->server->sendHeader("Connection", "close");
            this->server->send(400, "application/json", "{\"success\":false,\"error\":\"query too short\"}");
            return;
        }

        CTStationboard sb;
        StationResult results[10];
        int count = sb.searchStations(query, results, 10);

        DynamicJsonDocument doc(2048);
        JsonArray arr = doc.createNestedArray("stations");
        for (int i = 0; i < count; i++) {
            JsonObject s = arr.createNestedObject();
            s["id"]   = results[i].id;
            s["name"] = results[i].name;
        }
        doc["success"] = true;

        String output;
        serializeJson(doc, output);
        this->server->sendHeader("Connection", "close");
        this->server->send(200, "application/json", output);
    });
}


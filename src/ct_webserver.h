#ifndef _CLACKOTRON_WEBSERVER_H_
#define _CLACKOTRON_WEBSERVER_H_

#include <Arduino.h>
#include <WebServer.h>
#include "ct_preferences.h"
#include "ct_module.h"

/// @brief A class for controlling the webserver (normal mode)
class CTWebserver {
    public:
        CTWebserver();
        ~CTWebserver();

        /// @brief Set up the webserver endpoints
        /// @param preferences - Pointer to the preferences object
        /// @param module - Pointer to the module object
        /// @param moduleAddresses - Pointer to the array of module addresses
        /// @param needsToLoadConfig - Pointer to the flag that indicates if the config needs to be loaded
        void setup(CTPreferences* preferences, CTModule* module, uint8_t* moduleAddresses, bool* needsToLoadConfig);

        /// @brief Handle incoming requests
        void handle();

    private:
        WebServer* server;

        CTPreferences* preferences;
        CTModule* module;
        uint8_t* moduleAddresses;
        bool* needsToLoadConfig;

        void createIndexRoute();
        void createStaticRoutes();
        void createConfigGetRoute();
        void createConfigSetRoute();
        void createModuleLayoutSetRoute();
        void createBoardModulesSetRoute();
        void createRestartRoute();
        void createZeroRoute();
        void createStepRoute();
        void createTypeRoute();
        void createAddrRoute();
        void createMirrorConfigGetRoute();
        void createMirrorConfigSetRoute();
        void createStationSearchRoute();
};

#endif // _CLACKOTRON_WEBSERVER_H_

/* Sensitive Data like WiFi credentials, API-Key, Location. 
 * Added to gitignore - modify once and be happy
 * Keep config in sync with upstream while having a public repo
 * without the need to update it everytime. 
 * Copyright (C) 2026  Timo Hilbig
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include "config.h"

 // WIFI
const char *WIFI_SSID     = "ssid";
const char *WIFI_PASSWORD = "password";

// OpenWeatherMap API key, https://openweathermap.org/
const String OWM_APIKEY   = "0123456789abcdef0123456789abcdef";

// LOCATION
// Set your latitude and longitude.
// (used to get weather data as part of API requests to OpenWeatherMap)
const String LAT = "40.7128";
const String LON = "-74.0060";
// City name that will be shown in the top-right corner of the display.
const String CITY_STRING = "New York";

#pragma once
#include <Arduino.h>
// The P4 MQTT owner and its bounded command API live in p4_mqtt_worker.cpp.
// Protocol parsing remains in bambu_mqtt.cpp; no caller owns its sockets.
#include "p4_runtime.h"

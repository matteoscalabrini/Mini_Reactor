#include <Arduino.h>
#include "features/hub/HubRuntime.hpp"

void setup() { HubRuntime::begin(); }
void loop()  { HubRuntime::tick(); }

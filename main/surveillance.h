#pragma once

#include "observation_store.h"

surveillance_class_t surveillance_classify(const rf_device_t *observation);
const char *surveillance_name(surveillance_class_t classification);
char surveillance_morse_letter(surveillance_class_t classification);

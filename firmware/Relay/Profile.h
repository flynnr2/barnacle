#pragma once

// Legacy builds keep HNTBarnacle's existing behavior. The public builder always
// supplies a generated configuration and an explicitly selected device key.
#ifdef CUBECELL_DEVICE_CONFIG
#include "device_config.h"
#endif
#ifndef CUBECELL_PROFILE
#define CUBECELL_PROFILE 2
#endif
#ifndef CUBECELL_NODE_NAME
#define CUBECELL_NODE_NAME "HNTBarnacle"
#endif
#if CUBECELL_PROFILE < 1 || CUBECELL_PROFILE > 3
#error "Select repeater (1), repeater-clock (2), or time-publisher (3)"
#endif

namespace relayfw { namespace profile {
static const bool FORWARDING = CUBECELL_PROFILE != 3;
static const bool HOURLY_TIME = CUBECELL_PROFILE != 1;
static const unsigned ADVERT_TYPE = FORWARDING ? 2 : 4; // Repeater / sensor.
static const char NAME[] = CUBECELL_NODE_NAME;
#if CUBECELL_PROFILE == 1
static const char ROLE[] = "repeater";
static const char OLED_ROLE[] = "Relay";
#elif CUBECELL_PROFILE == 2
static const char ROLE[] = "repeater-clock";
static const char OLED_ROLE[] = "Relay";
#else
static const char ROLE[] = "time-publisher";
static const char OLED_ROLE[] = "Time";
#endif
static_assert(sizeof(NAME) > 1 && sizeof(NAME) <= 19, "Node name must contain 1..18 ASCII bytes");
}}

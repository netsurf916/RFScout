#include "surveillance.h"
#include <ctype.h>
#include <string.h>

typedef struct { uint8_t oui[3]; surveillance_class_t cls; } oui_sig_t;
typedef struct { const char *fragment; surveillance_class_t cls; } name_sig_t;

static const oui_sig_t oui_sigs[] = {
    /* Flock Safety / ALPR signatures carried forward from OUI-SPY. */
    {{0xB4,0x1E,0x52},SURVEILLANCE_FLOCK},{{0x70,0xC9,0x4E},SURVEILLANCE_FLOCK},
    {{0x3C,0x91,0x80},SURVEILLANCE_FLOCK},{{0xD8,0xF3,0xBC},SURVEILLANCE_FLOCK},
    {{0x80,0x30,0x49},SURVEILLANCE_FLOCK},{{0xB8,0x35,0x32},SURVEILLANCE_FLOCK},
    {{0x14,0x5A,0xFC},SURVEILLANCE_FLOCK},{{0x74,0x4C,0xA1},SURVEILLANCE_FLOCK},
    {{0x08,0x3A,0x88},SURVEILLANCE_FLOCK},{{0x9C,0x2F,0x9D},SURVEILLANCE_FLOCK},
    {{0xC0,0x35,0x32},SURVEILLANCE_FLOCK},{{0x94,0x08,0x53},SURVEILLANCE_FLOCK},
    {{0xE4,0xAA,0xEA},SURVEILLANCE_FLOCK},{{0xF4,0x6A,0xDD},SURVEILLANCE_FLOCK},
    {{0xF8,0xA2,0xD6},SURVEILLANCE_FLOCK},{{0x24,0xB2,0xB9},SURVEILLANCE_FLOCK},
    {{0x00,0xF4,0x8D},SURVEILLANCE_FLOCK},{{0xD0,0x39,0x57},SURVEILLANCE_FLOCK},
    {{0xE8,0xD0,0xFC},SURVEILLANCE_FLOCK},{{0xE0,0x4F,0x43},SURVEILLANCE_FLOCK},
    {{0xB8,0x1E,0xA4},SURVEILLANCE_FLOCK},{{0x70,0x08,0x94},SURVEILLANCE_FLOCK},
    {{0x58,0x8E,0x81},SURVEILLANCE_FLOCK},{{0xEC,0x1B,0xBD},SURVEILLANCE_FLOCK},
    {{0x3C,0x71,0xBF},SURVEILLANCE_FLOCK},{{0x58,0x00,0xE3},SURVEILLANCE_FLOCK},
    {{0x90,0x35,0xEA},SURVEILLANCE_FLOCK},{{0x5C,0x93,0xA2},SURVEILLANCE_FLOCK},
    {{0x64,0x6E,0x69},SURVEILLANCE_FLOCK},{{0x48,0x27,0xEA},SURVEILLANCE_FLOCK},
    {{0xA4,0xCF,0x12},SURVEILLANCE_FLOCK},
    {{0x00,0x25,0xDF},SURVEILLANCE_BODY_CAMERA},
    {{0x0C,0x9A,0xE6},SURVEILLANCE_DRONE},{{0x8C,0x58,0x23},SURVEILLANCE_DRONE},
    {{0x04,0xA8,0x5A},SURVEILLANCE_DRONE},{{0x58,0xB8,0x58},SURVEILLANCE_DRONE},
    {{0xE4,0x7A,0x2C},SURVEILLANCE_DRONE},{{0x60,0x60,0x1F},SURVEILLANCE_DRONE},
    {{0x48,0x1C,0xB9},SURVEILLANCE_DRONE},{{0x34,0xD2,0x62},SURVEILLANCE_DRONE},
    {{0x00,0x12,0x1C},SURVEILLANCE_DRONE},{{0x00,0x26,0x7E},SURVEILLANCE_DRONE},
    {{0x90,0x03,0xB7},SURVEILLANCE_DRONE},{{0x90,0x3A,0xE6},SURVEILLANCE_DRONE},
    {{0xA0,0x14,0x3D},SURVEILLANCE_DRONE},{{0x38,0x1D,0x14},SURVEILLANCE_DRONE},
    {{0x18,0x7F,0x88},SURVEILLANCE_CAMERA},{{0x24,0x2B,0xD6},SURVEILLANCE_CAMERA},
    {{0x34,0x3E,0xA4},SURVEILLANCE_CAMERA},{{0x50,0xE4,0x67},SURVEILLANCE_CAMERA},
    {{0x54,0xE0,0x19},SURVEILLANCE_CAMERA},{{0x5C,0x47,0x5E},SURVEILLANCE_CAMERA},
    {{0x64,0x9A,0x63},SURVEILLANCE_CAMERA},{{0x90,0x48,0x6C},SURVEILLANCE_CAMERA},
    {{0x9C,0x76,0x13},SURVEILLANCE_CAMERA},{{0xAC,0x9F,0xC3},SURVEILLANCE_CAMERA},
    {{0xC4,0xDB,0xAD},SURVEILLANCE_CAMERA},{{0xCC,0x3B,0xFB},SURVEILLANCE_CAMERA},
    {{0x00,0xB4,0x63},SURVEILLANCE_CAMERA},
    {{0x7C,0x2A,0x9E},SURVEILLANCE_SMART_GLASSES},{{0xCC,0x66,0x0A},SURVEILLANCE_SMART_GLASSES},
    {{0xF4,0x03,0x43},SURVEILLANCE_SMART_GLASSES},{{0x5C,0xE9,0x1E},SURVEILLANCE_SMART_GLASSES},
    {{0x98,0x59,0x49},SURVEILLANCE_SMART_GLASSES},{{0x98,0xDA,0x92},SURVEILLANCE_SMART_GLASSES},
};

static const name_sig_t name_sigs[] = {
    {"AXON",SURVEILLANCE_BODY_CAMERA},{"BODY",SURVEILLANCE_BODY_CAMERA},
    {"DJI",SURVEILLANCE_DRONE},{"PARROT",SURVEILLANCE_DRONE},{"SKYDIO",SURVEILLANCE_DRONE},
    {"RING",SURVEILLANCE_CAMERA},{"RAY-BAN",SURVEILLANCE_SMART_GLASSES},
    {"RAYBAN",SURVEILLANCE_SMART_GLASSES},{"VUZI",SURVEILLANCE_SMART_GLASSES},
    {"SMART GLASS",SURVEILLANCE_SMART_GLASSES},
};

static bool contains_ci(const char *s, const char *needle)
{
    if (!s || !needle) return false;
    size_t n = strlen(needle);
    for (; *s; ++s) {
        size_t i = 0;
        while (i < n && s[i] && toupper((unsigned char)s[i]) == toupper((unsigned char)needle[i])) ++i;
        if (i == n) return true;
    }
    return false;
}

surveillance_class_t surveillance_classify(const rf_device_t *o)
{
    if (!o) return SURVEILLANCE_NONE;
    /* BLE random/private addresses must never be interpreted as IEEE OUIs. */
    if (o->last_source != RF_SOURCE_BLE || o->address_type == 0) {
        for (size_t i = 0; i < sizeof(oui_sigs)/sizeof(oui_sigs[0]); ++i)
            if (!memcmp(o->address, oui_sigs[i].oui, 3)) return oui_sigs[i].cls;
    }
    if (o->has_name) {
        for (size_t i = 0; i < sizeof(name_sigs)/sizeof(name_sigs[0]); ++i)
            if (contains_ci(o->name, name_sigs[i].fragment)) return name_sigs[i].cls;
    }
    return SURVEILLANCE_NONE;
}

const char *surveillance_name(surveillance_class_t c)
{
    switch (c) {
        case SURVEILLANCE_FLOCK:return "Flock / ALPR"; case SURVEILLANCE_BODY_CAMERA:return "Body camera";
        case SURVEILLANCE_DRONE:return "Drone"; case SURVEILLANCE_CAMERA:return "Camera / doorbell";
        case SURVEILLANCE_SMART_GLASSES:return "Smart glasses"; case SURVEILLANCE_OTHER:return "Surveillance";
        default:return "None";
    }
}

char surveillance_morse_letter(surveillance_class_t c)
{
    switch (c) {
        case SURVEILLANCE_FLOCK:return 'F'; case SURVEILLANCE_BODY_CAMERA:return 'B'; case SURVEILLANCE_DRONE:return 'D';
        case SURVEILLANCE_CAMERA:return 'C'; case SURVEILLANCE_SMART_GLASSES:return 'G'; case SURVEILLANCE_OTHER:return 'S';
        default:return '\0';
    }
}

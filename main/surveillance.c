#include "surveillance.h"

surveillance_class_t surveillance_classify(const rf_device_t *observation)
{
    /*
     * Deliberately conservative baseline. Mature OUI-SPY signatures will be
     * migrated here. A generic vendor/OUI match alone must not label a device
     * as surveillance hardware when that vendor makes unrelated products.
     */
    (void)observation;
    return SURVEILLANCE_NONE;
}

const char *surveillance_name(surveillance_class_t classification)
{
    switch (classification) {
        case SURVEILLANCE_FLOCK: return "Flock / ALPR";
        case SURVEILLANCE_BODY_CAMERA: return "Body camera";
        case SURVEILLANCE_DRONE: return "Drone";
        case SURVEILLANCE_CAMERA: return "Camera / doorbell";
        case SURVEILLANCE_SMART_GLASSES: return "Smart glasses";
        case SURVEILLANCE_OTHER: return "Surveillance";
        default: return "None";
    }
}

char surveillance_morse_letter(surveillance_class_t classification)
{
    switch (classification) {
        case SURVEILLANCE_FLOCK: return 'F';
        case SURVEILLANCE_BODY_CAMERA: return 'B';
        case SURVEILLANCE_DRONE: return 'D';
        case SURVEILLANCE_CAMERA: return 'C';
        case SURVEILLANCE_SMART_GLASSES: return 'G';
        case SURVEILLANCE_OTHER: return 'S';
        default: return '\0';
    }
}

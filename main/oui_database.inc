/* Bootstrap table. Run tools/update_oui.py to replace with the IEEE registry. */
static const oui_record_t oui_records[] = {
    { 0x00000C, "Cisco Systems" },
    { 0x001124, "Apple" },
    { 0x0017F2, "Apple" },
    { 0x001A11, "Google" },
    { 0x001B63, "Apple" },
    { 0x002500, "Apple" },
    { 0x0026BB, "Apple" },
    { 0x04D3CF, "Apple" },
    { 0x3C5AB4, "Google" },
    { 0xB827EB, "Raspberry Pi" },
};
#define OUI_RECORD_COUNT (sizeof(oui_records) / sizeof(oui_records[0]))

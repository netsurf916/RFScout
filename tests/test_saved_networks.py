"""Host regression fixtures for the shared credential store: converting version 2 records without
losing passwords, learning BSSIDs, and forgetting the oldest when a profile is full."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WIRELESS = ROOT / "main/services/wireless"
STORE = WIRELESS / "saved_networks.c"

STUBS = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_RESPONSE 0x108
const char *esp_err_to_name( esp_err_t a_error );
""",
    "esp_log.h": """#pragma once
#define ESP_LOGW( tag, ... ) ( (void)( tag ) )
#define ESP_LOGI( tag, ... ) ( (void)( tag ) )
""",
    "app_descriptor.h": """#pragma once
typedef enum { RFSCOUT_APP_DEVICE = 0 } rfscout_app_id_t;
""",
    "memory_manager.h": """#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef enum { RFSCOUT_MEMORY_OWNER_PLATFORM = 0 } rfscout_memory_owner_class_t;
typedef struct { rfscout_memory_owner_class_t owner_class; } rfscout_memory_owner_t;
void *memory_manager_calloc( rfscout_memory_owner_t a_owner, size_t a_count, size_t a_size );
bool memory_manager_free( rfscout_memory_owner_t a_owner, void *a_pointer );
""",
    "persistent_store.h": """#pragma once
#include "app_descriptor.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#define RFSCOUT_PERSISTENT_RECORD_MAX_BYTES 16384U
typedef bool ( *persistent_record_valid_fn )( const void *a_record );
esp_err_t persistent_store_app_write( rfscout_app_id_t a_app, const char *a_key,
                                      const void *a_data, size_t a_length );
esp_err_t persistent_store_app_read( rfscout_app_id_t a_app, const char *a_key, void *a_data,
                                     size_t a_capacity, size_t *a_length );
esp_err_t persistent_store_app_load( rfscout_app_id_t a_app, const char *a_key, void *a_record,
                                     size_t a_length, persistent_record_valid_fn a_valid );
esp_err_t persistent_store_app_remove( rfscout_app_id_t a_app, const char *a_key );
""",
}

HARNESS = r"""
#undef NDEBUG
#include "saved_networks.h"
#include "memory_manager.h"
#include "persistent_store.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WPA2 3
#define WPA3 6

/* ---- One-record in-memory store with the production load semantics. ---- */
static unsigned char blob[ 16384 ];
static size_t blob_length;
static bool blob_present;
static unsigned writes;

const char *esp_err_to_name( esp_err_t a_error )
{
    (void)a_error;
    return "error";
}

void *memory_manager_calloc( rfscout_memory_owner_t a_owner, size_t a_count, size_t a_size )
{
    (void)a_owner;
    return calloc( a_count, a_size );
}

bool memory_manager_free( rfscout_memory_owner_t a_owner, void *a_pointer )
{
    (void)a_owner;
    free( a_pointer );
    return true;
}

esp_err_t persistent_store_app_write( rfscout_app_id_t a_app, const char *a_key,
                                      const void *a_data, size_t a_length )
{
    (void)a_app;
    (void)a_key;
    assert( a_length <= sizeof( blob ) );
    memcpy( blob, a_data, a_length );
    blob_length = a_length;
    blob_present = true;
    ++writes;
    return ESP_OK;
}

esp_err_t persistent_store_app_read( rfscout_app_id_t a_app, const char *a_key, void *a_data,
                                     size_t a_capacity, size_t *a_length )
{
    (void)a_app;
    (void)a_key;
    if( !blob_present )
    {
        return ESP_ERR_NOT_FOUND;
    }
    *a_length = blob_length;
    if( NULL == a_data && 0U == a_capacity )
    {
        return ESP_OK;
    }
    if( NULL == a_data || blob_length > a_capacity )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy( a_data, blob, blob_length );
    return ESP_OK;
}

esp_err_t persistent_store_app_remove( rfscout_app_id_t a_app, const char *a_key )
{
    (void)a_app;
    (void)a_key;
    blob_present = false;
    blob_length = 0U;
    return ESP_OK;
}

esp_err_t persistent_store_app_load( rfscout_app_id_t a_app, const char *a_key, void *a_record,
                                     size_t a_length, persistent_record_valid_fn a_valid )
{
    if( !blob_present )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( blob_length != a_length )
    {
        (void)persistent_store_app_remove( a_app, a_key );
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy( a_record, blob, a_length );
    if( NULL != a_valid && !a_valid( a_record ) )
    {
        (void)persistent_store_app_remove( a_app, a_key );
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

/* ---- Version 2 layout as written through firmware v0.4.0. ---- */
typedef struct
{
    uint8_t address[ 6 ];
    uint8_t state;
    int32_t auth_mode;
} v2_bssid_t;

typedef struct
{
    uint32_t profile_id;
    int32_t auth_mode;
    uint8_t bssid_count;
    v2_bssid_t bssids[ 32 ];
    char ssid[ 33 ];
    char password[ 65 ];
} v2_entry_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    v2_entry_t entries[ 8 ];
} v2_record_t;

static const uint8_t A[ 6 ] = { 0xA0, 0, 0, 0, 0, 1 };
static const uint8_t B[ 6 ] = { 0xA0, 0, 0, 0, 0, 2 };
static const uint8_t C[ 6 ] = { 0xA0, 0, 0, 0, 0, 3 };

static void add_v2_bssid( v2_entry_t *a_entry, const uint8_t a_mac[ 6 ], uint8_t a_state, int32_t a_auth )
{
    v2_bssid_t *bssid = &a_entry->bssids[ a_entry->bssid_count++ ];
    memcpy( bssid->address, a_mac, 6U );
    bssid->state = a_state;
    bssid->auth_mode = a_auth;
}

static void put_v2( uint32_t a_magic )
{
    static v2_record_t legacy;
    memset( &legacy, 0, sizeof( legacy ) );
    legacy.magic = a_magic;
    legacy.version = 2U;
    legacy.count = 2U;
    legacy.entries[ 0 ].profile_id = 7U;
    legacy.entries[ 0 ].auth_mode = WPA2;
    strcpy( legacy.entries[ 0 ].ssid, "CAFE" );
    strcpy( legacy.entries[ 0 ].password, "first-secret" );
    add_v2_bssid( &legacy.entries[ 0 ], A, 1U, WPA2 );
    add_v2_bssid( &legacy.entries[ 0 ], B, 2U, WPA2 ); /* rejected */
    add_v2_bssid( &legacy.entries[ 0 ], C, 1U, WPA2 );
    legacy.entries[ 1 ].profile_id = 9U;
    legacy.entries[ 1 ].auth_mode = SAVED_NETWORK_AUTH_UNKNOWN;
    strcpy( legacy.entries[ 1 ].ssid, "CAFE" );
    strcpy( legacy.entries[ 1 ].password, "second-secret" );
    add_v2_bssid( &legacy.entries[ 1 ], A, 2U, WPA2 ); /* rejected */
    (void)persistent_store_app_write( RFSCOUT_APP_DEVICE, "networks", &legacy, sizeof( legacy ) );
}

static void conversion( void )
{
    assert( 3976U == sizeof( v2_record_t ) );
    put_v2( 0x4f54414eU );
    static saved_network_profile_t profiles[ SAVED_NETWORK_MAX_COUNT ];
    size_t count = 0U;
    assert( ESP_OK == saved_networks_list( profiles, SAVED_NETWORK_MAX_COUNT, &count ) );
    assert( 2U == count );
    assert( 7U == profiles[ 0 ].profile_id && WPA2 == profiles[ 0 ].auth_mode );
    assert( 2U == profiles[ 0 ].bssid_count && profiles[ 0 ].has_password );
    assert( 1 == profiles[ 0 ].ordinal && 2 == profiles[ 1 ].ordinal );
    assert( NULL != saved_network_profile_bssid( &profiles[ 0 ], A ) );
    assert( NULL == saved_network_profile_bssid( &profiles[ 0 ], B ) );
    assert( NULL != saved_network_profile_bssid( &profiles[ 0 ], C ) );
    assert( WPA2 == saved_network_profile_bssid( &profiles[ 0 ], C )->auth_mode );
    assert( 0U == profiles[ 1 ].bssid_count );

    /* The converted record replaced the old one, once. */
    assert( 3976U != blob_length );
    const unsigned written = writes;
    char password[ 65 ];
    assert( ESP_OK == saved_networks_password( 7U, password, sizeof( password ) ) );
    assert( 0 == strcmp( password, "first-secret" ) );
    assert( ESP_OK == saved_networks_password( 9U, password, sizeof( password ) ) );
    assert( 0 == strcmp( password, "second-secret" ) );
    assert( written == writes );
}

/** A record of the old size that is not a valid version 2 record is deleted. */
static void bad_legacy_record( void )
{
    put_v2( 0x12345678U );
    static saved_network_profile_t profiles[ SAVED_NETWORK_MAX_COUNT ];
    size_t count = 9U;
    assert( ESP_OK == saved_networks_list( profiles, SAVED_NETWORK_MAX_COUNT, &count ) );
    assert( 0U == count && !blob_present );
}

/** A full profile forgets its oldest BSSID and keeps learning. */
static void learning( void )
{
    uint32_t first = 0U;
    uint32_t second = 0U;
    assert( ESP_OK == saved_networks_create( "CAFE", "first-secret", &first ) );
    assert( ESP_OK == saved_networks_create( "CAFE", "second-secret", &second ) );
    static saved_network_profile_t profile;
    uint8_t mac[ 6 ] = { 0xB0, 0, 0, 0, 0, 0 };
    for( unsigned index = 0U; index <= SAVED_NETWORK_BSSID_MAX_COUNT; ++index )
    {
        mac[ 5 ] = (uint8_t)index;
        assert( ESP_OK == saved_networks_record_bssid( first, mac, WPA2 ) );
    }
    assert( ESP_OK == saved_networks_get( first, &profile ) );
    assert( SAVED_NETWORK_BSSID_MAX_COUNT == profile.bssid_count );
    mac[ 5 ] = 0U;
    assert( NULL == saved_network_profile_bssid( &profile, mac ) );
    mac[ 5 ] = 1U;
    assert( &profile.bssids[ 0 ] == saved_network_profile_bssid( &profile, mac ) );
    mac[ 5 ] = (uint8_t)SAVED_NETWORK_BSSID_MAX_COUNT;
    assert( NULL != saved_network_profile_bssid( &profile, mac ) );

    /* Learning a known BSSID again writes nothing; another profile cannot claim it. */
    const unsigned written = writes;
    assert( ESP_OK == saved_networks_record_bssid( first, mac, WPA2 ) );
    assert( written == writes );
    assert( ESP_ERR_INVALID_STATE == saved_networks_record_bssid( second, mac, WPA2 ) );

    /* A new password clears what the old one taught. */
    assert( ESP_OK == saved_networks_set_password( first, "changed-secret" ) );
    assert( ESP_OK == saved_networks_get( first, &profile ) );
    assert( 0U == profile.bssid_count && SAVED_NETWORK_AUTH_UNKNOWN == profile.auth_mode );
}

int main( int argc, char **argv )
{
    if( 2 != argc )
    {
        return 2;
    }
    if( 0 == strcmp( argv[ 1 ], "conversion" ) )
    {
        conversion();
    }
    else if( 0 == strcmp( argv[ 1 ], "bad_legacy_record" ) )
    {
        bad_legacy_record();
    }
    else if( 0 == strcmp( argv[ 1 ], "learning" ) )
    {
        learning();
    }
    else
    {
        return 2;
    }
    return 0;
}
"""


class SavedNetworksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("C compiler unavailable")
        cls.folder = tempfile.TemporaryDirectory()
        temp = pathlib.Path(cls.folder.name)
        for name, text in STUBS.items():
            (temp / name).write_text(text)
        source = temp / "saved_networks_harness.c"
        source.write_text(HARNESS)
        cls.binary = temp / "saved_networks_harness"
        try:
            # strnlen() is POSIX, not ISO C11.
            subprocess.run(
                [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-I", str(temp), "-I", str(WIRELESS), str(source), str(STORE),
                 "-o", str(cls.binary)], check=True
            )
        except BaseException:
            cls.folder.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_scenario(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_version_2_record_converts_keeping_passwords(self):
        self.run_scenario("conversion")

    def test_invalid_legacy_record_is_deleted(self):
        self.run_scenario("bad_legacy_record")

    def test_full_profile_forgets_oldest_bssid(self):
        self.run_scenario("learning")


if __name__ == "__main__":
    unittest.main()

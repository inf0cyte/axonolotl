#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <extra_beacon.h>

#define TAG "Axonolotl"

/*
 * PROTOCOL DATA EXTRACTED FROM:
 * AxonCadabra/app/src/main/java/com/axon/blecontroller/MainActivity.kt
 * 
 * Lines 36-48 of MainActivity.kt:
 * 
 *   companion object {
 *       private const val TAG = "AxonCadabra"
 *       private const val TARGET_OUI = "00:25:DF"
 *       // Service UUID 0xFE6C
 *       private val SERVICE_UUID = UUID.fromString("0000FE6C-0000-1000-8000-00805F9B34FB")
 *       // Base service data from screenshot: 01583837303032465034010200000000CE1B330000020000
 *       private val BASE_SERVICE_DATA = byteArrayOf(
 *           0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,
 *           0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
 *           0xCE.toByte(), 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00
 *       )
 *       private const val FUZZ_INTERVAL_MS = 500L
 *   }
 */

// Service UUID: 0xFE6C (from line 40)
#define AXON_SERVICE_UUID_16 0xFE6C

// Target OUI for Axon cameras (from line 38)
// Used for scanning - Axon devices have MAC starting with 00:25:DF
#define AXON_TARGET_OUI "00:25:DF"

// Service data length (from lines 42-46)
#define AXON_SERVICE_DATA_LEN 24

// Base service data - EXACT bytes from MainActivity.kt lines 42-46
// Hex: 01583837303032465034010200000000CE1B330000020000
static const uint8_t AXON_BASE_SERVICE_DATA[AXON_SERVICE_DATA_LEN] = {
    0x01, 0x58, 0x38, 0x37, 0x30, 0x30, 0x32, 0x46,  // Bytes 0-7
    0x50, 0x34, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,  // Bytes 8-15
    0xCE, 0x1B, 0x33, 0x00, 0x00, 0x02, 0x00, 0x00   // Bytes 16-23
};

// Fuzz interval from line 47: FUZZ_INTERVAL_MS = 500L
#define FUZZ_INTERVAL_MS 500

/*
 * Fuzz byte positions from updateServiceDataWithFuzz() at lines 303-321:
 * 
 *   currentServiceData[10] = ((fuzzValue shr 8) and 0xFF).toByte()
 *   currentServiceData[11] = (fuzzValue and 0xFF).toByte()
 *   currentServiceData[20] = ((fuzzValue shr 4) and 0xFF).toByte()
 *   currentServiceData[21] = ((fuzzValue shl 4) and 0xFF).toByte()
 */
#define FUZZ_BYTE_10 10
#define FUZZ_BYTE_11 11
#define FUZZ_BYTE_20 20
#define FUZZ_BYTE_21 21

// Scenes
typedef enum {
    AxonolotlSceneMenu,
    AxonolotlSceneTx,
    AxonolotlSceneFuzz,
    AxonolotlSceneAbout,
    AxonolotlSceneCount,
} AxonolotlScene;

// Views
typedef enum {
    AxonolotlViewSubmenu,
    AxonolotlViewPopup,
    AxonolotlViewWidget,
} AxonolotlView;

// Custom events
typedef enum {
    AxonolotlEventFuzzTick,
} AxonolotlEvent;

// Application state
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    
    Submenu* submenu;
    Popup* popup;
    Widget* widget;
    
    bool is_advertising;
    bool is_fuzzing;
    uint16_t fuzz_value;  // 16-bit counter (masked with 0xFFFF in source)
    uint8_t current_data[AXON_SERVICE_DATA_LEN];
    
    FuriTimer* fuzz_timer;
} Axonolotl;

// Function declarations
Axonolotl* axonolotl_alloc(void);
void axonolotl_free(Axonolotl* app);

bool axonolotl_start_advertising(Axonolotl* app);
void axonolotl_stop_advertising(Axonolotl* app);
void axonolotl_update_fuzz_data(Axonolotl* app);

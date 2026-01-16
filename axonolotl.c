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
 */

#include "axonolotl.h"
#include <furi_hal_random.h>

// Forward declarations for scene handlers
void axonolotl_scene_menu_on_enter(void* context);
bool axonolotl_scene_menu_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_menu_on_exit(void* context);

void axonolotl_scene_tx_on_enter(void* context);
bool axonolotl_scene_tx_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_tx_on_exit(void* context);

void axonolotl_scene_fuzz_on_enter(void* context);
bool axonolotl_scene_fuzz_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_fuzz_on_exit(void* context);

void axonolotl_scene_about_on_enter(void* context);
bool axonolotl_scene_about_on_event(void* context, SceneManagerEvent event);
void axonolotl_scene_about_on_exit(void* context);

// Scene handlers table
void (*const axonolotl_scene_on_enter_handlers[])(void*) = {
    axonolotl_scene_menu_on_enter,
    axonolotl_scene_tx_on_enter,
    axonolotl_scene_fuzz_on_enter,
    axonolotl_scene_about_on_enter,
};

bool (*const axonolotl_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    axonolotl_scene_menu_on_event,
    axonolotl_scene_tx_on_event,
    axonolotl_scene_fuzz_on_event,
    axonolotl_scene_about_on_event,
};

void (*const axonolotl_scene_on_exit_handlers[])(void*) = {
    axonolotl_scene_menu_on_exit,
    axonolotl_scene_tx_on_exit,
    axonolotl_scene_fuzz_on_exit,
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
 * Update service data with fuzz values
 * 
 * From MainActivity.kt lines 303-321:
 * 
 *   private fun updateServiceDataWithFuzz() {
 *       currentServiceData = BASE_SERVICE_DATA.copyOf()
 *       currentServiceData[10] = ((fuzzValue shr 8) and 0xFF).toByte()
 *       currentServiceData[11] = (fuzzValue and 0xFF).toByte()
 *       currentServiceData[20] = ((fuzzValue shr 4) and 0xFF).toByte()
 *       currentServiceData[21] = ((fuzzValue shl 4) and 0xFF).toByte()
 *   }
 */
void axonolotl_update_fuzz_data(Axonolotl* app) {
    furi_assert(app);
    
    // Copy base data
    memcpy(app->current_data, AXON_BASE_SERVICE_DATA, AXON_SERVICE_DATA_LEN);
    
    // Apply fuzz mutations exactly as in original source
    app->current_data[FUZZ_BYTE_10] = (app->fuzz_value >> 8) & 0xFF;
    app->current_data[FUZZ_BYTE_11] = app->fuzz_value & 0xFF;
    app->current_data[FUZZ_BYTE_20] = (app->fuzz_value >> 4) & 0xFF;
    app->current_data[FUZZ_BYTE_21] = (app->fuzz_value << 4) & 0xFF;
}

/*
 * Start BLE advertising
 * 
 * From MainActivity.kt lines 355-380:
 * Uses Service Data (not manufacturer data) with UUID 0xFE6C
 */
bool axonolotl_start_advertising(Axonolotl* app) {
    furi_assert(app);
    
    if(app->is_advertising) {
        axonolotl_stop_advertising(app);
    }
    
    // Configure beacon
    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 100,
        .max_adv_interval_ms = 150,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypeRandom,
    };
    
    // Random MAC address
    furi_hal_random_fill_buf(config.address, 6);
    config.address[0] |= 0xC0;
    
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
    adv_data[pos++] = AXON_SERVICE_UUID_16 & 0xFF;         // UUID low byte
    adv_data[pos++] = (AXON_SERVICE_UUID_16 >> 8) & 0xFF;  // UUID high byte
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
    
    app->is_advertising = true;
    FURI_LOG_I(TAG, "TX Started");
    return true;
}

void axonolotl_stop_advertising(Axonolotl* app) {
    furi_assert(app);
    
    if(app->is_advertising) {
        furi_hal_bt_extra_beacon_stop();
        app->is_advertising = false;
        FURI_LOG_I(TAG, "TX Stopped");
    }
}

/*
 * Fuzz timer callback
 * 
 * From MainActivity.kt lines 276-296 (startFuzzLoop):
 *   fuzzValue = (fuzzValue + 1) and 0xFFFF
 */
static void axonolotl_fuzz_timer_callback(void* context) {
    Axonolotl* app = context;
    
    // Increment fuzz value with 16-bit wrap
    app->fuzz_value = (app->fuzz_value + 1) & 0xFFFF;
    
    // Update data and restart advertising
    axonolotl_update_fuzz_data(app);
    axonolotl_stop_advertising(app);
    axonolotl_start_advertising(app);
    
    // Trigger UI update
    view_dispatcher_send_custom_event(app->view_dispatcher, AxonolotlEventFuzzTick);
}

// ============================================================================
// Scene: Menu
// ============================================================================

typedef enum {
    MenuIndexTrigger,
    MenuIndexFuzz,
    MenuIndexAbout,
} MenuIndex;

static void axonolotl_menu_callback(void* context, uint32_t index) {
    Axonolotl* app = context;
    switch(index) {
        case MenuIndexTrigger:
            scene_manager_next_scene(app->scene_manager, AxonolotlSceneTx);
            break;
        case MenuIndexFuzz:
            scene_manager_next_scene(app->scene_manager, AxonolotlSceneFuzz);
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
    submenu_add_item(app->submenu, "Trigger", MenuIndexTrigger, axonolotl_menu_callback, app);
    submenu_add_item(app->submenu, "Fuzz Mode", MenuIndexFuzz, axonolotl_menu_callback, app);
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
// Scene: TX (Single broadcast)
// ============================================================================

void axonolotl_scene_tx_on_enter(void* context) {
    Axonolotl* app = context;
    
    // Use base service data (no fuzz)
    memcpy(app->current_data, AXON_BASE_SERVICE_DATA, AXON_SERVICE_DATA_LEN);
    
    if(axonolotl_start_advertising(app)) {
        popup_set_header(app->popup, "TX ACTIVE", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Broadcasting\nAxon Signal...\nPress Back to stop", 64, 26, AlignCenter, AlignTop);
        notification_message(app->notifications, &sequence_set_only_green_255);
    } else {
        popup_set_header(app->popup, "TX FAILED", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Could not start\nBLE advertising", 64, 26, AlignCenter, AlignTop);
        notification_message(app->notifications, &sequence_set_only_red_255);
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonolotlViewPopup);
}

bool axonolotl_scene_tx_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void axonolotl_scene_tx_on_exit(void* context) {
    Axonolotl* app = context;
    axonolotl_stop_advertising(app);
    notification_message(app->notifications, &sequence_reset_rgb);
    popup_reset(app->popup);
}

// ============================================================================
// Scene: Fuzz Mode
// ============================================================================

static void axonolotl_update_fuzz_display(Axonolotl* app) {
    static char text[80];
    snprintf(text, sizeof(text), 
        "Value: 0x%04X\n"
        "Interval: 500ms\n"
        "Press Back to stop", 
        app->fuzz_value);
    popup_set_text(app->popup, text, 64, 26, AlignCenter, AlignTop);
}

void axonolotl_scene_fuzz_on_enter(void* context) {
    Axonolotl* app = context;
    
    // Initialize fuzz state (from line 265: fuzzValue = 0)
    app->fuzz_value = 0;
    app->is_fuzzing = true;
    axonolotl_update_fuzz_data(app);
    
    if(axonolotl_start_advertising(app)) {
        popup_set_header(app->popup, "FUZZ ACTIVE", 64, 8, AlignCenter, AlignTop);
        axonolotl_update_fuzz_display(app);
        
        // Start fuzz timer (500ms interval from source)
        furi_timer_start(app->fuzz_timer, furi_ms_to_ticks(FUZZ_INTERVAL_MS));
        
        // Magenta LED for fuzz mode
        notification_message(app->notifications, &sequence_set_only_blue_255);
        notification_message(app->notifications, &sequence_set_only_red_255);
    } else {
        popup_set_header(app->popup, "FUZZ FAILED", 64, 8, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Could not start\nBLE advertising", 64, 26, AlignCenter, AlignTop);
        notification_message(app->notifications, &sequence_set_only_red_255);
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, AxonolotlViewPopup);
}

bool axonolotl_scene_fuzz_on_event(void* context, SceneManagerEvent event) {
    Axonolotl* app = context;
    
    if(event.type == SceneManagerEventTypeCustom && event.event == AxonolotlEventFuzzTick) {
        axonolotl_update_fuzz_display(app);
        return true;
    }
    return false;
}

void axonolotl_scene_fuzz_on_exit(void* context) {
    Axonolotl* app = context;
    
    furi_timer_stop(app->fuzz_timer);
    app->is_fuzzing = false;
    axonolotl_stop_advertising(app);
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
    app->is_advertising = false;
    app->is_fuzzing = false;
    app->fuzz_value = 0;
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
    
    // Create fuzz timer
    app->fuzz_timer = furi_timer_alloc(axonolotl_fuzz_timer_callback, FuriTimerTypePeriodic, app);
    
    return app;
}

void axonolotl_free(Axonolotl* app) {
    furi_assert(app);
    
    // Stop operations
    furi_timer_stop(app->fuzz_timer);
    axonolotl_stop_advertising(app);
    
    // Free timer
    furi_timer_free(app->fuzz_timer);
    
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

#include "ui_PomodoroPage.h"

///////////////////// VARIABLES ////////////////////

// Pomodoro states
typedef enum {
    STATE_WORK,
    STATE_SHORT_BREAK,
    STATE_LONG_BREAK
} pomodoro_state_t;

// Duration constants (seconds)
#define WORK_DURATION         (25 * 60)
#define SHORT_BREAK_DURATION  (5 * 60)
#define LONG_BREAK_DURATION   (15 * 60)
#define ROUNDS_BEFORE_LONG    4

// Page objects
static lv_obj_t * ui_pomodoro_arc;
static lv_obj_t * ui_time_label;
static lv_obj_t * ui_mode_label;
static lv_obj_t * ui_round_label;
static lv_obj_t * ui_start_btn;
static lv_obj_t * ui_start_label;
static lv_obj_t * ui_reset_btn;
static lv_timer_t * ui_pomodoro_timer;

// State
static pomodoro_state_t current_state;
static int remaining_seconds;
static int completed_rounds;
static bool is_running;
static int total_seconds;
static int second_tick;

///////////////////// FUNCTIONS ////////////////////

static const char * _get_mode_text(pomodoro_state_t state)
{
    switch (state) {
        case STATE_WORK:        return "专注";
        case STATE_SHORT_BREAK: return "短休息";
        case STATE_LONG_BREAK:  return "长休息";
        default:                return "";
    }
}

static void _update_display(void)
{
    // Update arc
    lv_arc_set_value(ui_pomodoro_arc, total_seconds - remaining_seconds);

    // Update time label
    int mins = remaining_seconds / 60;
    int secs = remaining_seconds % 60;
    char time_str[6];
    sprintf(time_str, "%02d:%02d", mins, secs);
    lv_label_set_text(ui_time_label, time_str);

    // Update mode label
    lv_label_set_text(ui_mode_label, _get_mode_text(current_state));

    // Update round label
    char round_str[16];
    sprintf(round_str, "第 %d/%d 轮", completed_rounds + 1, ROUNDS_BEFORE_LONG);
    lv_label_set_text(ui_round_label, round_str);

    // Update start button text
    lv_label_set_text(ui_start_label, is_running ? "暂停" : "开始");
}

static void _switch_state(void)
{
    switch (current_state) {
        case STATE_WORK:
            completed_rounds++;
            if (completed_rounds >= ROUNDS_BEFORE_LONG) {
                current_state = STATE_LONG_BREAK;
                total_seconds = LONG_BREAK_DURATION;
                completed_rounds = 0;
            } else {
                current_state = STATE_SHORT_BREAK;
                total_seconds = SHORT_BREAK_DURATION;
            }
            break;

        case STATE_SHORT_BREAK:
        case STATE_LONG_BREAK:
            current_state = STATE_WORK;
            total_seconds = WORK_DURATION;
            break;
    }

    remaining_seconds = total_seconds;
    lv_arc_set_range(ui_pomodoro_arc, 0, total_seconds);
    lv_arc_set_value(ui_pomodoro_arc, 0);

    _update_display();
}

static void _handle_time_up(void)
{
    // Auto-transition to next state
    _switch_state();

    // Show notification popup
    const char * next_mode = _get_mode_text(current_state);
    char msg[64];
    sprintf(msg, "时间到！进入: %s", next_mode);
    ui_msgbox_info("番茄时钟", msg);

    // Auto-start the timer running for the next state
    is_running = true;
}

static void _pomodoro_timer_cb(lv_timer_t * timer)
{
    second_tick++;

    // Timer period is 100ms, so 10 ticks = 1 second
    if (second_tick < 10) return;
    second_tick = 0;

    if (!is_running) return;

    if (remaining_seconds > 0) {
        remaining_seconds--;
        _update_display();

        if (remaining_seconds == 0) {
            _handle_time_up();
        }
    }
}

static void _on_start_btn_click(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        is_running = !is_running;
        _update_display();
    }
}

static void _on_reset_btn_click(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        is_running = false;
        remaining_seconds = total_seconds;
        lv_arc_set_value(ui_pomodoro_arc, 0);
        second_tick = 0;
        _update_display();
    }
}

static void _gesture_cb(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_GESTURE) {
        lv_indev_wait_release(lv_indev_get_act());
        if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
            lv_lib_pm_OpenPrePage(&page_manager);
        }
    }
}

///////////////////// SCREEN init ////////////////////

void ui_PomodoroPage_init(void)
{
    // Initialize state
    current_state = STATE_WORK;
    total_seconds = WORK_DURATION;
    remaining_seconds = total_seconds;
    completed_rounds = 0;
    is_running = false;
    second_tick = 0;

    // Create screen
    lv_obj_t * ui_PomodoroScreen = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_PomodoroScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_PomodoroScreen, lv_color_hex(0x1A1A2E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_PomodoroScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Gesture: swipe right to go back
    lv_obj_add_event_cb(ui_PomodoroScreen, _gesture_cb, LV_EVENT_GESTURE, NULL);

    // === Mode label (top) ===
    ui_mode_label = lv_label_create(ui_PomodoroScreen);
    lv_obj_set_width(ui_mode_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_mode_label, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_mode_label, 0);
    lv_obj_set_y(ui_mode_label, -95);
    lv_obj_set_align(ui_mode_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_mode_label, lv_color_hex(0xE94560), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_mode_label, &ui_font_heiti22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_mode_label, "专注");

    // === Round label ===
    ui_round_label = lv_label_create(ui_PomodoroScreen);
    lv_obj_set_width(ui_round_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_round_label, LV_SIZE_CONTENT);
    lv_obj_set_x(ui_round_label, 0);
    lv_obj_set_y(ui_round_label, -70);
    lv_obj_set_align(ui_round_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(ui_round_label, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_round_label, &ui_font_heiti14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_round_label, "第 1/4 轮");

    // === Circular progress arc ===
    ui_pomodoro_arc = lv_arc_create(ui_PomodoroScreen);
    lv_obj_set_size(ui_pomodoro_arc, 150, 150);
    lv_obj_center(ui_pomodoro_arc);
    lv_arc_set_range(ui_pomodoro_arc, 0, total_seconds);
    lv_arc_set_value(ui_pomodoro_arc, 0);
    lv_arc_set_rotation(ui_pomodoro_arc, 270);
    lv_arc_set_bg_angles(ui_pomodoro_arc, 0, 360);
    lv_obj_remove_style(ui_pomodoro_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ui_pomodoro_arc, LV_OBJ_FLAG_CLICKABLE);

    // Arc background (track) style
    lv_obj_set_style_arc_color(ui_pomodoro_arc, lv_color_hex(0x333355), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_pomodoro_arc, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_pomodoro_arc, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Arc indicator (progress) style
    lv_obj_set_style_arc_color(ui_pomodoro_arc, lv_color_hex(0xE94560), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_pomodoro_arc, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_pomodoro_arc, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // === Time label (center of arc) ===
    ui_time_label = lv_label_create(ui_PomodoroScreen);
    lv_obj_set_width(ui_time_label, LV_SIZE_CONTENT);
    lv_obj_set_height(ui_time_label, LV_SIZE_CONTENT);
    lv_obj_center(ui_time_label);
    lv_obj_set_style_text_color(ui_time_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_time_label, &ui_font_NuberBig90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(ui_time_label, "25:00");

    // === Start / Pause button ===
    ui_start_btn = lv_button_create(ui_PomodoroScreen);
    lv_obj_set_width(ui_start_btn, 90);
    lv_obj_set_height(ui_start_btn, 36);
    lv_obj_set_x(ui_start_btn, -50);
    lv_obj_set_y(ui_start_btn, 88);
    lv_obj_set_align(ui_start_btn, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(ui_start_btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_start_btn, lv_color_hex(0xE94560), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_start_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_start_label = lv_label_create(ui_start_btn);
    lv_obj_center(ui_start_label);
    lv_label_set_text(ui_start_label, "开始");
    lv_obj_set_style_text_color(ui_start_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_start_label, &ui_font_heiti22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_start_btn, _on_start_btn_click, LV_EVENT_CLICKED, NULL);

    // === Reset button ===
    ui_reset_btn = lv_button_create(ui_PomodoroScreen);
    lv_obj_set_width(ui_reset_btn, 90);
    lv_obj_set_height(ui_reset_btn, 36);
    lv_obj_set_x(ui_reset_btn, 50);
    lv_obj_set_y(ui_reset_btn, 88);
    lv_obj_set_align(ui_reset_btn, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(ui_reset_btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_reset_btn, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_reset_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_reset_label = lv_label_create(ui_reset_btn);
    lv_obj_center(ui_reset_label);
    lv_label_set_text(ui_reset_label, "重置");
    lv_obj_set_style_text_color(ui_reset_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_reset_label, &ui_font_heiti22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_reset_btn, _on_reset_btn_click, LV_EVENT_CLICKED, NULL);

    // === Timer: 100ms period (for LED blinking + 1-second countdown) ===
    ui_pomodoro_timer = lv_timer_create(_pomodoro_timer_cb, 100, NULL);

    // Load screen
    lv_scr_load_anim(ui_PomodoroScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);
}

/////////////////// SCREEN deinit ////////////////////

void ui_PomodoroPage_deinit(void)
{
    if (ui_pomodoro_timer) {
        lv_timer_delete(ui_pomodoro_timer);
        ui_pomodoro_timer = NULL;
    }
}

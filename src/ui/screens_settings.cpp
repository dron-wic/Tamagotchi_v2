#include "ui/screens.h"

#include "hw/audio.h"
#include "hw/camera.h"
#include "hw/net.h"
#include "hw/radio.h"
#include "hw/sdcard.h"
#include "hw/usb_msc.h"
#include "pet/pet.h"
#include "storage/save.h"
#include "ui/daily.h"
#include "ui/font_ru.h"
#include "ui/prefs.h"
#include "ui/ui.h"
#include "ui/ui_common.h"
#include "version.h"

#include <Arduino.h>
#include <stdio.h>

static lv_obj_t *set_scr = nullptr;
enum SetPage : uint8_t { PAGE_MENU, PAGE_SCREEN, PAGE_PET, PAGE_NET, PAGE_KEYS, PAGE_DEVICE };
static SetPage page = PAGE_MENU;
static lv_obj_t *btn_light = nullptr;
static lv_obj_t *btn_dark = nullptr;
static lv_obj_t *lbl_bl = nullptr;
static lv_obj_t *lbl_dim = nullptr;
static lv_obj_t *lbl_off = nullptr;
static lv_obj_t *btn_led = nullptr;
static lv_obj_t *btn_usb = nullptr;
static lv_obj_t *btn_fps = nullptr;
static lv_obj_t *btn_auto_ntp = nullptr;
static lv_obj_t *btn_night_pet = nullptr;
static lv_obj_t *btn_quiet = nullptr;
static lv_obj_t *btn_care = nullptr;
static lv_obj_t *lbl_pace = nullptr;
static lv_obj_t *lbl_key[3] = {};
static lv_obj_t *lbl_clock = nullptr;
static lv_obj_t *lbl_net = nullptr;
static uint32_t set_clock_next = 0;

static void paint_choice(lv_obj_t *btn, bool on)
{
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, lv_color_hex(on ? col.pressed : col.card), 0);
    lv_obj_set_style_border_width(btn, on ? 4 : 3, 0);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(col.ink), 0);
}

static void paint_theme()
{
    paint_choice(btn_light, save_theme() == 0);
    paint_choice(btn_dark, save_theme() == 1);
}

static void paint_led()
{
    bool on = save_led_flash() != 0;
    paint_choice(btn_led, on);
    lv_obj_t *lbl = btn_led ? lv_obj_get_child(btn_led, 0) : nullptr;
    if (lbl) lv_label_set_text(lbl, on ? "Включены" : "Выключены");
}

static void paint_usb()
{
    bool on = usb_msc_active();
    paint_choice(btn_usb, on);
    lv_obj_t *lbl = btn_usb ? lv_obj_get_child(btn_usb, 0) : nullptr;
    if (lbl) lv_label_set_text(lbl, on ? "USB вкл" : "USB выкл");
}

static void paint_fps()
{
    bool on = save_show_fps() != 0;
    paint_choice(btn_fps, on);
    lv_obj_t *lbl = btn_fps ? lv_obj_get_child(btn_fps, 0) : nullptr;
    if (lbl) lv_label_set_text(lbl, on ? "FPS включён" : "FPS выключен");
}

static void paint_daily_btn(lv_obj_t *btn, bool on, const char *on_t, const char *off_t)
{
    paint_choice(btn, on);
    lv_obj_t *lbl = btn ? lv_obj_get_child(btn, 0) : nullptr;
    if (lbl) lv_label_set_text(lbl, on ? on_t : off_t);
}

static void paint_daily()
{
    paint_daily_btn(btn_auto_ntp, daily_auto_ntp(), "Авто NTP вкл", "Авто NTP выкл");
    paint_daily_btn(btn_night_pet, daily_night_pet(), "Ночной сон вкл", "Ночной сон выкл");
    paint_daily_btn(btn_quiet, daily_quiet_night(), "Тихая ночь вкл", "Тихая ночь выкл");
    paint_daily_btn(btn_care, daily_care_notify(), "Уведомления вкл", "Уведомления выкл");
}

static void commit()
{
    prefs_apply();
    save_flush();
}

// Keep "screen off" later than "dim", unless one of them is "never".
static void keep_order(bool dim_moved)
{
    uint32_t dim = pref_dim_ms(save_dim_idx());
    uint32_t off = pref_off_ms(save_off_idx());
    if (dim == 0 || off == 0 || off > dim) return;
    if (dim_moved) {
        uint8_t o = save_off_idx();
        while (o + 1 < pref_off_count() && pref_off_ms(o) <= dim && pref_off_ms(o) != 0) o++;
        save_set_off_idx(o);
    } else {
        uint8_t d = save_dim_idx();
        while (d > 0 && pref_dim_ms(d) >= off) d--;
        save_set_dim_idx(d);
    }
}

static void paint_net()
{
    if (lbl_net) {
        char buf[64];
        if (radio_busy()) snprintf(buf, sizeof(buf), "%s", radio_status());
        else snprintf(buf, sizeof(buf), "%s", net_status());
        ui_set_text(lbl_net, buf);
    }
    if (!lbl_clock) return;
    char buf[24];
    if (net_format_clock(buf, sizeof(buf), true)) ui_set_text(lbl_clock, buf);
    else ui_set_text(lbl_clock, "Нет времени");
}

static void forget_widgets()
{
    btn_light = btn_dark = lbl_bl = lbl_dim = lbl_off = btn_led = btn_usb = btn_fps = nullptr;
    btn_auto_ntp = btn_night_pet = btn_quiet = btn_care = nullptr;
    lbl_pace = nullptr;
    lbl_key[0] = lbl_key[1] = lbl_key[2] = nullptr;
    lbl_clock = lbl_net = nullptr;
}

bool screen_settings_back()
{
    if (page == PAGE_MENU) return false;
    page = PAGE_MENU;
    return true;
}

static void on_set_del(lv_event_t *e)
{
    if (lv_event_get_target(e) != set_scr) return;
    set_scr = nullptr;
    page = PAGE_MENU;
    forget_widgets();
}

static void on_sync_time(lv_event_t *e)
{
    (void)e;
    if (net_busy()) {
        ui_toast("Уже занято");
        return;
    }
    if (!net_start_sync()) {
        ui_toast("Сначала Wi-Fi");
        paint_net();
        return;
    }
    ui_toast("Синхронизация...");
    paint_net();
}

static void on_wifi_setup(lv_event_t *e)
{
    (void)e;
    if (net_busy()) {
        ui_toast("Уже занято");
        return;
    }
    if (!net_start_portal()) {
        ui_toast("AP ошибка");
        paint_net();
        return;
    }
    ui_toast("TamaSetup");
    paint_net();
}

static void on_wifi_stop(lv_event_t *e)
{
    (void)e;
    net_stop();
    ui_toast("Wi-Fi выкл");
    paint_net();
}

static void on_status_page(lv_event_t *e)
{
    (void)e;
    if (net_busy()) {
        ui_toast("Уже занято");
        return;
    }
    if (!net_start_status()) {
        ui_toast("AP ошибка");
        paint_net();
        return;
    }
    ui_toast("TamaStatus");
    paint_net();
}

static void do_factory_reset()
{
    net_stop();
    radio_stop();
    net_clear_creds();
    usb_msc_forget();
    camera_photo_reset();
    camera_tune_reset();
    save_reset();
    prefs_apply();
    ui_toast("Перезагрузка");
    delay(400);
    ESP.restart();
}

static void on_ota(lv_event_t *e)
{
    (void)e;
    if (net_busy()) {
        ui_toast("Уже занято");
        paint_net();
        return;
    }
    if (!net_start_ota()) {
        ui_toast("AP ошибка");
        paint_net();
        return;
    }
    ui_toast("TamaUpdate");
    paint_net();
}

static void on_factory_reset(lv_event_t *e)
{
    (void)e;
    ui_confirm("Сброс устройства?\nСейв и Wi-Fi", do_factory_reset);
}

static void on_radio_listen(lv_event_t *e)
{
    (void)e;
    if (radio_busy()) {
        ui_toast("Уже слушаю");
        paint_net();
        return;
    }
    if (net_busy()) net_stop();
    if (!radio_start()) {
        ui_toast("ESP-NOW сбой");
        paint_net();
        return;
    }
    ui_toast("Жду друга");
    paint_net();
}

static void on_radio_gift(lv_event_t *e)
{
    (void)e;
    if (!radio_busy() && !radio_start()) {
        ui_toast("ESP-NOW сбой");
        paint_net();
        return;
    }
    if (!pet_spend_coins(5)) {
        ui_toast("Мало монет");
        return;
    }
    if (!radio_send_gift(5)) {
        pet_add_coins(5); // refund
        ui_toast("Не ушло");
    } else {
        ui_toast("Подарок 5");
    }
    paint_net();
}

static void on_radio_stop(lv_event_t *e)
{
    (void)e;
    radio_stop();
    ui_toast("ESP-NOW выкл");
    paint_net();
}

static void on_backup(lv_event_t *e)
{
    (void)e;
    if (!sd_ok()) {
        ui_toast(sd_unready_reason());
        return;
    }
    ui_toast(save_export_sd() ? "Бэкап готов" : "Ошибка записи");
}

static void do_restore()
{
    if (!sd_ok()) {
        ui_toast(sd_unready_reason());
        return;
    }
    if (!save_import_latest_sd()) {
        ui_toast("Нет бэкапа");
        return;
    }
    prefs_apply();
    ui_toast("Сейв с SD");
    ui_show(SCR_SETTINGS);
}

static void on_restore(lv_event_t *e)
{
    (void)e;
    ui_confirm("Восстановить\nсейв с SD?", do_restore);
}

static void on_theme(lv_event_t *e)
{
    uint8_t mode = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (mode == save_theme()) return;
    save_set_theme(mode);
    commit();
    ui_restyle_chrome();
    ui_show(SCR_SETTINGS);
}

static void step_bl(int dir)
{
    int n = (int)pref_bl_count();
    int i = (int)save_bl_idx() + dir;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    save_set_bl_idx((uint8_t)i);
    commit();
    if (lbl_bl) lv_label_set_text(lbl_bl, pref_bl_name(save_bl_idx()));
}

static void step_dim(int dir)
{
    int n = (int)pref_dim_count();
    int i = (int)save_dim_idx() + dir;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    save_set_dim_idx((uint8_t)i);
    keep_order(true);
    commit();
    if (lbl_dim) lv_label_set_text(lbl_dim, pref_dim_name(save_dim_idx()));
    if (lbl_off) lv_label_set_text(lbl_off, pref_off_name(save_off_idx()));
}

static void step_off(int dir)
{
    int n = (int)pref_off_count();
    int i = (int)save_off_idx() + dir;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    save_set_off_idx((uint8_t)i);
    keep_order(false);
    commit();
    if (lbl_dim) lv_label_set_text(lbl_dim, pref_dim_name(save_dim_idx()));
    if (lbl_off) lv_label_set_text(lbl_off, pref_off_name(save_off_idx()));
}

static void step_pace(int dir)
{
    int i = (int)save_pace_idx() + dir;
    if (i < 0) i = 0;
    if (i > 2) i = 2;
    save_set_pace_idx((uint8_t)i);
    save_flush();
    if (lbl_pace) lv_label_set_text(lbl_pace, save_pace_name(save_pace_idx()));
}

static void on_pace_minus(lv_event_t *e) { (void)e; step_pace(-1); }
static void on_pace_plus(lv_event_t *e) { (void)e; step_pace(1); }

static void on_bl_minus(lv_event_t *e) { (void)e; step_bl(-1); }
static void on_bl_plus(lv_event_t *e) { (void)e; step_bl(1); }
static void on_dim_minus(lv_event_t *e) { (void)e; step_dim(-1); }
static void on_dim_plus(lv_event_t *e) { (void)e; step_dim(1); }
static void on_off_minus(lv_event_t *e) { (void)e; step_off(-1); }
static void on_off_plus(lv_event_t *e) { (void)e; step_off(1); }
static void on_led(lv_event_t *e)
{
    (void)e;
    save_set_led_flash(save_led_flash() ? 0 : 1);
    commit();
    paint_led();
    if (save_led_flash()) audio_click();
}

static void on_usb(lv_event_t *e)
{
    (void)e;
    bool next = !usb_msc_active();
    if (!usb_msc_set(next)) {
        ui_toast("Нет SD");
        paint_usb();
        return;
    }
    ui_toast(next ? "Перезагрузка, карта для ПК" : "Карта у устройства");
    delay(500);
    ESP.restart();
}

static void on_fps(lv_event_t *e)
{
    (void)e;
    save_set_show_fps(save_show_fps() ? 0 : 1);
    save_flush();
    paint_fps();
}

static void on_auto_ntp(lv_event_t *e)
{
    (void)e;
    daily_set_auto_ntp(!daily_auto_ntp());
    paint_daily();
    ui_toast(daily_auto_ntp() ? "Авто NTP" : "Без авто NTP");
}

static void on_night_pet(lv_event_t *e)
{
    (void)e;
    daily_set_night_pet(!daily_night_pet());
    paint_daily();
}

static void on_quiet_night(lv_event_t *e)
{
    (void)e;
    daily_set_quiet_night(!daily_quiet_night());
    paint_daily();
}

static void on_care_notify(lv_event_t *e)
{
    (void)e;
    daily_set_care_notify(!daily_care_notify());
    paint_daily();
    ui_toast(daily_care_notify() ? "Уведомления вкл" : "Уведомления выкл");
}

static lv_obj_t *caption(lv_obj_t *scr, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 12, y);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    return label;
}

static lv_obj_t *value_label(lv_obj_t *scr, int y, const char *text)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 60, y);
    lv_obj_set_width(label, 102);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(col.ink), 0);
    return label;
}

static void add_stepper(lv_obj_t *scr, int y, lv_event_cb_t minus, lv_event_cb_t plus)
{
    ui_button(scr, 12, y, 44, 44, "-", minus, NULL);
    ui_button(scr, 166, y, 44, 44, "+", plus, NULL);
}

struct Lay {
    lv_obj_t *scr;
    int y;
};

static void section(Lay &lay, const char *text)
{
    caption(lay.scr, lay.y, text);
    lay.y += 24;
}

static lv_obj_t *full_btn(Lay &lay, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = ui_button(lay.scr, 12, lay.y, 198, 44, text, cb, user);
    lay.y += 52;
    return btn;
}

static lv_obj_t *step_row(Lay &lay, const char *text, lv_event_cb_t minus, lv_event_cb_t plus)
{
    add_stepper(lay.scr, lay.y, minus, plus);
    lv_obj_t *lbl = value_label(lay.scr, lay.y + 12, text);
    lay.y += 56;
    return lbl;
}

static void end_pad(Lay &lay)
{
    lv_obj_t *tail = lv_obj_create(lay.scr);
    lv_obj_set_pos(tail, 0, lay.y);
    lv_obj_set_size(tail, 10, 20);
    lv_obj_set_style_bg_opa(tail, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tail, 0, 0);
    lv_obj_clear_flag(tail, LV_OBJ_FLAG_CLICKABLE);
}

static void on_open_page(lv_event_t *e)
{
    page = (SetPage)(uint8_t)(intptr_t)lv_event_get_user_data(e);
    ui_show(SCR_SETTINGS);
}

static void menu_row(Lay &lay, const char *title, const char *hint, SetPage dest)
{
    lv_obj_t *btn = ui_button(lay.scr, 12, lay.y, 198, 68, title, on_open_page, (void *)(intptr_t)dest);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    char buf[72];
    snprintf(buf, sizeof(buf), "%s\n%s", title, hint);
    if (lbl) {
        lv_label_set_text(lbl, buf);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, 182);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }
    lay.y += 76;
}

static void build_menu(Lay &lay)
{
    menu_row(lay, "Экран", "тема и подсветка", PAGE_SCREEN);
    menu_row(lay, "Питомец", "темп и уведомления", PAGE_PET);
    menu_row(lay, "Сеть", "Wi-Fi и друг", PAGE_NET);
    menu_row(lay, "Кнопки", "BOOT, 12 и 16", PAGE_KEYS);
    menu_row(lay, "Устройство", "USB и прошивка", PAGE_DEVICE);
}

static void build_screen(Lay &lay)
{
    section(lay, "Тема");
    btn_light = ui_button(lay.scr, 12, lay.y, 96, 44, "Светлая", on_theme, (void *)(intptr_t)0);
    btn_dark = ui_button(lay.scr, 114, lay.y, 96, 44, "Тёмная", on_theme, (void *)(intptr_t)1);
    lay.y += 56;
    paint_theme();

    section(lay, "Яркость");
    lbl_bl = step_row(lay, pref_bl_name(save_bl_idx()), on_bl_minus, on_bl_plus);

    section(lay, "Приглушить через");
    lbl_dim = step_row(lay, pref_dim_name(save_dim_idx()), on_dim_minus, on_dim_plus);

    section(lay, "Погасить через");
    lbl_off = step_row(lay, pref_off_name(save_off_idx()), on_off_minus, on_off_plus);

    section(lay, "Счётчик кадров");
    btn_fps = full_btn(lay, "FPS", on_fps, NULL);
    paint_fps();
}

static void build_pet(Lay &lay)
{
    section(lay, "Темп нужд");
    lbl_pace = step_row(lay, save_pace_name(save_pace_idx()), on_pace_minus, on_pace_plus);

    section(lay, "Расписание");
    btn_night_pet = full_btn(lay, "Ночной сон", on_night_pet, NULL);
    btn_quiet = full_btn(lay, "Тихая ночь", on_quiet_night, NULL);
    btn_care = full_btn(lay, "Уведомления", on_care_notify, NULL);
    paint_daily();
}

static void build_net(Lay &lay)
{
    section(lay, "Время");
    lbl_clock = value_label(lay.scr, lay.y, "Нет времени");
    lv_obj_set_pos(lbl_clock, 12, lay.y);
    lv_obj_set_width(lbl_clock, 198);
    lay.y += 24;
    lbl_net = value_label(lay.scr, lay.y, net_status());
    lv_obj_set_pos(lbl_net, 12, lay.y);
    lv_obj_set_width(lbl_net, 198);
    lay.y += 28;
    paint_net();

    btn_auto_ntp = full_btn(lay, "Авто NTP", on_auto_ntp, NULL);
    paint_daily();
    full_btn(lay, "Синхр. время", on_sync_time, NULL);
    full_btn(lay, "Wi-Fi настройка", on_wifi_setup, NULL);
    full_btn(lay, "Страница статуса", on_status_page, NULL);
    full_btn(lay, "Выкл Wi-Fi", on_wifi_stop, NULL);

    section(lay, "ESP-NOW друг");
    full_btn(lay, "Слушать друга", on_radio_listen, NULL);
    full_btn(lay, "Подарок 5 монет", on_radio_gift, NULL);
    full_btn(lay, "Выкл ESP-NOW", on_radio_stop, NULL);
}

static void step_key(int key, int dir)
{
    if (key < 0 || key > 2) return;
    int n = BTN_ACT_COUNT;
    int i = (int)save_btn_act((uint8_t)key) + dir;
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    save_set_btn_act((uint8_t)key, (uint8_t)i);
    save_flush();
    if (lbl_key[key]) lv_label_set_text(lbl_key[key], save_btn_act_name((uint8_t)i));
}

static void on_key_minus(lv_event_t *e)
{
    step_key((int)(intptr_t)lv_event_get_user_data(e), -1);
}

static void on_key_plus(lv_event_t *e)
{
    step_key((int)(intptr_t)lv_event_get_user_data(e), 1);
}

static void build_keys(Lay &lay)
{
    lv_obj_t *hint = lv_label_create(lay.scr);
    lv_label_set_text(hint, "Действие: корм дома\nи нажатие в игре.");
    lv_obj_set_pos(hint, 12, lay.y);
    lv_obj_set_width(hint, 198);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, lv_color_hex(col.ink), 0);
    lay.y += 52;

    for (int key = 0; key < 3; key++) {
        section(lay, save_btn_key_name((uint8_t)key));
        ui_button(lay.scr, 12, lay.y, 44, 44, "-", on_key_minus, (void *)(intptr_t)key);
        ui_button(lay.scr, 166, lay.y, 44, 44, "+", on_key_plus, (void *)(intptr_t)key);
        lbl_key[key] = value_label(lay.scr, lay.y + 12, save_btn_act_name(save_btn_act((uint8_t)key)));
        lay.y += 56;
    }
}

static void build_device(Lay &lay)
{
    section(lay, "Светодиод камеры");
    btn_led = full_btn(lay, "Вспышки", on_led, NULL);
    paint_led();

    section(lay, "USB флешка");
    btn_usb = full_btn(lay, "USB", on_usb, NULL);
    paint_usb();
    lv_obj_t *usb_hint = caption(lay.scr, lay.y,
        "Карта уходит на ПК.\nЕщё раз или сброс\nпитания вернёт COM.\nФото и бэкап скрыты.");
    lv_obj_set_width(usb_hint, 198);
    lv_label_set_long_mode(usb_hint, LV_LABEL_LONG_WRAP);
    lay.y += 96;

    section(lay, "Сейв на карте");
    full_btn(lay, "Бэкап на SD", on_backup, NULL);
    full_btn(lay, "С SD", on_restore, NULL);

    section(lay, "Прошивка");
    lv_obj_t *ver = lv_label_create(lay.scr);
    lv_label_set_text(ver, "Версия " FW_VERSION_STR);
    lv_obj_set_pos(ver, 12, lay.y);
    lv_obj_set_width(ver, 198);
    lv_obj_set_style_text_color(ver, lv_color_hex(col.ink), 0);
    lay.y += 28;
    full_btn(lay, "Обновление", on_ota, NULL);
    full_btn(lay, "Сброс устройства", on_factory_reset, NULL);
}

static const char *page_title(SetPage which)
{
    if (which == PAGE_SCREEN) return "Экран";
    if (which == PAGE_PET) return "Питомец";
    if (which == PAGE_NET) return "Сеть";
    if (which == PAGE_KEYS) return "Кнопки";
    if (which == PAGE_DEVICE) return "Устройство";
    return "Настройки";
}

lv_obj_t *screen_settings_create()
{
    lv_obj_t *scr = ui_screen();
    lv_obj_add_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_AUTO);
    forget_widgets();
    ui_back_bar(scr, page_title(page));

    Lay lay = {scr, 56};
    if (page == PAGE_SCREEN) build_screen(lay);
    else if (page == PAGE_PET) build_pet(lay);
    else     if (page == PAGE_NET) build_net(lay);
    else if (page == PAGE_KEYS) build_keys(lay);
    else if (page == PAGE_DEVICE) build_device(lay);
    else build_menu(lay);
    end_pad(lay);

    set_scr = scr;
    set_clock_next = 0;
    lv_obj_add_event_cb(scr, on_set_del, LV_EVENT_DELETE, NULL);
    return scr;
}

void screen_settings_tick(uint32_t dt)
{
    (void)dt;
    if (!set_scr) return;
    uint32_t now = millis();
    if ((int32_t)(now - set_clock_next) < 0) return;
    set_clock_next = now + 500;
    paint_net();
    paint_usb();
}

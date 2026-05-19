#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "ui_components.h"
#include "ui_helpers.h"

typedef enum
{
    UI_CALC_KEY_DIGIT = 0,
    UI_CALC_KEY_DOT,
    UI_CALC_KEY_CLEAR,
    UI_CALC_KEY_SIGN,
    UI_CALC_KEY_PERCENT,
    UI_CALC_KEY_OPERATOR,
    UI_CALC_KEY_EQUALS,
} ui_calculator_key_type_t;

typedef struct
{
    const char *label;
    ui_calculator_key_type_t type;
    char op;
    int x;
    int y;
    int w;
    bool filled;
} ui_calculator_key_t;

lv_obj_t *ui_Calculator = NULL;

static lv_obj_t *s_calculator_display = NULL;
static lv_obj_t *s_calculator_operator_label = NULL;
static char s_calc_input[32] = "0";
static double s_calc_accumulator = 0.0;
static char s_calc_pending_op = 0;
static bool s_calc_waiting_for_operand = true;
static bool s_calc_error = false;

static const ui_calculator_key_t s_calculator_keys[] = {
    {"C", UI_CALC_KEY_CLEAR, 0, 18, 222, 110, false},
    {"+/-", UI_CALC_KEY_SIGN, 0, 145, 222, 110, false},
    {"%", UI_CALC_KEY_PERCENT, 0, 272, 222, 110, false},
    {"÷", UI_CALC_KEY_OPERATOR, '/', 399, 222, 110, false},
    {"7", UI_CALC_KEY_DIGIT, 0, 18, 330, 110, false},
    {"8", UI_CALC_KEY_DIGIT, 0, 145, 330, 110, false},
    {"9", UI_CALC_KEY_DIGIT, 0, 272, 330, 110, false},
    {"×", UI_CALC_KEY_OPERATOR, '*', 399, 330, 110, false},
    {"4", UI_CALC_KEY_DIGIT, 0, 18, 438, 110, false},
    {"5", UI_CALC_KEY_DIGIT, 0, 145, 438, 110, false},
    {"6", UI_CALC_KEY_DIGIT, 0, 272, 438, 110, false},
    {"-", UI_CALC_KEY_OPERATOR, '-', 399, 438, 110, false},
    {"1", UI_CALC_KEY_DIGIT, 0, 18, 546, 110, false},
    {"2", UI_CALC_KEY_DIGIT, 0, 145, 546, 110, false},
    {"3", UI_CALC_KEY_DIGIT, 0, 272, 546, 110, false},
    {"+", UI_CALC_KEY_OPERATOR, '+', 399, 546, 110, false},
    {"0", UI_CALC_KEY_DIGIT, 0, 18, 654, 237, false},
    {".", UI_CALC_KEY_DOT, 0, 272, 654, 110, false},
    {"=", UI_CALC_KEY_EQUALS, 0, 399, 654, 110, true},
};

static lv_obj_t *ui_calculator_plain_obj(lv_obj_t *parent,
                                         int x,
                                         int y,
                                         int w,
                                         int h,
                                         int radius,
                                         lv_opa_t opa,
                                         uint32_t bg,
                                         int border_w)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, ui_px_x(x), ui_px_y(y));
    lv_obj_set_size(obj, ui_px_w(w), ui_px_h(h));
    lv_obj_set_style_radius(obj, ui_px_x(radius), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(obj, border_w, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static void ui_calculator_update_display(void)
{
    const char *op_text = "";

    if (s_calculator_display == NULL)
    {
        return;
    }

    lv_label_set_text(s_calculator_display, s_calc_error ? "Error" : s_calc_input);

    if (s_calculator_operator_label != NULL)
    {
        switch (s_calc_pending_op)
        {
        case '+':
            op_text = "+";
            break;
        case '-':
            op_text = "-";
            break;
        case '*':
            op_text = "×";
            break;
        case '/':
            op_text = "÷";
            break;
        default:
            op_text = "";
            break;
        }
        lv_label_set_text(s_calculator_operator_label, s_calc_error ? "" : op_text);
    }
}

static void ui_calculator_reset(void)
{
    strcpy(s_calc_input, "0");
    s_calc_accumulator = 0.0;
    s_calc_pending_op = 0;
    s_calc_waiting_for_operand = true;
    s_calc_error = false;
    ui_calculator_update_display();
}

static double ui_calculator_input_value(void)
{
    return strtod(s_calc_input, NULL);
}

static void ui_calculator_format_value(double value)
{
    if (value > 999999999999.0 || value < -999999999999.0)
    {
        s_calc_error = true;
        strcpy(s_calc_input, "Error");
        return;
    }

    snprintf(s_calc_input, sizeof(s_calc_input), "%.10g", value);
}

static bool ui_calculator_apply_pending(double rhs)
{
    switch (s_calc_pending_op)
    {
    case '+':
        s_calc_accumulator += rhs;
        return true;
    case '-':
        s_calc_accumulator -= rhs;
        return true;
    case '*':
        s_calc_accumulator *= rhs;
        return true;
    case '/':
        if (rhs == 0.0)
        {
            s_calc_error = true;
            strcpy(s_calc_input, "Error");
            return false;
        }
        s_calc_accumulator /= rhs;
        return true;
    default:
        s_calc_accumulator = rhs;
        return true;
    }
}

static void ui_calculator_append_digit(const char *digit)
{
    size_t len;

    if (digit == NULL)
    {
        return;
    }

    if (s_calc_error)
    {
        ui_calculator_reset();
    }

    if (s_calc_waiting_for_operand || strcmp(s_calc_input, "0") == 0)
    {
        snprintf(s_calc_input, sizeof(s_calc_input), "%s", digit);
        s_calc_waiting_for_operand = false;
        ui_calculator_update_display();
        return;
    }

    len = strlen(s_calc_input);
    if (len + strlen(digit) < sizeof(s_calc_input))
    {
        strncat(s_calc_input, digit, sizeof(s_calc_input) - len - 1U);
    }
    ui_calculator_update_display();
}

static void ui_calculator_append_dot(void)
{
    size_t len;

    if (s_calc_error)
    {
        ui_calculator_reset();
    }

    if (s_calc_waiting_for_operand)
    {
        strcpy(s_calc_input, "0.");
        s_calc_waiting_for_operand = false;
        ui_calculator_update_display();
        return;
    }

    if (strchr(s_calc_input, '.') != NULL)
    {
        return;
    }

    len = strlen(s_calc_input);
    if (len + 1U < sizeof(s_calc_input))
    {
        strcat(s_calc_input, ".");
    }
    ui_calculator_update_display();
}

static void ui_calculator_toggle_sign(void)
{
    double value;

    if (s_calc_error)
    {
        ui_calculator_reset();
        return;
    }

    value = -ui_calculator_input_value();
    ui_calculator_format_value(value);
    s_calc_waiting_for_operand = false;
    ui_calculator_update_display();
}

static void ui_calculator_percent(void)
{
    double value;

    if (s_calc_error)
    {
        ui_calculator_reset();
        return;
    }

    value = ui_calculator_input_value() / 100.0;
    ui_calculator_format_value(value);
    s_calc_waiting_for_operand = false;
    ui_calculator_update_display();
}

static void ui_calculator_operator(char op)
{
    double value;

    if (s_calc_error)
    {
        ui_calculator_reset();
        return;
    }

    value = ui_calculator_input_value();
    if (!s_calc_waiting_for_operand)
    {
        if (!ui_calculator_apply_pending(value))
        {
            ui_calculator_update_display();
            return;
        }
        ui_calculator_format_value(s_calc_accumulator);
    }
    else
    {
        s_calc_accumulator = value;
    }

    s_calc_pending_op = op;
    s_calc_waiting_for_operand = true;
    ui_calculator_update_display();
}

static void ui_calculator_equals(void)
{
    double value;

    if (s_calc_error)
    {
        ui_calculator_reset();
        return;
    }

    value = ui_calculator_input_value();
    if (!ui_calculator_apply_pending(value))
    {
        ui_calculator_update_display();
        return;
    }
    s_calc_pending_op = 0;
    s_calc_waiting_for_operand = true;
    ui_calculator_format_value(s_calc_accumulator);
    ui_calculator_update_display();
}

static void ui_calculator_key_event_cb(lv_event_t *e)
{
    const ui_calculator_key_t *key;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    key = (const ui_calculator_key_t *)lv_event_get_user_data(e);
    if (key == NULL)
    {
        return;
    }

    switch (key->type)
    {
    case UI_CALC_KEY_DIGIT:
        ui_calculator_append_digit(key->label);
        break;
    case UI_CALC_KEY_DOT:
        ui_calculator_append_dot();
        break;
    case UI_CALC_KEY_CLEAR:
        ui_calculator_reset();
        break;
    case UI_CALC_KEY_SIGN:
        ui_calculator_toggle_sign();
        break;
    case UI_CALC_KEY_PERCENT:
        ui_calculator_percent();
        break;
    case UI_CALC_KEY_OPERATOR:
        ui_calculator_operator(key->op);
        break;
    case UI_CALC_KEY_EQUALS:
        ui_calculator_equals();
        break;
    default:
        break;
    }
}

static void ui_calculator_create_key(lv_obj_t *parent, const ui_calculator_key_t *key)
{
    lv_obj_t *button;
    lv_obj_t *label;

    button = ui_calculator_plain_obj(parent,
                                     key->x,
                                     key->y,
                                     key->w,
                                     92,
                                     12,
                                     LV_OPA_COVER,
                                     key->filled ? 0x000000 : 0xffffff,
                                     key->filled ? 0 : 2);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, ui_calculator_key_event_cb, LV_EVENT_CLICKED, (void *)key);

    label = ui_create_label(button,
                            key->label,
                            0,
                            15,
                            key->w,
                            64,
                            50,
                            LV_TEXT_ALIGN_CENTER,
                            false,
                            false);
    lv_obj_set_style_text_color(label, lv_color_hex(key->filled ? 0xffffff : 0x000000), 0);
}

void ui_Calculator_screen_init(void)
{
    size_t i;

    if (ui_Calculator != NULL)
    {
        return;
    }

    ui_Calculator = ui_create_screen_base();
    lv_obj_set_style_bg_color(ui_Calculator, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ui_Calculator, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ui_Calculator, LV_OBJ_FLAG_SCROLLABLE);

    ui_secondary_top_nav_create(ui_Calculator, "计算器", UI_SCREEN_MORE);

    s_calculator_display = ui_create_label(ui_Calculator,
                                           "0",
                                           120,
                                           132,
                                           376,
                                           86,
                                           72,
                                           LV_TEXT_ALIGN_RIGHT,
                                           false,
                                           false);
    s_calculator_operator_label = ui_create_label(ui_Calculator,
                                                  "",
                                                  32,
                                                  141,
                                                  80,
                                                  70,
                                                  56,
                                                  LV_TEXT_ALIGN_LEFT,
                                                  false,
                                                  false);

    for (i = 0; i < sizeof(s_calculator_keys) / sizeof(s_calculator_keys[0]); ++i)
    {
        ui_calculator_create_key(ui_Calculator, &s_calculator_keys[i]);
    }

    ui_calculator_reset();
}

void ui_Calculator_screen_destroy(void)
{
    if (ui_Calculator != NULL)
    {
        lv_obj_delete(ui_Calculator);
        ui_Calculator = NULL;
    }

    s_calculator_display = NULL;
    s_calculator_operator_label = NULL;
    strcpy(s_calc_input, "0");
    s_calc_accumulator = 0.0;
    s_calc_pending_op = 0;
    s_calc_waiting_for_operand = true;
    s_calc_error = false;
}

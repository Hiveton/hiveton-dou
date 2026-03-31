#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "button.h"
#include "sf_type.h"
#include "rtdebug.h"
#include "rtthread.h"

// Key action type definitions
typedef enum
{
    Action_NONE = 0,
    Action_UP,
    Action_DOWN,
    Action_SELECT,
    Action_EXIT
} ActionType;

typedef void (*ActionCallback_t)(ActionType action);
static ActionCallback_t action_cbk;

typedef struct
{
    ActionCallback_t on_action;
    int32_t button_id;
} ButtonControls;


// ADC Key event handlers
static void adc_button_handler(uint8_t group_idx, int32_t pin, button_action_t action)
{
    // Handle click events (short press)
    if (action == BUTTON_CLICKED)
    {
        if (pin == 0)
        {
            rt_kprintf("First button clicked → Up\n");
            if (action_cbk) action_cbk(Action_UP);
        }
        else if (pin == 1)
        {
            rt_kprintf("Second button clicked → Down\n");
            if (action_cbk) action_cbk(Action_DOWN);
        }
    }
    // Handle long press events
    else if (action == BUTTON_LONG_PRESSED)
    {
        if (pin == 0)
        {
            rt_kprintf("First button long pressed → Select\n");
            if (action_cbk) action_cbk(Action_SELECT);
        }
        else if (pin == 1)
        {
            rt_kprintf("Second button long pressed → Exit\n");
            if (action_cbk) action_cbk(Action_EXIT);
        }
    }
}

static void dummy_button_event_handler(int32_t pin, button_action_t action)
{
    // the actual events are handled by adc_button_handler
}

static ButtonControls *button_controls_create(ActionCallback_t on_action)
{
    ButtonControls *ctrl = (ButtonControls *)malloc(sizeof(ButtonControls));
    if (!ctrl)
    {
        rt_kprintf("Memory allocation failed\n");
        return NULL;
    }

    int32_t id;
    button_cfg_t cfg;

    // Initialize the key configuration
    cfg.pin = EPD_KEY_GPADC;
    cfg.active_state = BUTTON_ACTIVE_HIGH;
    cfg.mode = PIN_MODE_INPUT;
    cfg.button_handler = dummy_button_event_handler;

    // Initialize the button driver
    id = button_init(&cfg);
    if (id < 0)
    {
        rt_kprintf("Button initialization failed! Error code:%d\n", id);
        free(ctrl);
        return NULL;
    }

    // Bind ADC button handler
    adc_button_handler_t handlers[2] = {adc_button_handler, adc_button_handler};
    if (button_bind_adc_button(id, 0, 2, handlers) != 0)
    {
        rt_kprintf("Failed to bind ADC button handler\n");
        free(ctrl);
        return NULL;
    }

    // Enable button detection
    if (button_enable(id) != SF_EOK)
    {
        rt_kprintf("Failed to enable button\n");
        free(ctrl);
        return NULL;
    }
    ctrl->on_action = on_action;
    ctrl->button_id = id;
    action_cbk = on_action;

    rt_kprintf("Button controller initialized successfully, ID:%d\n", id);
    return ctrl;
}

static void button_controls_destroy(ButtonControls *ctrl)
{
    if (ctrl)
    {
        rt_kprintf("Destroying button controller, ID:%d\n", ctrl->button_id);
        free(ctrl);
    }
}

static void handle_action(ActionType action)
{
    switch (action)
    {
    case Action_UP:
        // Add the specific code for the upward action here
        break;
    case Action_DOWN:
        // Add the specific code for the downward action here
        break;
    case Action_SELECT:
        // Add the specific code for the selection action here
        break;
    case Action_EXIT:
        // Add the specific code for the exit action here
        break;
    default:
        break;
    }
}

static void button_test_thread(void *parameter)
{
    ButtonControls *button_ctrl = button_controls_create(handle_action);
    if (!button_ctrl)
    {
        rt_kprintf("The key controller creation fails and the thread quits\n");
        return;
    }

    while (1)
    {
        rt_thread_mdelay(100);
        rt_kprintf("Button test thread running\n");
    }
}

int main()
{
    rt_thread_t tid = rt_thread_create("button_test", button_test_thread, RT_NULL, 1024, 10, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("The key thread creation fails\n");
    }
    return 0;
}

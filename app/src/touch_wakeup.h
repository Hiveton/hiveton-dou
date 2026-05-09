#ifndef APP_TOUCH_WAKEUP_H
#define APP_TOUCH_WAKEUP_H

#ifdef __cplusplus
extern "C" {
#endif

void touch_wakeup_init(void);
void app_touch_wakeup_notify_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TOUCH_WAKEUP_H */

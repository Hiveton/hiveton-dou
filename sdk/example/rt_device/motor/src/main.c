#include "rtthread.h"
#include "rthw.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "board.h"
#include <drivers/rt_drv_encoder.h>

#define PWMA_PIN 36
#define PWMB_PIN 41
#define AIN1_PIN 44
#define AIN2_PIN 35
#define STBY_PIN 43
#define BIN1_PIN 42
#define BIN2_PIN 40
#define ENC1A_PIN 38
#define ENC1B_PIN 37
#define ENC2A_PIN 39
#define ENC2B_PIN 30

#define PWM_PERIOD (50000) // Period value of PWM , 20KHz
#define MAX_DUTY (10000)
#define MIN_DUTY (-10000)

#define ENCODER_PPR (13)    // Encoder PPR
#define GEAR_RATIO (20.409) // Gear ratio
#define PPR                                                                    \
    (ENCODER_PPR * GEAR_RATIO * 4) // Total PPR considering quadrature encoding

typedef struct _pid_controller
{
    float Kp; // Proportional gain
    float Ki; // Integral gain
    float Kd; // Derivative gain

    float NowError;   // e(k) (current error)
    float LastError;  // e(k-1) (previous error)
    float LLastError; // e(k-2) (error two steps ago)
    float TotalError; // e(j) (accumulated/integral error)

    float Result;     // U(k) (current control output)
    float LastResult; // U(k-1) (previous control output)
} pid_controller_t;

/* Global devices and control variables */
struct rt_device_pwm *pwma1_device = RT_NULL;
rt_device_t gptim1_device = RT_NULL;
rt_device_t gptim2_device = RT_NULL;
rt_device_t pin_device = RT_NULL;
int speed_max = 0;
int l_pulse = 0;
int r_pulse = 0;
int non_stop_mode = 0; // 0 for stop after num, 1 for continuous
pid_controller_t pid1;
pid_controller_t pid2;
pid_controller_t pid_pos1;
pid_controller_t pid_pos2;

/**
 * @brief Initialize a PID controller structure
 *
 * @param pid Pointer to the pid_controller_t structure to initialize
 * @param Kp Proportional gain
 * @param Ki Integral gain
 * @param Kd Derivative gain
 * @return 0 on success, -1 on invalid parameter
 */
int pid_controller_init(pid_controller_t *pid, float Kp, float Ki, float Kd)
{
    if (pid == RT_NULL)
        return -1;

    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;

    pid->NowError = 0.0f;
    pid->LastError = 0.0f;
    pid->LLastError = 0.0f;
    pid->TotalError = 0.0f;

    pid->Result = 0.0f;
    pid->LastResult = 0.0f;

    return 0;
}

/**
 * @brief Set PID controller parameters
 *
 * @param pid Pointer to pid_controller_t structure
 * @param Kp Proportional gain
 * @param Ki Integral gain
 * @param Kd Derivative gain
 * @return 0 on success, -1 on invalid parameter
 */
int pid_controller_set_params(pid_controller_t *pid, float Kp, float Ki,
                              float Kd)
{
    if (pid == RT_NULL)
        return -1;

    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;

    return 0;
}

/**
 * @brief Compute PID output (no anti-windup)
 *
 * Uses the incremental form to compute the PID output and updates historical
 * errors and the previous output.
 *
 * @param pid Pointer to pid_controller_t structure
 * @param setpoint Target setpoint
 * @param measure Measured value
 * @return Computed control value (float)
 */
float pid_controller_cal(pid_controller_t *pid, float setpoint, float measure)
{
    if (pid == RT_NULL)
        return 0.0f;

    pid->NowError = setpoint - measure;
    pid->TotalError += pid->NowError;

    // PID calculation
    pid->Result =
        pid->LastResult + pid->Kp * (pid->NowError - pid->LastError) +
        pid->Ki * pid->NowError +
        pid->Kd * (pid->NowError - 2 * pid->LastError + pid->LLastError);

    // Save historical data
    pid->LLastError = pid->LastError;
    pid->LastError = pid->NowError;
    pid->LastResult = pid->Result;

    return pid->Result;
}

/**
 * @brief Compute PID output (with anti-integral-windup)
 *
 * Similar to pid_controller_cal but applies saturation limits after calculation
 * to prevent integral accumulation from causing outputs to go out of range.
 *
 * @param pid Pointer to pid_controller_t structure
 * @param setpoint Target setpoint
 * @param measure Measured value
 * @return Computed and clipped control value (float)
 */
float pid_controller_cal_sat(pid_controller_t *pid, float setpoint,
                             float measure)
{
    if (pid == RT_NULL)
        return 0.0f;

    pid->NowError = setpoint - measure;
    pid->TotalError += pid->NowError;

    // PID calculation
    pid->Result =
        pid->LastResult + pid->Kp * (pid->NowError - pid->LastError) +
        pid->Ki * pid->NowError +
        pid->Kd * (pid->NowError - 2 * pid->LastError + pid->LLastError);

    // Save historical data
    pid->LLastError = pid->LastError;
    pid->LastError = pid->NowError;
    pid->LastResult = pid->Result;

    // Anti-integral-windup (saturation handling)
    if (pid->LastResult > MAX_DUTY)
        pid->LastResult = MAX_DUTY;
    if (pid->LastResult < MIN_DUTY)
        pid->LastResult = MIN_DUTY;
    if (pid->Result > MAX_DUTY)
        pid->Result = MAX_DUTY;
    if (pid->Result < MIN_DUTY)
        pid->Result = MIN_DUTY;

    return pid->Result;
}

// ... device variables moved to unified globals above ...

/**
 * @brief Set GPIO pin level
 *
 * A thin wrapper around rt_device_write to write the pin device's pin state.
 *
 * @param pin GPIO pin number (corresponding to board PAD index)
 * @param value Level to write: 0 = low, non-zero = high
 */
static void rt_pin_set(int pin, int value)
{
    struct rt_device_pin_status st;
    st.pin = pin;
    st.status = value ? 1 : 0;
    rt_device_write(pin_device, 0, &st, sizeof(struct rt_device_pin_status));
}

/**
 * @brief Read encoder count and clear it
 *
 * Read the count value of the specified encoder device, then clear the counter
 * so it can accumulate again.
 *
 * @param enc_device Encoder device handle (obtained via rt_device_find)
 * @return The count read this time (int)
 */
static int encoder_read(rt_device_t enc_device)
{
    struct rt_encoder_configuration enc;
    rt_device_control((struct rt_device *)enc_device,
                      PULSE_ENCODER_CMD_GET_COUNT, (void *)&enc);
    int res = enc.get_count;
    enc.set_count = 0;
    rt_device_control((struct rt_device *)enc_device,
                      PULSE_ENCODER_CMD_SET_COUNT, (void *)&enc);
    return res;
}

/**
 * @brief Set direction and PWM duty for left and right motors
 *
 * Set direction pins according to the sign of left_duty and right_duty, clip
 * the duty values, and output via PWM. Duty is in the range
 * -MAX_DUTY..MAX_DUTY; negative values indicate reverse rotation.
 *
 * @param left_duty Target duty for left motor (negative => reverse)
 * @param right_duty Target duty for right motor (negative => reverse)
 */
static void motor_set(int left_duty, int right_duty)
{
    if (left_duty >= 0)
    {
        rt_pin_set(AIN2_PIN, 0);
        rt_pin_set(AIN1_PIN, 1);
    }
    else
    {
        rt_pin_set(AIN1_PIN, 0);
        rt_pin_set(AIN2_PIN, 1);
    }
    if (right_duty >= 0)
    {
        rt_pin_set(BIN2_PIN, 0);
        rt_pin_set(BIN1_PIN, 1);
    }
    else
    {
        rt_pin_set(BIN1_PIN, 0);
        rt_pin_set(BIN2_PIN, 1);
    }

    left_duty = left_duty > MAX_DUTY ? MAX_DUTY : left_duty;
    left_duty = left_duty < MIN_DUTY ? MIN_DUTY : left_duty;
    right_duty = right_duty > MAX_DUTY ? MAX_DUTY : right_duty;
    right_duty = right_duty < MIN_DUTY ? MIN_DUTY : right_duty;

    left_duty = left_duty < 0 ? -left_duty : left_duty;
    right_duty = right_duty < 0 ? -right_duty : right_duty;

    uint32_t left_duty_time = PWM_PERIOD * left_duty / 10000;
    uint32_t right_duty_time = PWM_PERIOD * right_duty / 10000;

    rt_pwm_set(pwma1_device, 1, PWM_PERIOD, left_duty_time);
    rt_pwm_set(pwma1_device, 2, PWM_PERIOD, right_duty_time);
}

/**
 * @brief Convert encoder pulse count to RPM
 *
 * Map the pulse count to revolutions per minute (RPM) based on the encoder and
 * gear ratio used in the example.
 *
 * @param pulse Pulse count (increment within a fixed time window)
 * @return Converted RPM (float)
 */
float pulse_to_rpm(int32_t pulse)
{
    return (float)pulse * 50 / PPR * 60;
}

/**
 * @brief Convert target RPM to corresponding pulse count (for mapping)
 *
 * @param rpm Target speed in RPM
 * @return Corresponding pulse count (integer)
 */
int rpm_to_pulse(float rpm)
{
    return (int)(rpm / 60 * PPR / 50);
}

/**
 * @brief finsh command interface: parse and set the motor command
 *
 * Command format: motor [dir] [spd] [num]
 *  - dir: 0 = forward, 1 = reverse
 *  - spd: target speed (RPM)
 *  - num: number of rotations, 0 = continuous rotation
 *
 * @param argc Number of arguments
 * @param argv Argument string array
 */
void motor(int argc, char **argv)
{
    int dir, spd, num;

    if (argc != 4)
    {
        rt_kprintf("Usage: motor [dir] [spd] [num]\n");
        rt_kprintf("  dir: 0-forward, 1-reverse\n");
        rt_kprintf("  spd: speed in rpm\n");
        rt_kprintf("  num: rotation count (0 for continuous)\n");
        return;
    }

    // Parse arguments
    dir = atoi(argv[1]);
    spd = atoi(argv[2]);
    num = atoi(argv[3]);

    // Validate arguments
    if (dir != 0 && dir != 1)
    {
        rt_kprintf("Error: dir must be 0 or 1\n");
        return;
    }

    if (spd < 0)
    {
        rt_kprintf("Error: speed cannot be negative\n");
        return;
    }

    if (num < 0)
    {
        rt_kprintf("Error: rotation count cannot be negative\n");
        return;
    }
    // Set motor parameters
    l_pulse = dir == 0 ? num * PPR : -num * PPR;
    r_pulse = dir == 0 ? num * PPR : -num * PPR;
    non_stop_mode = (num == 0) ? 1 : 0;
    speed_max = rpm_to_pulse(spd);
    rt_kprintf("Motor command set:\n");
    rt_kprintf("  Direction: %s\n", dir == 0 ? "Forward" : "Reverse");
    rt_kprintf("  Speed: %d rpm\n", spd);
    rt_kprintf("  Rotations: %d\n", num);
}
MSH_CMD_EXPORT(motor, motor control : motor[dir][spd][num]);

/**
 * @brief Program entry (main function)
 *
 * Initialize PWM, encoders and GPIO, bootstrap PID controllers, then in the
 * main loop read encoders, compute desired speeds, and call motor_set to
 * output control signals.
 *
 * @return 0 indicates normal exit (usually does not return)
 */
int main(void)
{
    /* Output a message on console using printf function */
    rt_kprintf("Motor Example.\n");
    rt_err_t result;
    struct rt_encoder_configuration config1;
    struct rt_encoder_configuration config2;
    struct rt_device_pin_mode m;

    // init pwm
    HAL_PIN_Set(PAD_PA00 + PWMA_PIN, ATIM1_CH1, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA00 + PWMB_PIN, ATIM1_CH2, PIN_NOPULL, 1);
    pwma1_device = (struct rt_device_pwm *)rt_device_find("pwma1");
    if (pwma1_device == RT_NULL)
    {
        rt_kprintf("pwm device pwma1 not found!\n");
        return -1;
    }
    result =
        rt_device_open((struct rt_device *)pwma1_device, RT_DEVICE_OFLAG_RDWR);
    rt_pwm_set(pwma1_device, 1, PWM_PERIOD, 0);
    rt_pwm_set(pwma1_device, 2, PWM_PERIOD, 0);
    rt_pwm_enable(pwma1_device, 1);
    rt_pwm_enable(pwma1_device, 2);

    // init encoder
    HAL_PIN_Set(PAD_PA00 + ENC1A_PIN, GPTIM1_CH1, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + ENC1B_PIN, GPTIM1_CH2, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + ENC2A_PIN, GPTIM2_CH1, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + ENC2B_PIN, GPTIM2_CH2, PIN_PULLUP, 1);
    gptim1_device = rt_device_find("encoder1");
    if (gptim1_device == RT_NULL)
    {
        rt_kprintf("gptim device gptim1 not found!\n");
        return -1;
    }
    gptim2_device = rt_device_find("encoder2");
    if (gptim2_device == RT_NULL)
    {
        rt_kprintf("gptim device gptim2 not found!\n");
        return -1;
    }
    config1.channel = GPT_CHANNEL_ALL;
    config2.channel = GPT_CHANNEL_ALL;

    result = rt_device_control(gptim1_device, PULSE_ENCODER_CMD_ENABLE,
                               (void *)&config1);
    if (result != RT_EOK)
    {
        rt_kprintf("gptim1 enable encoder failed!\n");
        return -1;
    }
    result = rt_device_control(gptim2_device, PULSE_ENCODER_CMD_ENABLE,
                               (void *)&config2);
    if (result != RT_EOK)
    {
        rt_kprintf("gptim2 enable encoder failed!\n");
        return -1;
    }

    // init pin
    HAL_PIN_Set(PAD_PA00 + AIN1_PIN, GPIO_A0 + AIN1_PIN, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + AIN2_PIN, GPIO_A0 + AIN2_PIN, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + STBY_PIN, GPIO_A0 + STBY_PIN, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + BIN1_PIN, GPIO_A0 + BIN1_PIN, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + BIN2_PIN, GPIO_A0 + BIN2_PIN, PIN_PULLUP, 1);
    pin_device = rt_device_find("pin");
    if (pin_device == RT_NULL)
    {
        printf("Find device pin fail\n");
    }
    rt_device_open(pin_device, RT_DEVICE_OFLAG_RDWR);
    m.pin = AIN1_PIN;
    m.mode = PIN_MODE_OUTPUT;
    rt_device_control(pin_device, 0, &m);
    m.pin = AIN2_PIN;
    m.mode = PIN_MODE_OUTPUT;
    rt_device_control(pin_device, 0, &m);
    m.pin = STBY_PIN;
    m.mode = PIN_MODE_OUTPUT;
    rt_device_control(pin_device, 0, &m);
    m.pin = BIN1_PIN;
    m.mode = PIN_MODE_OUTPUT;
    rt_device_control(pin_device, 0, &m);
    m.pin = BIN2_PIN;
    m.mode = PIN_MODE_OUTPUT;
    rt_device_control(pin_device, 0, &m);

    pid_controller_init(&pid1, 100, 20, 0);
    pid_controller_init(&pid2, 100, 20, 0);
    pid_controller_init(&pid_pos1, 1, 0, 0);
    pid_controller_init(&pid_pos2, 1, 0, 0);

    rt_pin_set(STBY_PIN, 1);
    speed_max = rpm_to_pulse(300);

    int64_t wake_time = rt_tick_get();
    int cnt = 0;
    /* Infinite loop */
    while (1)
    {
        int l_out = 0;
        int r_out = 0;
        int l_expect_speed;
        int r_expect_speed;
        int enc_l = encoder_read(gptim1_device);
        int enc_r = encoder_read(gptim2_device);
        if (non_stop_mode)
        {
            l_expect_speed = speed_max;
            r_expect_speed = speed_max;
        }
        else
        {
            l_pulse -= enc_l;
            r_pulse -= enc_r;
            l_expect_speed = -pid_controller_cal(&pid_pos1, 0, l_pulse);
            if (l_expect_speed > speed_max)
                l_expect_speed = speed_max;
            if (l_expect_speed < -speed_max)
                l_expect_speed = -speed_max;
            r_expect_speed = -pid_controller_cal(&pid_pos2, 0, r_pulse);
            if (r_expect_speed > speed_max)
                r_expect_speed = speed_max;
            if (r_expect_speed < -speed_max)
                r_expect_speed = -speed_max;
        }
        l_out = pid_controller_cal_sat(&pid1, l_expect_speed, enc_l);
        r_out = pid_controller_cal_sat(&pid2, r_expect_speed, enc_r);
        motor_set(l_out, r_out);
        cnt++;
        if (cnt >= 50)
        {
            printf("left speed: %f rpm, right speed: %f rpm.\n",
                   pulse_to_rpm(enc_l), pulse_to_rpm(enc_r));
            cnt = 0;
        }
        wake_time += 20;
        rt_thread_mdelay(wake_time - rt_tick_get());
    }
    return 0;
}

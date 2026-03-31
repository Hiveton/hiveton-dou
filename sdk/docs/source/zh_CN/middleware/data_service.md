# 数据服务

应用程序需要使用各种传感器或BLE传输过来的数据，而这些数据可能来自不同的硬件接口，也可能来自不同的核，比如传感器接在LCPU上，HCPU上运行的图形界面需要显示传感器收集到的数据。
为了让应用无需知道数据来源，简化双核设计，不论数据是由哪个核产生、来自什么设备，数据使用者都可以用同样的方法获取和处理数据，SDK 提供了一个数据服务框架，
可以让应用在不了解数据生成细节的情况下使用这些数据。

主要功能包括 ：
- 应用订阅数据服务，应用被称为订阅者（subscriber）或客户（client），是数据的消费者
- 传感器或 BLE 数据服务作为提供者(provider)实现某个数据服务，从 I2C、SPI 或不同的远程 BLE 设备获取数据 ，是数据的生产者
- 框架支持数据服务提供者和订阅者运行在不同的 CPU 内核上。提供的数据服务可以在一个内核中实现，也可以在不影响应用程序（订阅者）的情况下移动到另一个内核 
- 提供数据服务模拟功能，以便应用程序和数据服务的独立开发
- 线程安全的API接口，允许在多线程中使用

SIFLI SDK中包含了用于测试数据服务提供者或订阅者的测试工具。用户可以使用测试服务来模拟传感器服务，例如心率传感器、指南针、运动等传感器。这些功能与SDK示例 - 手表演示模拟器集成，可以帮助用户开发需要传感器支持的手表应用程序。数据服务API的详细介绍请参考[data_service](middleware-data_service).

数据服务使用消息在不同的核间/线程之间传递数据，消息分为请求(XXX_REQ)和响应(XXX_RSP)两种，请求消息为client发送给service的，响应消息为service发送给client的，
消息ID长度为16比特，低15位是消息编号，最高位（比特15）表示消息种类，0表示请求消息，1表示响应消息，通常请求和响应消息成对出现，其da低15比特的编号相同，只有最高比特不同，
例如下面的例子定义了SUBSCRIBE_REQ和SUBSCRIBE_RSP两条消息
```c
MSG_SERVICE_SUBSCRIBE_REQ = 0x0,
MSG_SERVICE_SUBSCRIBE_RSP = RSP_MSG_TYPE | MSG_SERVICE_SUBSCRIBE_REQ,
```
有时service也会主动发送消息给client, 并没有请求消息与之对应, 这时也可以定义独立的响应消息, 一般以IND结尾, 但也需要标记为响应消息, 例如
```c
MSG_SERVICE_DATA_NTF_IND  = RSP_MSG_TYPE | 0x09,
```

框架预留了48个消息ID (0x0 ~ 0x2F)用于内部使用, 用户可以从`MSG_SERVICE_CUSTOM_ID_BEGIN`开始定义自己的消息, 避免与预留消息冲突, 例如
```c
typedef enum
{
    BLE_DFU_SEND_DATA_REQ = MSG_SERVICE_CUSTOM_ID_BEGIN,
    /* Response. */
    BLE_DFU_SEND_DATA_RSP = BLE_DFU_SEND_DATA_REQ | RSP_MSG_TYPE,
} ble_dfu_service_message_t;
```



## 数据服务配置
运行menuconfig, 在Sifli Middleware中选择`Enable Data Service`, 如需支持跨核服务, 还需选中Middleware中的`Enable IPC Queue Library`, 
配置成功后，在工程的rtconfig.h中会出现以下两个宏, `BSP_USING_DATA_SVC`为data service的宏, `USING_IPC_QUEUE`为IPC Queue的宏, 用于支持跨核操作
```c
#define BSP_USING_DATA_SVC 1
#define USING_IPC_QUEUE 1
```

## 使用的系统资源
如果使能了IPC Queue, 数据服务框架使用IPC Queue 1作为双核的通信通道，HCPU->LCPU的发送buffer地址为`HCPU2LCPU_MB_CH2_BUF_START_ADDR`, 大小为`HCPU2LCPU_MB_CH2_BUF_SIZE`，
LCPU->HCPU的发送buffer地址为`LCPU2HCPU_MB_CH2_BUF_START_ADDR`, 大小为`LCPU2HCPU_MB_CH2_BUF_SIZE`, 默认大小均为512字节, 如需修改buffer大小, 可以参考( _/app_note/memory_usage.md_ )

## 注册数据服务
数据服务组件是一个开放框架, 实现的数据服务需要注册到框架中后才能供订阅者使用，以下示例使用函数 #datas_register 注册名为`test`的服务.
需要注意, 服务的注册必须在INIT_COMPONENT阶段, 以确保在INIT_ENV阶段启动数据服务框架时所有服务都已就绪

```c

#include "data_service_provider.h"

bool test_service_filter(data_req_t *config, uint16_t msg_id, uint32_t len, uint8_t *data)		
{
	return TRUE;								// Do not filter any data, push all data to subscriber.
}

static void timer_callback()
{
	datas_ind_size(service, 16);				// Indicate to service that 16 bytes of data is generated
}

static int32_t test_service_msg_handler(datas_handle_t service, data_msg_t *msg)
{
    switch (msg->msg_id)
    {
		case MSG_SERVICE_SUBSCRIBE_REQ:
			// Start to generate data, for examples, start a periodical timer to read from sensor. timeout will call timer_callback
			break;
		case MSG_SERVICE_UNSUBSCRIBE_REQ:
			// Stop to generate data, for examples, stop the periodical timer previously created
			break;
		case MSG_SERVICE_CONFIG_REQ:
		case MSG_SERVICE_TX_REQ:
		case MSG_SERVICE_RX_REQ:
			break;
		case MSG_SERVICE_DATA_RDY_IND:
		{
			data_rdy_ind_t *data_ind = (data_rdy_ind_t *)(data_service_get_msg_body(msg));
			uint8_t *data;

			// Fetch data, code is not in this sample, in this sample, data_ind->len is 16 bytes.
			...
			datas_push_data_to_client(service, data_ind->len, data);
			break;
		}
		default:
			ASSERT(0);			//Unknown message
    }
    return 0;
}

static data_service_config_t test_service=
{
    .max_client_num = 5,						// Support up to 5 client connection
    .queue = NULL,								// Do not have its own process thread, use framework thread for message handling
    .data_filter = test_service_filter,			// Message filter
    .msg_handler = test_service_msg_handler,	// Message handler
};

static int test_service_register(void)
{
    /* register test service, service name is "test" */
    datas_handle_t service_handle = datas_register("test", &test_service);
    RT_ASSERT(service_handle);

    return 0;
}
INIT_COMPONENT_EXPORT(test_service_register);

```

## 使用数据服务
应用使用服务的名称来订阅服务，以下示例代码展示如何订阅名为test的服务并发送请求消息。需要注意，只有在订阅成功后才能发送消息给服务，否则会返回失败。

```c

#include "data_service_subscriber.h"

int datasvc_clnt_callback(data_callback_arg_t *arg)
{
    if (MSG_SERVICE_TEST_DATA_REQ == arg->msg_id)
    {
        uint8_t *data = arg->data;
        // handle arg->data, length is indicated by arg->data_len
        LOG("Command result %d\n", rsp->result);
    }
    else if (MSG_SERVICE_DATA_NTF_IND == arg->msg_id)
    {
    	LOG("Get data length %d\n", arg->data_len);
    }
    else
    {
    	LOG("Get message %d\n", arg->msg_id);
    }
}

static datac_handle_t hdl;

int main(void)
{
    // allocate subscriber(i.e. client) handle
    hdl = datac_open();
    // check handle
    RT_ASSERT(DATA_CLIENT_INVALID_HANDLE != hdl);
        
    // Subscribe a service named "test"
    hdl = datac_subscribe(hdl, "test", datasvc_clnt_callback, NULL);
}

int send_req(void)
{
    data_msg_t msg;
    uint8_t *body;
    rt_err_t err;

    body = data_service_init_msg(&msg, MSG_SERVICE_TEST_DATA_REQ, 0);
    err = datac_send_msg(client_handle, &msg);
    RT_ASSERT(RT_EOK == err);
}

int unsubscribe(void(
{

    // Unsubscribe data. hdl could be used to subscribe again
    datac_unsubscribe(hdl);

    // Free subscriber handle, the handle cannot be used anymore
    datac_close(hdl);
    hdl = DATA_CLIENT_INVALID_HANDLE;
}

```

## 调用顺序

用户应该遵循以下调用顺序使用数据服务。在SDK中，数据服务框架的启动在INIT_ENV阶段，因此要求数据服务在INIT_COMPONENT阶段完成注册，订阅者在INIT_APP阶段或者更晚才能订阅服务

1. `datas_register`: 在RT-Thread的`INIT_COMPONENT`阶段调用，注册数据服务到框架中
2. `datas_start`: 在RT-Thread的`INIT_ENV`阶段自动执行（无需用户代码调用），启动数据服务框架，接收Subscriber的请求，HCPU此时会启动LCPU
3. `datac_open`: 为subscriber分配端口
4. `datac_subscribe`: Subscriber发起连接，订阅数据服务。


## 在 GUI 中使用数据服务

像 LVGL 这样的 GUI 系统以单线程模式运行，它有自己的 LVGL 任务。 为了在 GUI 线程中更新数据，数据服务定义了可以从 GUI 线程/任务调用的特定接口。

### 工作原理

GUI数据服务的实现机制：
1. 通过 `datac_subscribe_ex` 注册一个消息队列（queue）
2. 数据服务的所有消息先进入这个队列，而不是直接回调
3. 启动一个LVGL定时器（lv_timer）定期检查这个队列
4. 在LVGL任务上下文中从队列取出消息并触发回调

这种设计满足了LVGL只能单线程执行的要求，确保所有GUI更新操作都在LVGL任务中完成，避免了线程安全问题。

### 使用示例

以下是在GUI应用中使用数据服务的核心步骤：

```c
#include "ui_datasrv_subscriber.h"

static datac_handle_t sensor_handle;

/* 数据服务回调函数 - 在LVGL任务中执行 */
static int sensor_data_callback(data_callback_arg_t *arg)
{
    switch (arg->msg_id)
    {
        case MSG_SERVICE_SUBSCRIBE_RSP:
        {
            data_subscribe_rsp_t *rsp = (data_subscribe_rsp_t *)arg->data;
            if (rsp->result == 0)
            {
                /* 订阅成功，可以开始请求数据 */
            }
            break;
        }

        case MSG_SERVICE_DATA_NTF_IND:
        {
            /* 收到数据通知，安全地更新LVGL控件 */
            sensor_data_t *data = (sensor_data_t *)arg->data;
            
            // 直接更新UI，无需额外同步
            lv_label_set_text_fmt(label, "Value: %d", data->value);
            break;
        }
    }
    return 0;
}

/* 应用启动时 */
void app_start(void)
{
    /* 1. 初始化GUI数据服务模块（整个应用只需初始化一次） */
    ui_datac_init();
    
    /* 2. 分配客户端句柄 */
    sensor_handle = datac_open();
    RT_ASSERT(DATA_CLIENT_INVALID_HANDLE != sensor_handle);
    
    /* 3. 订阅服务 - 回调将在LVGL任务中执行 */
    ui_datac_subscribe(sensor_handle, "SENSOR", 
                      sensor_data_callback, 0);
}

/* 应用停止时 */
void app_stop(void)
{
    if (DATA_CLIENT_INVALID_HANDLE != sensor_handle)
    {
        datac_unsubscribe(sensor_handle);
        datac_close(sensor_handle);
        sensor_handle = DATA_CLIENT_INVALID_HANDLE;
    }
}
```

**完整的GUI应用示例**请参考[图形应用框架](gui_app_framework.md#应用示例)章节中的指南针应用，该示例展示了完整的应用生命周期和数据服务集成。



### 关键接口说明

- **`ui_datac_init()`**: 初始化GUI数据服务模块，创建消息队列并启动LVGL定时器（15ms周期）。应在GUI初始化时调用一次。

- **`ui_datac_subscribe(handle, name, callback, user_data)`**: 在GUI线程上下文中订阅数据服务。与普通的 `datac_subscribe` 不同，它通过消息队列确保回调在LVGL任务中执行。

- **回调函数执行环境**: 所有通过 `ui_datac_subscribe` 注册的回调函数都在LVGL任务中执行，可以安全地调用任何LVGL API更新界面。

### 与普通订阅的区别

| 特性 | datac_subscribe | ui_datac_subscribe |
|------|-----------------|-------------------|
| 回调执行线程 | 数据服务任务 | LVGL任务 |
| 线程安全性 | 需要用户处理同步 | 框架自动保证 |
| 适用场景 | 非GUI应用 | GUI应用 |
| 是否可更新UI | 否，需额外同步 | 是，直接更新 |


## API参考
[](middleware-data_service)

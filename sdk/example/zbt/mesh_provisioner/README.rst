.. zephyr:code-sample:: ble_mesh_provisioner
   :name: Mesh Provisioner
   :relevant-api: bt_mesh bluetooth

   Provision a node and configure it using the Bluetooth Mesh APIs.

Overview
********

This sample demonstrates how to use the Bluetooth Mesh APIs related to
provisioning and using the Configuration Database (CDB). It is intended
to be tested together with a device capable of being provisioned. For
example, one could use the sample in `example/zbt/mesh` or `example/zbt/mesh_demo`.

The application will self-provision and load the application key into 
the CDB, then wait to receive unprovisioned beacons from devices. 
It will use the 'mesh_btn 1' command to trigger the provisioning process 
using the PB-ADV protocol. After provisioning is completed, the node will 
appear in the CDB but will not yet be marked as "configured." The 
application will detect this unconfigured node and begin configuring 
it. If no errors are encountered, the node will be marked as configured.

The node configuration process includes adding an application key, 
retrieving its composition data, and binding all its models to that 
application key.

Requirements
************

* A board with Bluetooth LE support

menuconfig Configuration
************************
1. Enable Bluetooth (`BLUETOOTH`):
    - Path: Sifli middleware → Bluetooth
    - Enable: Enable bluetooth
        - Macro switch: `CONFIG_BLUETOOTH`
        - Description: Enables Bluetooth functionality
2. Enable GAP, GATT Client, BLE connection manager:
    - Path: Sifli middleware → Bluetooth → Bluetooth service → BLE 
    service
    - Enable: Enable BLE GAP central role
        - Macro switch: `CONFIG_BLE_GAP_CENTRAL`
        - Description: Switch for BLE CENTRAL (central device). When 
        enabled, it provides scanning and active connection initiation 
        with peripherals.
    - Enable: Enable BLE GATT client
        - Macro switch: `CONFIG_BLE_GATT_CLIENT`
        - Description: Switch for GATT CLIENT. When enabled, it can 
        actively search and discover services, read/write data, and 
        receive notifications.
    - Enable: Enable BLE connection manager
        - Macro switch: `CONFIG_BSP_BLE_CONNECTION_MANAGER`
        - Description: Provides BLE connection control management, 
        including multi-connection management, BLE pairing, link 
        connection parameter updates, etc.
3. Enable NVDS:
    - Path: Sifli middleware → Bluetooth → Bluetooth service → Common 
    service
    - Enable: Enable NVDS synchronous
        - Macro switch: `CONFIG_BSP_BLE_NVDS_SYNC`
        - Description: Bluetooth NVDS synchronization. When Bluetooth 
        is configured to HCPU, BLE NVDS can be accessed synchronously, 
        so enable this option; when Bluetooth is configured to LCPU, 
        this option needs to be disabled.


Compilation and Flashing
************************

Navigate to the example project directory and run the scons command to 
compile:
```c
> scons --board=eh-lb525 -j32
```
Navigate to the example's `project/build_xx` directory, run 
`uart_download.bat`, and select the port as prompted to download:
```c
$ ./uart_download.bat

     Uart Download

please input the serial port num:5
```

Expected Results
****************

After the example starts:
1. It can scan for beacons from unprovisioned devices and use 
'mesh_btn 1' command to provision them.
2. Provisioned devices can send and receive messages within the same 
network.
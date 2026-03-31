.. zephyr:code-sample:: ble_mesh_demo
   :name: Mesh Demo
   :relevant-api: bt_mesh bluetooth

   Implement a Bluetooth Mesh demo application.

Overview
********

This sample is a Bluetooth Mesh application intended for demonstration
purposes only. The application provisions and configures itself (i.e. no
external provisioner needed) with hard-coded network and application key
values. The local unicast address can be set using a NODE_ADDR build
variable (e.g. NODE_ADDR=0x0001 for unicast address 0x0001), or by
manually editing the value in the ``board.h`` file.

Because of the hard-coded values, the application is not suitable for
production use, but is quite convenient for quick demonstrations of mesh
functionality.

A special address, 0x000f, will make the application become a heart-beat
publisher and enable the other nodes to show information of the received
heartbeat messages.

Requirements
************

* one more board with Bluetooth LE support

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

Change to the `mesh/project` directory and run the scons command to compile:
::
   scons --board=eh-lb525 -j8

Switch to the example project/build_xx directory, run uart_download.bat, 
and follow the prompts to select the port to proceed with the flash.

::
   ./uart_download.bat

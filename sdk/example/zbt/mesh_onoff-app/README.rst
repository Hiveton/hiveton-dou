.. zephyr:code-sample:: ble_mesh_onoff
   :name: Mesh OnOff Model
   :relevant-api: bt_mesh

   Control LEDs on a mesh network using the Bluetooth Mesh OnOff model.

Overview
********

This is a simple application demonstrating a Bluetooth Mesh multi-element node.
Each element has a mesh onoff client and server
model which controls one of the 4 sets of buttons and LEDs .

Prior to provisioning, an unprovisioned beacon is broadcast that contains
a unique UUID. Each button controls the state of its
corresponding LED and does not initiate any mesh activity.

The models for button 1 and LED 1 are in the node's root element.
The 3 remaining button/LED pairs are in elements 1 through 3.
Assuming the provisioner assigns 0x100 to the root element,
the secondary elements will appear at 0x101, 0x102 and 0x103.

After provisioning, the button clients must be configured to 
publish and the LED servers to subscribe.

If a LED server is provided with a publish address, it will
also publish its status on an onoff state change.

Requirements
************

* Two or more board with Bluetooth LE support, and
* A mobile phone with ble mesh app installed 

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


Expected Results
****************

After the example is launched:

1. It can be discovered and provisioned via a BLE MESH APP on a mobile phone.

2. The device can simulate a button to publish messages to a specified address using the 'mesh_btn [publish_address] [led_status]' command.

3. The device can subscribe to a specified address and receive published messages.
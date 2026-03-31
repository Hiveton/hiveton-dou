.. zephyr:code-sample:: ble_mesh
   :name: Mesh
   :relevant-api: bt_mesh bluetooth

   Use basic Bluetooth LE Mesh functionality.

Overview
********

This sample demonstrates Bluetooth Mesh functionality. It has several
standard mesh models, and supports provisioning over both the
Advertising and the GATT Provisioning Bearers (i.e. PB-ADV and PB-GATT).

The application also needs a functioning serial console, since that's
used for the Out-of-Band provisioning procedure.

On boards with LEDs, a Generic OnOff Server model exposes functionality for
controlling the first LED on the board over the mesh.

On boards with buttons, a Generic OnOff Client model will send Onoff messages
to all nodes in the network when the button is pressed.

Requirements
************

* Two or more board with Bluetooth LE support, or
* A mobile phone with ble mesh app installed and one
* board with Bluetooth LE support

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


Interacting with the sample
***************************

The sample can either be provisioned into an existing mesh network by an  
external provisioner device, or self-provisioned by sending 'mesh_btn' to simulate a button press.

When provisioning with a provisioner device, the provisioner must give the
device an Application key and bind it to both Generic OnOff models.

When self-provisioning, the device will take a random unicast address and
bind a dummy Application key to these models.

After provisioning is complete, messages sent to the Generic OnOff Server model 
will be used to turn the LED on or off, while simulating a button press by 
sending the 'mesh_btn' command will be used to broadcast OnOff messages to 
all nodes in the same network.

Expected Results
****************

After the program starts:

1.It can be discovered and provisioned by a phone via a BLE Mesh APP. 
Once provisioning is complete, messages sent to the Generic OnOff Server 
will be used to control the LED.

2.The self‑provisioned device can broadcast OnOff messages to all nodes 
in the same network through a virtual button.
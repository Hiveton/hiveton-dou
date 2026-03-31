# Opus Example

Source code path: example/multimedia/audio/opus

## Supported Platforms
<!-- Which boards and chip platforms are supported -->
+ eh-lb525

## Overview
<!-- Example introduction -->
This example demonstrates how to use the Opus audio codec library for recording, encoding, decoding, and playback, including:

+ Record PCM audio data at a 16kHz sampling rate from the microphone via mic recording
+ Encoding: Compress PCM data using the Opus codec (10ms frame length, approximately 16kbps bitrate)
+ Decoding: Decompress audio data using the Opus decoder
+ Playback: Play the decoded audio data through the speakers

## Example Usage
<!-- Explain how to use the example, such as connecting which hardware pins to observe waveforms, compilation and programming can reference related documents.
For rt_device examples, also need to list the configuration switches used by this example, such as PWM example using PWM1, need to enable PWM1 in onchip menu -->

### Hardware Requirements
Before running this example, prepare:
+ A development board supported by this example ([Supported Platforms](quick_start)).
+ Speaker.

### menuconfig Configuration

1. This example needs to read and write files, so it needs to use a file system. Configure the `FAT` file system:
![RT_USING_DFS_ELMFAT](./assets/mc_fat.png)

     ```{tip}
     Mount root partition in mnt_init.
     ```
2. Enable AUDIO CODEC and AUDIO PROC:
![AUDIO CODEC & PROC](./assets/mc_audcodec_audprc.png)
3. Enable AUDIO(`AUDIO`):
![AUDIO](./assets/mc_audio.png)
4. Enable AUDIO MANAGER.(`AUDIO_USING_MANAGER`)
![AUDIO_USING_MANAGER](./assets/mc_audio_manager.png)

### **Routine Description**

```c
If the opus_test() function contains opus_encoder_ctl(encoder, OPUS_SET_FORCE_MODE(MODE_SILK_ONLY));then define OPUS_STACK_SIZE as 20k.

If the opus_test() function does not call opus_encoder_ctl(encoder, OPUS_SET_FORCE_MODE(MODE_SILK_ONLY));, then define `OPUS_STACK_SIZE` as 200k.
```

### Compilation and Programming

Switch to the example project directory and run the scons command to execute compilation:

> scons --board=eh-lb525 -j32

Switch to the example `project/build_xx` directory and run `uart_download.bat`, select the port as prompted for download:

 >./uart_download.bat

>Uart Download

>please input the serial port num:5

For detailed steps on compilation and download, please refer to [Quick Start](quick_start).

## Expected Results of Example
<!-- Explain example running results, such as which LEDs will light up, which logs will be printed, to help users judge whether the example is running normally, running results can be explained step by step combined with code -->
After the example starts:
Start recording for ten seconds, then automatically play back after recording is complete. Expected successful recording and automatic playback.

## Exception Diagnosis


## Reference Documents
<!-- For rt_device examples, RT-Thread official website documentation provides detailed explanations, web links can be added here, for example, refer to RT-Thread's [RTC documentation](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/rtc/rtc) -->

## Update History
|Version |Date   |Release Notes |
|:---|:---|:---|
|0.0.1 |10/2024 |Initial version |
| | | |
| | | |
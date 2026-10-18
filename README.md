# DAW-Discord-Bridge
<img align="right" width="442"  alt="Discord-DAW-Bridge" src="https://github.com/user-attachments/assets/8cf2deb9-ffe9-499e-9f60-88e0274ea6af">

**Let your friends hear your DAW in your Discord stream.**

DAW-Discord-Bridge is a small, free plugin for Windows. Put it on your master channel, pick an output device, and the sound of your music software is sent to your Discord screen share, even if your DAW uses ASIO.

## What is this for?

When you share your screen in Discord, people often can't hear your DAW. Many music programs play their sound in a special way (ASIO) that goes around Windows, and Discord can only pick up sound that goes through Windows.

This plugin fixes that. It sends a **copy** of your DAW's sound through Windows, so Discord can hear it. Your normal DAW sound is not changed and keeps its low delay.

**PLEASE NOTE**: This Plugin should also work for other platforms where you screenshare your DAW, like Zoom or Skype. It has only been tested in discord though.

## Download and install

1. Go to the **[Releases page](../../releases/latest)** and download the newest `.zip` file.
2. Unzip it. You get a File called `DAW-Discord-Bridge.vst3` and the README.
3. Copy that folder to `C:\Program Files\Common Files\VST3\` or your VST-Plugin path of choice.
4. Open your DAW and rescan your plugins.

**PLEASE NOTE:** This plugin works on WINDOWS ONLY

### **Video Tutorial**
![Watch the tutorial](https://img.youtube.com/vi/4YWiLVWJyNY/maxresdefault.jpg)

Your browser, Windows or your virus scanner might warn you about this file.
This is common for new, free plugins. The file is not signed with a paid code-signing certificate, and very few people have downloaded it so far, so it may be called “uncommon” or even “dangerous”. The warning is about the missing signature, not about what the plugin does.

Only download the plugin from the Releases page of this repository.
All source code is public here, and the download is built automatically by GitHub from that code.
If you would rather not trust a download, you can build it yourself.
If your browser asks, choose Keep. If your virus scanner removes or blocks the file, you may have to allow it in the scanner first.

## Quick start

1. Put **DAW-Discord-Bridge** on your **master channel** as the last Plugin (if you wanna share all Audio from your DAW) or your desired Audio channels if you only wanna share specific ones.
2. Open the plugin and choose an **output device**. Pick one you are **not** listening to, otherwise you will hear everything twice.
3. Start your stream or screen share in Discord and make sure sound sharing is turned on.

The plugin shows a level meter and a green light when audio from your DAW arrives. If the meter moves but your friends hear nothing, check the device you chose and Discord's sound settings.

## The controls

- **Output device:** Where the copy of your sound is sent. Click *Refresh* if a device is missing from the list.
- **Signal from your DAW:** Shows that sound is arriving. The light is green while there is sound.
- **Stream volume:** How loud the stream copy is. This does not change your own sound. A built-in safety limiter stops the stream copy from getting harshly distorted if it gets too loud.
- **Stream buffer:** Five steps from *Less delay* to *Fewer dropouts*. It starts at the lowest delay (40 ms). If you or your friends hear crackling or stuttering in the stream, move the slider to the right one step at a time until it sounds clean.

## Troubleshooting

**My friends hear nothing.**
Check that the level meter in the plugin moves while your DAW plays. Then check that the output device is selected and that Discord's screen share has sound turned on. If you share only a single window, share your DAW, because the sound comes from your DAW program.

**Crackling or stuttering in the stream.**
Move the *Stream buffer* slider to the right, one step at a time.

**The sound in the stream is a bit behind the picture.**
Move the *Stream buffer* slider to the left. A small delay is normal.

**I hear everything twice.**
You chose the device you are listening on. Choose a different one that you do not use.

**The plugin does not show up in my DAW.**
You need a 64-bit DAW that supports VST3. Check that the `DAW-Discord-Bridge.vst3` folder is in `C:\Program Files\Common Files\VST3\` and rescan your plugins.

**The plugin says "Device not found".**
The device was unplugged or switched off. The plugin reconnects by itself as soon as it is back.

**Windows warns me about the file.**
The plugin is not signed with a paid certificate, which is normal for free projects. The source code is open and the file is built automatically on GitHub from that code.

**Does it work while I export (bounce) my project?**
Yes. While your DAW renders faster than real time, nothing is sent to the stream. The plugin continues when the export is done.

## Compatibility

Windows 10 and 11, 64-bit, VST3, stereo.

Tested in:

| DAW | Result |
| --- | --- |
| REAPER 7.69 | Works |
| Cubase 15 Pro | Works |
<!-- Add more rows here, for example: | FL Studio | Works | -->

It should work in any DAW that supports VST3, but only the DAWs listed above have been tested. If you try another one, please tell me in an issue.

## Found a bug or have a question?

Please [open an issue on GitHub](../../issues). It helps to write which DAW and which Windows version you use, and what you see.

## Build it yourself

You need Visual Studio 2022 (with the C++ desktop workload) and CMake.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The plugin is created in `build/DAWDiscordBridge_artefacts/Release/VST3/`.

You do not need to install anything to build it: when you upload this repository to GitHub, the *Actions* tab builds the plugin automatically and offers it as a download.

<details>
<summary>Technical details</summary>

- The plugin passes the audio through unchanged. It copies the signal into a lock-free ring buffer on the DAW's audio thread.
- A second audio thread reads from that buffer and plays it on the chosen device using WASAPI shared mode (called "Windows Audio" in JUCE). Windows sees this as normal audio of the DAW process, which is why Discord can capture it.
- If the DAW and the output device use different sample rates, the audio is converted. Small differences between the two clocks are corrected slowly.
- The stream buffer is 40, 80, 120, 160 or 200 ms. It only delays the stream copy, not your DAW output.
- Stream volume is smoothed to avoid clicks. After it, a soft clipper (starting at -2 dBFS) keeps the signal below 0 dBFS.
- The output device is opened on the message thread and watched in the background. If it disappears, the plugin reconnects automatically.
- During an offline render nothing is sent to the device.

</details>

## License and credits

This project is open source under the **GNU Affero General Public License v3.0 (AGPLv3)**. See the `LICENSE` file.

It is built with [JUCE](https://juce.com), which is used here under the AGPLv3 option of its license.

DAW-Discord-Bridge is not affiliated with or endorsed by Discord. Discord is a trademark of its owner.

The plugin is provided as is, without any warranty. Use it at your own risk.

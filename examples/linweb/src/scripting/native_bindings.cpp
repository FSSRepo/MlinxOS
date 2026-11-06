#include "scripting/native_bindings.h"
#include "scripting/audio_bindings.h"
#include "scripting/console_bindings.h"
#include "scripting/gamepad_bindings.h"
#include "scripting/input_bindings.h"
#include "scripting/install_bindings.h"
#include "scripting/js_engine.h"
#include "scripting/network_bindings.h"
#include "scripting/nodelite_bindings.h"
#include "scripting/perf_bindings.h"
#include "scripting/power_bindings.h"
#include "scripting/storage_bindings.h"
#include "scripting/sysinfo_bindings.h"
#include "scripting/video_bindings.h"

namespace linweb {

void install_native_bindings(JSEngine& js)
{
    js.add_function("__glRendererInfo", js_gl_renderer_info);
    js.add_function("__glDrainErrors", js_gl_drain_errors);
    js.add_function("__storageList", js_storage_list);
    js.add_function("__storageMount", js_storage_mount);
    js.add_function("__storageUnmount", js_storage_unmount);
    js.add_function("__storageFormat", js_storage_format);
    js.add_function("__storageListDir", js_storage_list_dir);
    js.add_function("__storageReadFile", js_storage_read_file);
    js.add_function("__storageWriteFile", js_storage_write_file);
    js.add_function("__storageMakeDir", js_storage_make_dir);
    js.add_function("__storageRemove", js_storage_remove);
    js.add_function("__audioCards", js_audio_cards);
    js.add_function("__audioGetVolume", js_audio_get_volume);
    js.add_function("__audioSetVolume", js_audio_set_volume);
    js.add_function("__audioSetMute", js_audio_set_mute);
    js.add_function("__audioPlay", js_audio_play);
    js.add_function("__audioStop", js_audio_stop);
    js.add_function("__audioPause", js_audio_pause);
    js.add_function("__audioResume", js_audio_resume);
    js.add_function("__audioStatus", js_audio_status);
    js.add_function("__wifiStatus", js_wifi_status);
    js.add_function("__wifiEnable", js_wifi_enable);
    js.add_function("__wifiScan", js_wifi_scan);
    js.add_function("__wifiNetworks", js_wifi_networks);
    js.add_function("__wifiConnect", js_wifi_connect);
    js.add_function("__wifiDisconnect", js_wifi_disconnect);
    js.add_function("__netAddresses", js_net_addresses);
    js.add_function("__webserverStatus", js_webserver_status);
    js.add_function("__webserverStart", js_webserver_start);
    js.add_function("__webserverStop", js_webserver_stop);
    js.add_function("__systemPowerOff", js_power_off);
    js.add_function("__systemInputDevices", js_input_devices);
    js.add_function("__gamepadOpen", js_gamepad_open);
    js.add_function("__gamepadRead", js_gamepad_read);
    js.add_function("__systemInstall", js_install_start);
    js.add_function("__systemInstallStatus", js_install_status);
    js.add_function("__systemRunsFromDisk", js_runs_from_disk);
    js.add_function("__systemInstallAvailable", js_install_available);
    js.add_function("__systemMounts", js_system_mounts);
    js.add_function("__systemMemory", js_system_memory);
    js.add_function("__perfStats", js_perf_stats);
    js.add_function("__perfToggle", js_perf_toggle);
    js.add_function("__nodeAvailable", js_node_available);
    js.add_function("__nodeStart", js_node_start);
    js.add_function("__nodePoll", js_node_poll);
    js.add_function("__nodeStop", js_node_stop);
    js.add_function("__videoPlay", js_video_play);
    js.add_function("__videoPause", js_video_pause);
    js.add_function("__videoResume", js_video_resume);
    js.add_function("__videoStop", js_video_stop);
    js.add_function("__videoSeek", js_video_seek);
    js.add_function("__videoSetLoop", js_video_set_loop);
    js.add_function("__videoStatus", js_video_status);

    js.execute(R"JS(
const linweb = {
  renderer: { info: () => __glRendererInfo() },
  debug: {
    renderer: () => { const info = __glRendererInfo(); console.info('OpenGL renderer', info); return info; },
    glErrors: () => { const errors = __glDrainErrors(); if (errors.length) console.error('OpenGL errors', errors); return errors; }
  },
  storage: {
    list: () => __storageList(),
    mount: (dev) => __storageMount(dev),
    unmount: (path) => __storageUnmount(path),
    format: (dev, fs, label, confirm) => __storageFormat(dev, fs, label || '', confirm || ''),
    readDir: (path) => __storageListDir(path),
    readFile: (path, maxBytes) => __storageReadFile(path, maxBytes || 0),
    writeFile: (path, data) => __storageWriteFile(path, String(data)),
    makeDir: (path) => __storageMakeDir(path),
    remove: (path) => __storageRemove(path)
  },
  perf: {
    stats: () => __perfStats(),
    toggle: () => __perfToggle()
  },
  gamepad: {
    open: () => __gamepadOpen(),
    read: () => __gamepadRead()
  },
  system: {
    powerOff: () => __systemPowerOff(),
    inputDevices: () => __systemInputDevices(),
    install: (disk, confirm) => __systemInstall(disk, confirm),
    installStatus: () => __systemInstallStatus(),
    runsFromDisk: () => __systemRunsFromDisk(),
    installAvailable: () => __systemInstallAvailable(),
    mounts: () => __systemMounts(),
    memory: () => __systemMemory()
  },
  audio: {
    cards: () => __audioCards(),
    getVolume: (card) => __audioGetVolume(card || 0),
    setVolume: (card, percent) => __audioSetVolume(card || 0, percent),
    setMute: (card, muted) => __audioSetMute(card || 0, muted ? 1 : 0),
    play: (path, card, marginMs) => __audioPlay(path, card === undefined ? -1 : card, marginMs || 1000),
    stop: () => __audioStop(),
    pause: () => __audioPause(),
    resume: () => __audioResume(),
    status: () => __audioStatus()
  },
  wifi: {
    status: () => __wifiStatus(),
    enable: () => __wifiEnable(),
    scan: () => __wifiScan(),
    networks: () => __wifiNetworks(),
    connect: (ssid, password) => __wifiConnect(ssid, password || ''),
    disconnect: () => __wifiDisconnect()
  },
  network: { addresses: () => __netAddresses() },
  webserver: {
    status: () => __webserverStatus(),
    start: (port) => __webserverStart(port || 8080),
    stop: () => __webserverStop()
  },
  node: {
    available: () => __nodeAvailable(),
    start: (script, args, timeoutMs) => __nodeStart('file', script, (args || []).join('\n'), timeoutMs || 60000),
    eval: (code, timeoutMs) => __nodeStart('eval', code, '', timeoutMs || 60000),
    poll: (id) => __nodePoll(id),
    stop: (id) => __nodeStop(id)
  },
  video: {
    play: (path, options) => __videoPlay(path, options && options.card !== undefined ? options.card : -1, options && options.loop ? 1 : 0),
    pause: () => __videoPause(),
    resume: () => __videoResume(),
    stop: () => __videoStop(),
    seek: (seconds) => __videoSeek(seconds),
    setLoop: (loop) => __videoSetLoop(loop ? 1 : 0),
    status: () => __videoStatus()
  }
};
)JS");
    // the page reaches the same object as window.linweb
    js.execute("__window.linweb = linweb;");
}

} // namespace linweb

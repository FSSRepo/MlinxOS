#include "natives.h"
#include "scripting/gamepad_bindings.h"
#include "scripting/gl_info.h"
#include "scripting/input_bindings.h"
#include "scripting/install_bindings.h"
#include "scripting/network_bindings.h"
#include "scripting/nodelite_bindings.h"
#include "scripting/perf_bindings.h"
#include "scripting/power_bindings.h"
#include "scripting/storage_bindings.h"
#include "scripting/sysinfo_bindings.h"

// The natives of the OS module and the `linweb` object the scripts call them through, also reachable as window.linweb.
// The audio and video groups of the old fork are gone: the engine follows the HTML standard for <audio>, <video> and
// new Audio(). The frame timings are the profiler of the engine, so the group only has toggle().
void register_system_natives(linweb::JSEngine& js)
{
    js.add_function("__glRendererInfo", linweb::js_gl_renderer_info);
    js.add_function("__glDrainErrors", linweb::js_gl_drain_errors);
    js.add_function("__storageList", linweb::js_storage_list);
    js.add_function("__storageMount", linweb::js_storage_mount);
    js.add_function("__storageUnmount", linweb::js_storage_unmount);
    js.add_function("__storageFormat", linweb::js_storage_format);
    js.add_function("__storageListDir", linweb::js_storage_list_dir);
    js.add_function("__storageReadFile", linweb::js_storage_read_file);
    js.add_function("__storageWriteFile", linweb::js_storage_write_file);
    js.add_function("__storageMakeDir", linweb::js_storage_make_dir);
    js.add_function("__storageRemove", linweb::js_storage_remove);
    js.add_function("__wifiStatus", linweb::js_wifi_status);
    js.add_function("__wifiEnable", linweb::js_wifi_enable);
    js.add_function("__wifiScan", linweb::js_wifi_scan);
    js.add_function("__wifiNetworks", linweb::js_wifi_networks);
    js.add_function("__wifiConnect", linweb::js_wifi_connect);
    js.add_function("__wifiDisconnect", linweb::js_wifi_disconnect);
    js.add_function("__netAddresses", linweb::js_net_addresses);
    js.add_function("__webserverStatus", linweb::js_webserver_status);
    js.add_function("__webserverStart", linweb::js_webserver_start);
    js.add_function("__webserverStop", linweb::js_webserver_stop);
    js.add_function("__systemPowerOff", linweb::js_power_off);
    js.add_function("__systemInputDevices", linweb::js_input_devices);
    js.add_function("__gamepadOpen", linweb::js_gamepad_open);
    js.add_function("__gamepadRead", linweb::js_gamepad_read);
    js.add_function("__systemInstall", linweb::js_install_start);
    js.add_function("__systemInstallStatus", linweb::js_install_status);
    js.add_function("__systemRunsFromDisk", linweb::js_runs_from_disk);
    js.add_function("__systemInstallAvailable", linweb::js_install_available);
    js.add_function("__systemMounts", linweb::js_system_mounts);
    js.add_function("__systemMemory", linweb::js_system_memory);
    js.add_function("__perfToggle", linweb::js_perf_toggle);
    js.add_function("__nodeAvailable", linweb::js_node_available);
    js.add_function("__nodeStart", linweb::js_node_start);
    js.add_function("__nodePoll", linweb::js_node_poll);
    js.add_function("__nodeStop", linweb::js_node_stop);

    js.execute(R"JS(
const linweb = {
  gl: {
    info: () => __glRendererInfo(),
    drainErrors: () => __glDrainErrors()
  },
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
  }
};
)JS");
    // the page reaches the same object as window.linweb
    js.execute("__window.linweb = linweb;");
}

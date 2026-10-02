// Runs the actual connection functions without opening display/audio devices.
#define main settings_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
namespace screen { uint32_t tick() { return 1000; } }
extern "C" int __real_execvp(const char *, char *const []);
extern "C" int __wrap_execvp(const char *name, char *const args[]) {
    const char *fake = std::getenv("C1_SETTINGS_TEST_WPA");
    if (fake && (std::string(name) == "/usr/sbin/wpa_cli" || std::string(name) == "/usr/bin/wpa_cli"))
        return __real_execvp(fake, args);
    return __real_execvp(name, args);
}
int main(int argc, char **argv) {
    assert(argc >= 2);
    const std::string mode = argv[1];
    if (mode == "device-cancel") {
        // Uses only a disposable, unreachable SSID. Never save_config on device.
        start_connect("C1Max-QA-unreachable-20261002", "", "C1Max-QA-password", false);
        if (job != Job::Connect) return 3;
        usleep(300000);
        cancel_connect();
    } else {
        start_connect("Home", mode == "new-cancel" ? "" : "0", mode == "saved-cancel" ? "" : "wrong-password", false);
        if (mode == "setup-fail" || mode == "select-fail" || mode == "read-fail") assert(job == Job::None);
        else {
            assert(job == Job::Connect);
            if (mode == "success" || mode == "save-fail") finish_connect(true, "");
            else if (mode == "wrong-password") finish_connect(false, "wrong password");
            else cancel_connect();
        }
        if (mode == "save-fail") assert(toast.find("保存失败") != std::string::npos);
    }
    assert(job == Job::None);
    puts("PASS actual Wi-Fi transaction");
}

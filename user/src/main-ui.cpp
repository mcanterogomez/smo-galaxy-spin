#include "custom/HideUI.h"

struct AppRunAlt : public mallow::hook::Trampoline<AppRunAlt> {
    static void Callback(void* thisPtr) {
        nn::fs::MountSdCardForDebug("sd");
        if (!mallow::config::loadConfig(true)) {
            mallow::config::useDefaultConfig();
            mallow::config::saveConfig();
        }
        mallow::config::readConfigToStruct();
        Orig(thisPtr);
    }
};

extern "C" void userMain() {
    AppRunAlt::InstallAtSymbol("_ZN11Application3runEv");
    HideUI::Install();
}
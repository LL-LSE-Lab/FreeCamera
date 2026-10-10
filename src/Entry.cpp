#include "Entry.h"
#include "Global.h"
#include "gmlib/gm/i18n/LangI18n.h"
#include "ll/api/Config.h"
#include "ll/api/Versions.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/service/Bedrock.h"
#include "mc/world/level/Level.h"
#include "resource.h"

namespace FreeCamera {

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

bool Entry::load() { return true; }

bool Entry::enable() {
    mConfig.emplace();
    auto const configPath = getSelf().getConfigDir() / u8"config.json";
    try {
        if (!ll::config::loadConfig(*mConfig, configPath)) {
            ll::config::saveConfig(*mConfig, configPath);
        }
    } catch (std::exception const& e) {
        // A config file that cannot be deserialized (e.g. hand edited, or written by a build with a
        // field this one does not know) must not stop the mod from loading.
        getSelf().getLogger().error("[{}] failed to load config, using defaults: {}", MOD_NAME, e.what());
        mConfig.emplace();
        ll::config::saveConfig(*mConfig, configPath);
    }
    FreeCamera::reloadPacketOptions();
    mI18n.emplace(getSelf().getLangDir(), mConfig->language);
    mI18n->updateOrCreateLanguage("en_US", en_US);
    mI18n->updateOrCreateLanguage("zh_CN", zh_CN);
    mI18n->loadAllLanguages();
    FreeCamera::freecameraHook(true);
    RegisterCommand();
    return true;
}

bool Entry::disable() {
    FreeCamList.clear();
    ll::service::getLevel()->forEachPlayer([](Player& pl) -> bool {
        FreeCamera::DisableFreeCamera(&pl);
        return true;
    });
    return true;
}

bool Entry::unload() {
    mConfig.reset();
    mI18n.reset();
    FreeCamera::freecameraHook(false);
    return true;
}

Config& Entry::getConfig() { return mConfig.value(); }

void reloadPacketOptions() {
    auto& entry  = Entry::getInstance();
    auto& config = entry.getConfig();
    try {
        ll::config::loadConfig(config, entry.getSelf().getConfigDir() / u8"config.json");
    } catch (std::exception const& e) {
        entry.getSelf().getLogger().error("[{}] packet options reload failed: {}", MOD_NAME, e.what());
    }
    packetSerializationMode   = parseSerializationMode(config.packetSerializationMode);
    sendGamemodeSwitch        = config.sendGamemodeSwitch;
    sendFakePlayerListAdd     = config.sendFakePlayerListAdd;
    sendFakePlayerListRemove  = config.sendFakePlayerListRemove;
    sendFakeAddPlayer         = config.sendFakeAddPlayer;
    clearAnchorIdentity       = config.clearAnchorIdentity;
    preferClientSkin          = config.preferClientSkin;
    sendFakePlayerSkin        = config.sendFakePlayerSkin;
    sendSelfSkinRefresh       = config.sendSelfSkinRefresh;
    refreshAnchorOnSkinChange = config.refreshAnchorOnSkinChange;
    skinDiagnostics           = config.skinDiagnostics;
    entry.getSelf().getLogger().debug(
        "[{}] packet options: mode={} gamemodeSwitch={} playerListAdd={} playerListRemove={} addPlayer={} "
        "clearAnchorIdentity={} preferClientSkin={} fakePlayerSkin={} selfSkinRefresh={} refreshOnSkinChange={} "
        "skinDiagnostics={}",
        MOD_NAME,
        config.packetSerializationMode,
        sendGamemodeSwitch,
        sendFakePlayerListAdd,
        sendFakePlayerListRemove,
        sendFakeAddPlayer,
        clearAnchorIdentity,
        preferClientSkin,
        sendFakePlayerSkin,
        sendSelfSkinRefresh,
        refreshAnchorOnSkinChange,
        skinDiagnostics
    );
}

gmlib::i18n::LangI18n& Entry::getI18n() { return mI18n.value(); }

} // namespace FreeCamera

LL_REGISTER_MOD(FreeCamera::Entry, FreeCamera::Entry::getInstance());
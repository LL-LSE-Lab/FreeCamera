#include "Global.h"
#include "gmlib/mc/world/actor/Player.h"
#include "ll/api/io/Logger.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/AddPlayerPacket.h"
#include "mc/network/packet/PlayerListPacket.h"
#include "mc/network/packet/PlayerListPacketPayload.h"
#include "mc/network/packet/PlayerListPacketType.h"
#include "mc/network/packet/PlayerSkinPacket.h"
#include "mc/network/packet/RemoveActorPacket.h"
#include "mc/network/packet/UpdateAbilitiesPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/SerializedAbilitiesData.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/PlayerListManager.h"

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

std::unordered_set<uint64> FreeCamList;

namespace FreeCamera {

SerializationMode packetSerializationMode = SerializationMode::CerealOnly;

bool sendGamemodeSwitch        = true;
bool sendFakePlayerListAdd     = true;
bool sendFakePlayerListRemove  = true;
bool sendFakeAddPlayer         = true;
bool clearAnchorIdentity       = true;
bool preferClientSkin          = true;
bool sendFakePlayerSkin        = true;
bool sendSelfSkinRefresh       = true;
bool refreshAnchorOnSkinChange = true;
bool skinDiagnostics           = false;

SerializationMode parseSerializationMode(std::string const& name) {
    if (name == "manualOnly") return SerializationMode::ManualOnly;
    if (name == "sideBySide") return SerializationMode::SideBySideLogOnMismatch;
    return SerializationMode::CerealOnly;
}

namespace {

/// Offset added to the real actor unique id to build the client side anchor entity id.
constexpr int64 kFakeEntityIdOffset = 114514;

[[nodiscard]] ll::io::Logger& logger() { return ll::mod::NativeMod::current()->getLogger(); }

/// Latest skin each client reported about itself, keyed by network guid.
///
/// The server caches a player's own skin, but the direction that matters here is the other way
/// round: the client caches the appearance of every identity it has seen, and it is never sent its
/// own skin update, so that cache entry can stay on the skin the player logged in with. Everything
/// the client uploads is recorded here so the anchor can be built from the same data the client
/// believes it is wearing.
std::unordered_map<uint64, std::shared_ptr<SerializedSkinRef>> lastClientSkin;

[[nodiscard]] uint64 guidOf(Player const& pl) { return pl.getNetworkIdentifier().mGuid.g; }

[[nodiscard]] ActorUniqueID makeFakeUniqueID(Player const& pl) {
    ActorUniqueID id  = pl.getOrCreateUniqueID();
    id.rawID         += kFakeEntityIdOffset;
    return id;
}

/// Deterministic per player identity for the client side anchor, so the enable and the disable path
/// agree on it without keeping extra state.
[[nodiscard]] mce::UUID makeFakeUUID(Player const& pl) {
    return mce::UUID::fromStringHash(std::string{pl.getUuid().asString()} + "/freecamera");
}

/// Display name of the anchor. It must differ from the real player's name, otherwise the client
/// binds its local player to the anchor, which then has no chunk publisher: the world stops
/// rendering and the client dies. Kept within the 16 character name limit.
[[nodiscard]] std::string makeFakeName(Player const& pl) {
    std::string name = pl.getRealName();
    if (name.size() > 13) {
        name.resize(13);
    }
    return name + "*FC";
}

/// The serialized skin behind a ref. Only used to compare sources and to log what was sent; never
/// mutate anything reachable through it (the impl is shared with the real player's skin).
[[nodiscard]] SerializedSkinImpl const& skinImplOf(SerializedSkinRef const& skin) {
    return skin.mSkinImpl.get()->mObject;
}

[[nodiscard]] PlayerListEntry* tryGetListEntry(Player const& pl) {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }
    return level->getPlayerListManager()->tryGetPlayerEntry(pl.getUuid());
}

[[nodiscard]] Player* findPlayerByGuid(uint64 guid) {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }
    Player* found = nullptr;
    level->forEachPlayer([&](Player& pl) -> bool {
        if (guidOf(pl) == guid) {
            found = &pl;
            return false;
        }
        return true;
    });
    return found;
}

void rememberClientSkin(uint64 guid, SerializedSkinRef const& skin) {
    auto& slot = lastClientSkin[guid];
    if (!slot) {
        slot = std::make_shared<SerializedSkinRef>();
    }
    *slot = skin;
}

struct AnchorSkin {
    SerializedSkinRef const* skin   = nullptr;
    char const*              source = "default";
};

/// Picks the freshest skin known for @p pl. The client's own upload wins: the entry in the player
/// list and `Player::mSkin` are both server side copies that a skin change is not guaranteed to
/// update.
[[nodiscard]] AnchorSkin anchorSkinOf(Player const& pl) {
    if (preferClientSkin) {
        auto it = lastClientSkin.find(guidOf(pl));
        if (it != lastClientSkin.end() && it->second && it->second->isValid()) {
            return {it->second.get(), "client"};
        }
    }
    if (auto* entry = tryGetListEntry(pl); entry != nullptr && entry->mSkin.get().isValid()) {
        return {&entry->mSkin.get(), "listEntry"};
    }
    if (pl.mSkin && pl.mSkin->isValid()) {
        return {pl.mSkin.get(), "player"};
    }
    return {};
}

/// Sends @p skin for @p uuid as a `PlayerSkinPacket`.
///
/// `mSerializationMode` is deliberately not assigned: unlike `PlayerListPacket` /
/// `RemoveActorPacket`, `PlayerSkinPacket` does declare a constructor, so the engine initialises
/// that member for us (the engine sends this packet itself).
void sendSkinPacket(Player* pl, mce::UUID const& uuid, SerializedSkinRef const& skin) {
    PlayerSkinPacket pkt;
    pkt.mUUID.get() = uuid;
    pkt.mSkin.get() = skin;
    // Empty localized names: a non empty name makes the client show a "changed skin" toast.
    pkt.mLocalizedNewSkinName.get().clear();
    pkt.mLocalizedOldSkinName.get().clear();

    if (skinDiagnostics) {
        // `toString()` is engine code, so it formats the payload through the *real* offsets: a
        // mismatch between the SDK header and BDS would show up here (the same class of bug as the
        // hand built `SetPlayerGameTypePacket`, see BUILD.md).
        std::string engineView = pkt.toString();
        if (engineView.size() > 240) {
            engineView.resize(240);
        }
        logger().info(
            "[{}] PlayerSkinPacket for {}: runtimeMode={} skinValid={} engineView={}",
            MOD_NAME,
            uuid.asString(),
            static_cast<int>(pkt.getSerializationMode()),
            pkt.mSkin.get().isValid(),
            engineView
        );
    }
    pl->sendNetworkPacket(pkt);
}

/// Fills a fake player list entry with the anchor's identity and @p skin.
///
/// The anchor has to own every part of its identity (`BUILD.md`): when the entry keeps the real
/// player's XUID the client resolves the anchor to the real player's identity and renders the
/// appearance it has cached for it, which for the owning client is the skin it logged in with.
void fillFakeEntry(
    PlayerListPacketPayload::AddEntry& entry,
    mce::UUID const&                   fakeUuid,
    ActorUniqueID                      fakeUniqueId,
    std::string const&                 fakeName,
    SerializedSkinRef const*           skin
) {
    entry.mUUID       = fakeUuid;
    entry.mId         = fakeUniqueId;
    entry.mName.get() = fakeName;

    if (clearAnchorIdentity) {
        entry.mXUID.get().clear();
        entry.mPlatformOnlineId.get().clear();
        entry.mIsTeacher   = false;
        entry.mIsHost      = false;
        entry.mIsSubClient = false;
    }

    if (skin != nullptr && skin->isValid()) {
        entry.mSkin.get() = *skin;
    } else {
        entry.mSkin.get() = SerializedSkinRef::createTrustedDefaultSerializedSkin();
    }
}

void sendFakeListAdd(Player* pl, mce::UUID const& fakeUuid, ActorUniqueID fakeUniqueId, SerializedSkinRef const* skin) {
    PlayerListPacket listPkt;
    listPkt.mSerializationMode = packetSerializationMode;

    PlayerListPacketPayload::AddEntry addEntry{PlayerListEntry(*pl)};
    fillFakeEntry(addEntry, fakeUuid, fakeUniqueId, makeFakeName(*pl), skin);

    listPkt.mEntries.get().emplace_back(std::move(addEntry));
    pl->sendNetworkPacket(listPkt);
}

void logAnchorSkin(Player const& pl, AnchorSkin const& chosen) {
    auto* entry = tryGetListEntry(pl);

    SerializedSkinRef const* listSkin =
        (entry != nullptr && entry->mSkin.get().isValid()) ? &entry->mSkin.get() : nullptr;
    SerializedSkinRef const* playerSkin = (pl.mSkin && pl.mSkin->isValid()) ? pl.mSkin.get() : nullptr;

    if (chosen.skin == nullptr) {
        logger().info(
            "[{}] anchor skin: source={} (no valid skin found, the trusted default is used)",
            MOD_NAME,
            chosen.source
        );
        return;
    }

    bool const  equalToList   = listSkin != nullptr && skinImplOf(*chosen.skin) == skinImplOf(*listSkin);
    bool const  equalToPlayer = playerSkin != nullptr && skinImplOf(*chosen.skin) == skinImplOf(*playerSkin);
    auto const& impl          = skinImplOf(*chosen.skin);

    logger().info(
        "[{}] anchor skin: source={} equalToList={} equalToPlayer={} id={} fullId={} playFabId={} profileHash={} "
        "image={}x{} persona={} trusted={} identityCleared={}",
        MOD_NAME,
        chosen.source,
        equalToList,
        equalToPlayer,
        impl.mId.get(),
        impl.mFullId.get(),
        impl.mPlayFabId.get(),
        impl.mProfileHash.get(),
        impl.mSkinImage.get().mWidth,
        impl.mSkinImage.get().mHeight,
        impl.mIsPersona,
        static_cast<int>(impl.mIsTrustedSkin),
        clearAnchorIdentity
    );
}

/// Pushes @p skin onto an anchor that already exists. The anchor entity itself is left alone on
/// purpose: removing it would detach the camera and its chunk publisher.
void refreshAnchorSkin(Player* pl, SerializedSkinRef const& skin) {
    if (!skin.isValid()) {
        return;
    }
    auto fakeUuid = makeFakeUUID(*pl);

    if (sendSelfSkinRefresh) {
        sendSkinPacket(pl, pl->getUuid(), skin);
    }
    if (sendFakePlayerListAdd) {
        sendFakeListAdd(pl, fakeUuid, makeFakeUniqueID(*pl), &skin);
    }
    if (sendFakePlayerSkin) {
        sendSkinPacket(pl, fakeUuid, skin);
    }
    logger().info("[{}] anchor skin refreshed for {}", MOD_NAME, pl->getRealName());
}

/// Runs @p fn and turns any escaped exception into a log entry instead of taking the server down.
template <class Fn>
void guard(char const* what, Fn&& fn) {
    try {
        fn();
    } catch (std::exception const& e) {
        logger().error("[{}] {} threw: {}", MOD_NAME, what, e.what());
    } catch (...) {
        logger().error("[{}] {} threw an unknown exception", MOD_NAME, what);
    }
}

} // namespace

void EnableFreeCameraPacket(Player* pl) {
    if (!sendGamemodeSwitch) {
        return;
    }
    // Do not build `SetPlayerGameTypePacket` by hand. In the LeviLamina 26.51.6 headers its payload
    // and `mSerializationMode` are transposed relative to the real BDS layout (the engine's own
    // `getSerializationMode()` reads the offset the header calls `mPlayerGameType`), so a hand built
    // packet goes out with an out of range SerializationMode. GMLIB is compiled against the real
    // layout, so let it build the packet; it sends UpdatePlayerGameTypePacket.
    static_cast<gmlib::GMPlayer*>(pl)->setClientGamemode(GameType::Spectator);
}

void SendFakePlayerPacket(Player* pl) {
    auto fakeUuid = makeFakeUUID(*pl);
    auto fakeName = makeFakeName(*pl);
    auto uniqueId = makeFakeUniqueID(*pl);
    auto skin     = anchorSkinOf(*pl);

    if (skinDiagnostics) {
        logAnchorSkin(*pl, skin);
    }

    // Refresh the client's own identity *before* the anchor entry is added: adding the entry first
    // would let the client resolve the anchor's appearance from the stale login skin.
    if (sendSelfSkinRefresh && skin.skin != nullptr) {
        sendSkinPacket(pl, pl->getUuid(), *skin.skin);
    }

    if (sendFakePlayerListAdd) {
        // `AddPlayerPacketPayload` carries no skin: the client builds the anchor's model from the
        // skin of the player list entry whose uuid matches the anchor, so that entry has to exist and
        // has to hold a valid skin.
        sendFakeListAdd(pl, fakeUuid, uniqueId, skin.skin);
    }

    if (sendFakeAddPlayer) {
        auto pkt = AddPlayerPacket(*pl);

        // The anchor must not claim any part of the real player's identity, otherwise the client
        // binds its local player / camera to it and the world stops rendering.
        pkt.mRuntimeId  = ActorRuntimeID{static_cast<uint64>(uniqueId.rawID)};
        pkt.mUuid       = fakeUuid;
        pkt.mName.get() = fakeName;
        pkt.mPlatformOnlineId.get().clear();
        pkt.mDeviceId.get().clear();
        pkt.mLinks.get().clear();
        pkt.mAbilitiesData.get() = SerializedAbilitiesData{uniqueId, pl->getAbilities()};

        pl->sendNetworkPacket(pkt);
    }

    // Apply the skin to the entity once it exists, so the client does not have to resolve it while
    // creating the anchor.
    if (sendFakePlayerSkin && skin.skin != nullptr) {
        sendSkinPacket(pl, fakeUuid, *skin.skin);
    }

    logger().info(
        "[{}] anchor packet sent: uuid={} id={} name={} skinSource={} skinValid={}",
        MOD_NAME,
        fakeUuid.asString(),
        uniqueId.rawID,
        fakeName,
        skin.skin != nullptr ? skin.source : "default",
        skin.skin != nullptr && skin.skin->isValid()
    );
}

void DisableFreeCameraPacket(Player* pl) {
    if (sendGamemodeSwitch) {
        static_cast<gmlib::GMPlayer*>(pl)->setClientGamemode(pl->getPlayerGameType());
    }

    if (sendFakeAddPlayer) {
        RemoveActorPacket removePkt;
        removePkt.mSerializationMode = packetSerializationMode;
        removePkt.mEntityId          = makeFakeUniqueID(*pl);
        pl->sendNetworkPacket(removePkt);
    }

    if (sendFakePlayerListAdd && sendFakePlayerListRemove) {
        PlayerListPacket listPkt;
        listPkt.mSerializationMode = packetSerializationMode;

        PlayerListPacketPayload::RemoveEntry entry{};
        entry.mAction = PlayerListPacketType::Remove;
        entry.mUUID   = makeFakeUUID(*pl);

        listPkt.mEntries.get().emplace_back(entry);
        pl->sendNetworkPacket(listPkt);
    }

    UpdateAbilitiesPacket abilitiesPkt(pl->getOrCreateUniqueID(), pl->getAbilities());
    pl->sendNetworkPacket(abilitiesPkt);
}

void EnableFreeCamera(Player* pl) {
    FreeCamList.insert(guidOf(*pl));
    EnableFreeCameraPacket(pl);
    SendFakePlayerPacket(pl);
    logger().info("[{}] free camera enabled for {}", MOD_NAME, pl->getRealName());
}

void DisableFreeCamera(Player* pl) {
    auto pos   = pl->getFeetPos();
    auto dimid = pl->getDimensionId();
    FreeCamList.erase(guidOf(*pl));
    DisableFreeCameraPacket(pl);
    pl->teleport(pos, dimid);
    logger().info("[{}] free camera disabled for {}", MOD_NAME, pl->getRealName());
}

LL_TYPE_INSTANCE_HOOK(
    ServerPlayerMoveHandleEvent,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     id,
    PlayerAuthInputPacket const& pkt
) {
    if (FreeCamList.contains(id.mGuid.g)) {
        return;
    } else {
        return origin(id, pkt);
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerSkinChangeEvent,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const& id,
    PlayerSkinPacket const&  pkt
) {
    origin(id, pkt);

    auto* pl = findPlayerByGuid(id.mGuid.g);
    if (pl == nullptr) {
        return;
    }

    SerializedSkinRef const* skin = nullptr;
    if (pkt.mSkin.get().isValid()) {
        skin = &pkt.mSkin.get();
    } else if (pl->mSkin && pl->mSkin->isValid()) {
        skin = pl->mSkin.get();
    }
    if (skin == nullptr) {
        return;
    }

    rememberClientSkin(id.mGuid.g, *skin);

    if (skinDiagnostics) {
        bool const playerSkinUpdated = pl->mSkin && pl->mSkin->isValid() && skinImplOf(*pl->mSkin) == skinImplOf(*skin);
        logger().info(
            "[{}] skin change for {}: playerSkinUpdated={} packetSkinValid={}",
            MOD_NAME,
            pl->getRealName(),
            playerSkinUpdated,
            pkt.mSkin.get().isValid()
        );
    }

    if (refreshAnchorOnSkinChange && FreeCamList.contains(id.mGuid.g)) {
        guard("refreshAnchorSkin", [&] { refreshAnchorSkin(pl, *skin); });
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerGamemodeChangeEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$setPlayerGameType,
    void,
    ::GameType gamemode
) {
    origin(gamemode);
    if (FreeCamList.contains(getNetworkIdentifier().mGuid.g)) {
        guard("setPlayerGameType", [this] { DisableFreeCamera(this); });
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerHurtEvent,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$_hurt,
    ::ActorHurtResult,
    ::ActorDamageSource const& source,
    float                      damage,
    ::HurtParameters const&    hurtParameters
) {
    // `Mob::getDamageAfterResistanceEffect`, used before BDS 1.26.51, is gone; `Player::_hurt` is the
    // remaining virtual entry point of the damage path.
    if (damage > 0 && (isSurvival() || isAdventure()) && FreeCamList.contains(getNetworkIdentifier().mGuid.g)) {
        guard("_hurt", [this] { DisableFreeCamera(this); });
    }
    return origin(source, damage, hurtParameters);
}

LL_TYPE_INSTANCE_HOOK(
    PlayerDieEvent,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$die,
    void,
    ::ActorDamageSource const& source
) {
    if (FreeCamList.contains(getNetworkIdentifier().mGuid.g)) {
        guard("die", [this] { DisableFreeCamera(this); });
    }
    return origin(source);
}

LL_TYPE_INSTANCE_HOOK(
    PlayerLeftEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::disconnect,
    void
) {
    FreeCamList.erase(getNetworkIdentifier().mGuid.g);
    lastClientSkin.erase(getNetworkIdentifier().mGuid.g);
    return origin();
}

struct Impl {
    ll::memory::HookRegistrar<
        ServerPlayerMoveHandleEvent,
        PlayerSkinChangeEvent,
        PlayerGamemodeChangeEvent,
        PlayerHurtEvent,
        PlayerDieEvent,
        PlayerLeftEvent>
        r;
};

std::unique_ptr<Impl> impl;

void freecameraHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<Impl>();
    } else {
        impl.reset();
        lastClientSkin.clear();
    }
}

} // namespace FreeCamera

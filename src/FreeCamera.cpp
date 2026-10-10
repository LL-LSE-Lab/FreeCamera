#include "Global.h"
#include "gmlib/mc/world/actor/Player.h"
#include "ll/api/io/Logger.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/AddPlayerPacket.h"
#include "mc/network/packet/PlayerListPacket.h"
#include "mc/network/packet/PlayerListPacketPayload.h"
#include "mc/network/packet/PlayerListPacketType.h"
#include "mc/network/packet/RemoveActorPacket.h"
#include "mc/network/packet/UpdateAbilitiesPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/SerializedAbilitiesData.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/actor/player/SerializedSkinRef.h"

#include <exception>
#include <memory>
#include <utility>

std::unordered_set<uint64> FreeCamList;

namespace FreeCamera {

SerializationMode packetSerializationMode = SerializationMode::CerealOnly;

bool sendGamemodeSwitch       = true;
bool sendFakePlayerListAdd    = true;
bool sendFakePlayerListRemove = true;
bool sendFakeAddPlayer        = true;

SerializationMode parseSerializationMode(std::string const& name) {
    if (name == "manualOnly") return SerializationMode::ManualOnly;
    if (name == "sideBySide") return SerializationMode::SideBySideLogOnMismatch;
    return SerializationMode::CerealOnly;
}

namespace {

/// Offset added to the real actor unique id to build the client side anchor entity id.
constexpr int64 kFakeEntityIdOffset = 114514;

[[nodiscard]] ll::io::Logger& logger() { return ll::mod::NativeMod::current()->getLogger(); }

[[nodiscard]] ActorUniqueID makeFakeUniqueID(Player const& pl) {
    ActorUniqueID id = pl.getOrCreateUniqueID();
    id.rawID += kFakeEntityIdOffset;
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
    bool skinOk   = false;

    if (sendFakePlayerListAdd) {
        PlayerListPacket listPkt;
        listPkt.mSerializationMode = packetSerializationMode;

        PlayerListPacketPayload::AddEntry addEntry{PlayerListEntry(*pl)};

        // `AddPlayerPacketPayload` carries no skin: the client builds the anchor's model from the
        // skin of the player list entry whose uuid matches the anchor, so that entry has to exist and
        // has to hold a valid skin.
        bool const playerSkinValid = pl->mSkin != nullptr && pl->mSkin->isValid();
        if (playerSkinValid) {
            addEntry.mSkin.get() = *pl->mSkin;
        } else {
            addEntry.mSkin.get() = SerializedSkinRef::createTrustedDefaultSerializedSkin();
        }
        skinOk = addEntry.mSkin.get().isValid();

        addEntry.mUUID       = fakeUuid;
        addEntry.mId         = uniqueId;
        addEntry.mName.get() = fakeName;

        listPkt.mEntries.get().emplace_back(std::move(addEntry));
        pl->sendNetworkPacket(listPkt);
    }

    if (sendFakeAddPlayer) {
        auto pkt = AddPlayerPacket(*pl);

        // The anchor must not claim any part of the real player's identity, otherwise the client
        // binds its local player / camera to it and the world stops rendering.
        pkt.mRuntimeId         = ActorRuntimeID{static_cast<uint64>(uniqueId.rawID)};
        pkt.mUuid              = fakeUuid;
        pkt.mName.get()        = fakeName;
        pkt.mPlatformOnlineId.get().clear();
        pkt.mDeviceId.get().clear();
        pkt.mLinks.get().clear();
        pkt.mAbilitiesData.get() = SerializedAbilitiesData{uniqueId, pl->getAbilities()};

        pl->sendNetworkPacket(pkt);
    }

    logger().info(
        "[{}] anchor packet sent: uuid={} id={} name={} skinValid={}",
        MOD_NAME,
        fakeUuid.asString(),
        uniqueId.rawID,
        fakeName,
        skinOk
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
    FreeCamList.insert(pl->getNetworkIdentifier().mGuid.g);
    EnableFreeCameraPacket(pl);
    SendFakePlayerPacket(pl);
    logger().info("[{}] free camera enabled for {}", MOD_NAME, pl->getRealName());
}

void DisableFreeCamera(Player* pl) {
    auto pos   = pl->getFeetPos();
    auto dimid = pl->getDimensionId();
    FreeCamList.erase(pl->getNetworkIdentifier().mGuid.g);
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

LL_TYPE_INSTANCE_HOOK(PlayerLeftEvent, ll::memory::HookPriority::Normal, ServerPlayer, &ServerPlayer::disconnect, void) {
    FreeCamList.erase(getNetworkIdentifier().mGuid.g);
    return origin();
}

struct Impl {
    ll::memory::HookRegistrar<
        ServerPlayerMoveHandleEvent,
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
    }
}

} // namespace FreeCamera

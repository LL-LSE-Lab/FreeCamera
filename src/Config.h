#pragma once
#include <string>

namespace FreeCamera {

struct Config {
    /// Bump whenever a field is added: `ll::config::loadConfig` only runs its merge-updater when the
    /// version stored in `config.json` differs from this one, otherwise a config file written by an
    /// older build fails to deserialize (`missing required field ...`).
    int version = 4;

    std::string language = "zh_CN";

    /// Serialization mode used for every packet this mod builds by hand.
    ///
    /// Accepted values:
    ///   "cerealOnly"  - SerializationMode::CerealOnly               (default)
    ///   "sideBySide"  - SerializationMode::SideBySideLogOnMismatch
    ///   "manualOnly"  - SerializationMode::ManualOnly
    ///
    /// Since BDS 1.26.51 packets are `ll::PayloadPacket<Payload>` and carry an extra
    /// `mSerializationMode` field that several of them (`PlayerListPacket`, `RemoveActorPacket`)
    /// do not initialize in any constructor. The engine itself uses `CerealOnly` for the same
    /// packets, which is why that is the default here.
    std::string packetSerializationMode = "cerealOnly";

    // ---------------------------------------------------------------------------------------
    // Individual packet switches. They exist so a misbehaving packet can be isolated without a
    // rebuild: edit `config.json` and press `/fc` again (the options are re-read on every use).
    // ---------------------------------------------------------------------------------------

    /// Send the spectator game mode switch (through GMLIB's GMPlayer::setClientGamemode).
    bool sendGamemodeSwitch = true;

    /// Send the PlayerListPacket that registers the anchor player (and carries its skin) before the
    /// AddPlayerPacket. The client resolves the anchor's skin through this entry, so it must be sent
    /// first and must stay in the list while the anchor entity exists.
    bool sendFakePlayerListAdd = true;

    /// Send the matching PlayerListPacket remove. It is sent when free camera is *disabled*, not
    /// right after the add: removing the entry in the same tick makes the client crash while it
    /// resolves the anchor entity's skin.
    bool sendFakePlayerListRemove = true;

    /// Send the AddPlayerPacket that creates the client side anchor entity.
    bool sendFakeAddPlayer = true;
};

} // namespace FreeCamera

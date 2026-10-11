#pragma once
#include <string>

namespace FreeCamera {

struct Config {
    /// Bump whenever a field is added: `ll::config::loadConfig` only runs its merge-updater when the
    /// version stored in `config.json` differs from this one, otherwise a config file written by an
    /// older build fails to deserialize (`missing required field ...`).
    int version = 6;

    std::string language = "zh_CN";

    /// Log routine lifecycle information such as connection, anchor and free-camera transitions.
    /// Errors, warnings and opt-in skin diagnostics are not controlled by this switch.
    bool normalLogging = false;

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

    /// Clear the real player's identity out of the fake player list entry (`mXUID`,
    /// `mPlatformOnlineId`, host/sub-client flags).
    ///
    /// The client resolves the anchor model through the player list entry, and it matches entries
    /// to the player identity it already knows. When the entry keeps the real player's XUID the
    /// client resolves the anchor to the *real* player's cached appearance instead of the entry we
    /// just sent, and the owning client's own entry is never refreshed by the server (it is not
    /// sent its own skin change), so the anchor then shows the skin from the moment the player
    /// logged in. `AddPlayerPacket` already clears the same fields; this switch does it for the
    /// player list entry too. See BUILD.md, "The anchor entity must not reuse the player's
    /// identity".
    bool clearAnchorIdentity = true;

    /// Prefer the skin the owning client itself last uploaded (captured from the serverbound
    /// `PlayerSkinPacket`) over the skin cached on the server.
    bool preferClientSkin = true;

    /// After the anchor entity exists, send a `PlayerSkinPacket` for the anchor's uuid so the
    /// client applies the skin to the entity instead of resolving it once at creation time.
    bool sendFakePlayerSkin = true;

    /// Send a `PlayerSkinPacket` for the *real* player uuid to the player itself before adding the
    /// anchor entry. Vanilla never sends a player its own skin update, so that entry can still hold
    /// the login skin; this refreshes it (this is what the anchor may resolve through).
    bool sendSelfSkinRefresh = true;

    /// When a player in free camera changes skin, refresh the anchor's skin in place (no entity
    /// recreation: removing the anchor would detach the camera and its chunk publisher).
    bool refreshAnchorOnSkinChange = true;

    /// Log which skin source the anchor was built from and whether the candidates agree. Only
    /// needed to diagnose a wrong anchor skin.
    bool skinDiagnostics = false;
};

} // namespace FreeCamera

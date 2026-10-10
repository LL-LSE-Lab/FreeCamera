#pragma once
#include <mc/network/packet/cerealize/core/SerializationMode.h>
#include <mc/world/actor/player/Player.h>
#include <string>
#include <unordered_set>

#define MOD_NAME "FreeCamera"

extern std::unordered_set<uint64> FreeCamList;

extern void RegisterCommand();

namespace FreeCamera {

/**
 * @brief Serialization mode used for every packet this mod builds by hand.
 *
 * @details Since BDS 1.26.51 packets are `ll::PayloadPacket<Payload>` and carry an extra
 * `mSerializationMode` member. `PlayerListPacket` and `RemoveActorPacket` declare no constructor
 * at all, so a default constructed instance leaves that field indeterminate and it must always be
 * assigned before the packet is sent. The engine itself uses `CerealOnly` for both.
 *
 * The value is read from `config.json` (`packetSerializationMode`).
 */
extern ::SerializationMode packetSerializationMode;

/// See Config::sendGamemodeSwitch / sendFakePlayerListAdd / sendFakePlayerListRemove /
/// sendFakeAddPlayer.
extern bool sendGamemodeSwitch;
extern bool sendFakePlayerListAdd;
extern bool sendFakePlayerListRemove;
extern bool sendFakeAddPlayer;

/// Re-reads the packet switches from `config.json`. Called on every `/fc` so the switches can be
/// changed without restarting the server.
void reloadPacketOptions();

/// Maps "manualOnly" / "sideBySide" / "cerealOnly" onto the enum; anything else becomes
/// SerializationMode::CerealOnly.
[[nodiscard]] ::SerializationMode parseSerializationMode(std::string const& name);

extern void DisableFreeCamera(Player* pl);
extern void EnableFreeCamera(Player* pl);
extern void freecameraHook(bool enable);
} // namespace FreeCamera

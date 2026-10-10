#include "Entry.h"
#include "Global.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/io/Logger.h"
#include "ll/api/service/Bedrock.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"

#include <exception>

void RegisterCommand() {
    auto& cmd = ll::command::CommandRegistrar::getServerInstance()
                    .getOrCreateCommand("freecamera", "freecamera.command.desc"_tr(), CommandPermissionLevel::Any);
    ll::service::getCommandRegistry()->registerAlias("freecamera", "fc");
    cmd.overload().execute([&](CommandOrigin const& origin, CommandOutput& output) {
        try {
            // Pick up config.json edits (packet switches) without restarting the server.
            FreeCamera::reloadPacketOptions();
            auto* entity = origin.getEntity();
            if (entity && entity->isPlayer()) {
                auto* pl   = static_cast<Player*>(entity);
                auto  guid = pl->getNetworkIdentifier().mGuid.g;
                if (!FreeCamList.count(guid)) {
                    FreeCamera::EnableFreeCamera(pl);
                    return output.success("freecamera.command.enabled"_tr());
                } else {
                    FreeCamera::DisableFreeCamera(pl);
                    return output.success("freecamera.command.disabled"_tr());
                }
            }
            return output.error("freecamera.command.console"_tr());
        } catch (std::exception const& e) {
            // LeviLamina prints uncaught command errors through a path that can itself crash the
            // server, so never let one escape this handler.
            FreeCamera::Entry::getInstance().getSelf().getLogger().error(
                "[{}] /freecamera failed: {}",
                MOD_NAME,
                e.what()
            );
        } catch (...) {
            FreeCamera::Entry::getInstance().getSelf().getLogger().error("[{}] /freecamera failed: unknown error", MOD_NAME);
        }
        return output.error("freecamera.command.error"_tr());
    });
}

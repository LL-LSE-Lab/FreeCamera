# Build (26.51)

```powershell
xmake repo -u
xmake f -a x64 -m release -p windows -y
xmake -y
```

Output: `bin/FreeCamera/`

## Local toolchain requirement (important)

`levilamina 26.51.6` is built from source by xmake, and its own `xmake.lua` calls
`set_toolchains("clang-cl")`. So the machine running the build needs:

1. **LLVM `clang-cl.exe` reachable on `PATH`** — otherwise xmake aborts with
   `toolchain("clang-cl"): not found!`.
2. **The MSVC / Windows SDK environment** (`INCLUDE`, `LIB`). `clang-cl` does not get these
   automatically when Visual Studio lives outside the default location, and package builds then
   fail with `fatal error: 'cassert' file not found`. Run xmake from a *Developer Command Prompt
   for VS* (or `vcvars64.bat`) to provide them.

On this machine both are wrapped by `E:\dev\BDS\dev\temp\xmake-vs.bat`:

```powershell
cmd /c "E:\dev\BDS\dev\temp\xmake-vs.bat f -a x64 -m release -p windows -y"
cmd /c "E:\dev\BDS\dev\temp\xmake-vs.bat -y"
```

GitHub Actions runners have LLVM and VS preinstalled, so the workflow in
`.github/workflows/` needs no change.

## Toolchain pins (`xmake.lua`)

| Package | Version |
|---------|---------|
| levilamina | **26.51.6** (matches the target loader) |
| gmlib | **26.51.0** |
| levibuildscript | 0.6.1 |

`gmlib` and `levilamina` must stay in the same generation (`26.51.x`): the GMLIB `SDK-static`
is generated from one specific LeviLamina header revision, and linking it against a different
generation breaks with unresolved BDS symbols.

## Prelink chain (do not substitute by hand)

`levilamina 26.51.6` resolves its symbol database transitively:

```
levilamina 26.51.6 -> bedrockdata v26.51.1-server.7 -> prelink v0.8.6
```

`@levibuildscript/linkrule` looks up `bedrock_runtime_data` and `prelink.exe` on `PATH` at link
time. `prelink` validates every still-unresolved symbol against `bedrock_runtime_data` and only
emits the ones it finds, so the generated `bedrock_runtime_api.lib` is exactly the set of BDS
symbols the plugin imports. PreLoader resolves those names at runtime against the
`bedrock_runtime_data` sitting in the server directory — that file must be the same generation as
the one used for the build, otherwise the plugin fails with `__delayLoadHelper2` /
`0xC06D007E` (`ERROR_MOD_NOT_FOUND`) even though it links fine. Let xmake resolve both packages;
never download `prelink` manually.

### Inspecting what was actually used

```powershell
xmake require --info levilamina
xmake require --info gmlib
# link line actually used for the last build:
Get-Content build\.deps\FreeCamera\windows\x64\release\FreeCamera.dll.d
# delay-load dependency must be bedrock_runtime.dll:
dumpbin /dependents bin\FreeCamera\FreeCamera.dll
```

## Deploying to a local test server

```powershell
Copy-Item .\bin\FreeCamera\* "E:\dev\BDS\26.51.6\plugins\FreeCamera\" -Recurse -Force
```

`lang/` is embedded into the DLL through `src/resource.rc`; the plain `lang/*.lang` files are also
shipped next to the DLL for `LangI18n::loadAllLanguages()`.

## BDS 1.26.51 notes (why the code looks the way it does)

These were all found the hard way against a real client; do not "simplify" them away.

### Packets are `ll::PayloadPacket<Payload>` now

Fields moved onto a payload base and every packet gained a `mSerializationMode`. `PlayerListPacket`
and `RemoveActorPacket` declare no constructor, so that member is indeterminate until assigned.
The engine uses `CerealOnly` for both, and `AddPlayerPacket`'s own constructor already picks
`CerealOnly`; the network layer supplies no per-packet override, so the value the packet carries is
the one that gets used.

### Do not build `SetPlayerGameTypePacket` by hand

In the 26.51.6 headers its payload and `mSerializationMode` are laid out transposed relative to the
real BDS class: the engine's `getSerializationMode()` reads the offset the header calls
`mPlayerGameType`, so a hand built packet goes out with an out-of-range serialization mode. The mod
uses GMLIB's `GMPlayer::setClientGamemode()` instead, which is compiled against the real layout
(it sends `UpdatePlayerGameTypePacket`).

### The anchor entity must not reuse the player's identity

`AddPlayerPacketPayload` carries no skin, so the client builds the anchor's model from the skin of
the player list entry whose uuid matches the anchor - that entry has to exist and has to hold a
valid skin (`SerializedSkinRef::isValid()`, falling back to
`SerializedSkinRef::createTrustedDefaultSerializedSkin()`).

Beyond that, the anchor must own every part of its identity. If it keeps the real player's name,
`mAbilitiesData` target, platform id, device id or actor links, the client binds its local player /
camera to the anchor, which has no chunk publisher: the world stops rendering (sky only) and the
client crashes a moment later. Hence the `*FC` suffix name, the cleared link / device / platform
fields and the `SerializedAbilitiesData` rebuilt around the fake actor id.

**This applies to the player list entry too, not only to `AddPlayerPacket`.** `AddEntry` copies
every `PlayerListEntry` field, so the entry used to keep the real player's `mXUID` /
`mPlatformOnlineId`. The client matches a player list entry to the identity it already knows, so
the anchor then resolved to the *real* player's cached appearance instead of the entry's skin.
That cache is never refreshed for the owning client (the server does not send a player its own skin
update), which is why the anchor showed the skin from the moment the player logged in while
everyone else saw the current one. `clearAnchorIdentity` (default `true`) clears those fields.
`sendSelfSkinRefresh` additionally sends a `PlayerSkinPacket` for the real uuid to the player
itself, and `sendFakePlayerSkin` re-applies the skin to the anchor entity after it is created, so
the anchor does not depend on the client resolving anything at creation time.

### `PlayerSkinPacket` is safe to build by hand, but do not touch `mSerializationMode`

Unlike `PlayerListPacket` / `RemoveActorPacket`, `PlayerSkinPacket` declares a constructor, so the
engine initialises `mSerializationMode` (the packet is serverbound and clientbound, the engine sends
it itself). Assigning that member by hand is what breaks `SetPlayerGameTypePacket`, so leave it
alone and only write the payload fields (`mUUID`, `mSkin`, `mLocalizedNewSkinName`,
`mLocalizedOldSkinName`). Use empty localized names: a non empty name makes the client show a
"changed skin" toast. With `skinDiagnostics` enabled the log prints the engine's own
`getSerializationMode()` / `toString()` view of the packet, which is what to compare when a hand
built packet is suspected of having a transposed layout.

### Config version bump

`ll::config::loadConfig` only runs its merge-updater when the `version` stored in `config.json`
differs from `Config::version`. Adding a field without bumping the version makes every existing
config fail to deserialize (`missing required field ...`) and the mod fails to enable.

## Runtime switches

`plugins/FreeCamera/config/config.json` (re-read on every `/fc`, no restart needed):

| Key | Default | Meaning |
|-----|---------|---------|
| `packetSerializationMode` | `cerealOnly` | serialization mode for the packets the mod builds by hand (`cerealOnly` / `sideBySide` / `manualOnly`) |
| `sendGamemodeSwitch` | `true` | send the spectator game mode switch |
| `sendFakePlayerListAdd` | `true` | send the player list entry that carries the anchor's identity and skin |
| `sendFakePlayerListRemove` | `true` | remove that entry again when leaving free camera |
| `sendFakeAddPlayer` | `true` | send the anchor entity itself |
| `clearAnchorIdentity` | `true` | clear the real player's `mXUID` / `mPlatformOnlineId` (and the host flags) out of the fake entry — see above |
| `preferClientSkin` | `true` | build the anchor from the skin the client itself last uploaded instead of the server's cached copy |
| `sendFakePlayerSkin` | `true` | send a `PlayerSkinPacket` for the anchor uuid after the entity is created |
| `sendSelfSkinRefresh` | `true` | send a `PlayerSkinPacket` for the real uuid to the player itself (`/fc` entry and skin change) |
| `refreshAnchorOnSkinChange` | `true` | push a new skin onto an existing anchor when the player changes skin in free camera |
| `skinDiagnostics` | `false` | log the skin source, whether the candidates agree, and the engine's view of the `PlayerSkinPacket` we built |

With `skinDiagnostics` enabled, `/fc` logs one `anchor skin: source=...` line (with
`equalToList` / `equalToPlayer`, `identityCleared`, the skin ids and image size) and each skin
change logs `skin change for <name>: playerSkinUpdated=...`. `source=client` means the skin used
was the one the client uploaded; `equalToPlayer=false` means the server's own copy was stale.

# Rebuild dashboard

Data: decomp index + call edges + symbols.

| metric | value |
|---|---|
| functions | 8584 |
| decompiled ok | 8583
| code bytes | 2590052
| community-named | 4067

## Largest functions

| address | name | bytes |
|---|---|---|
| 0066D530 | RulesClass::Read_General | 18793 |
| 00712170 | TechnoType_ReadINI | 16337 |
| 00675210 | Rules_LoadStateFromStream | 13883 |
| 0064DEA0 | Network_WriteSyncDump | 11242 |
| 004B6C30 | Dropship_RunLoadoutScreen | 10849 |
| 006BB9A0 | Application_WinMain | 10256 |
| 006691E0 | RulesClass::Read_AudioVisual | 10016 |
| 006040B0 | UI_LookupDialogTooltipKey | 9871 |
| 005A9C40 | StaticCellPairTables_Initialize | 9537 |
| 006DD8B0 | TActionClass::Execute | 9530 |
| 00665650 | RulesClass::RulesClass | 9175 |
| 005E3D10 | Lobby_DecodeGameOptions | 8702 |
| 004B2630 | Locomotion_ProcessGroundMovement_004B2630 | 8503 |
| 006A1C80 | Locomotion_ProcessGroundMovement_006A1C80 | 8470 |
| 00545150 | Theater_LoadIsometricTileResources | 7233 |
| 006FDD50 | Techno_FireWeaponAtTarget | 7167 |
| 00532150 | Commands_RegisterBuiltinsAndDefaultHotkeys | 7118 |
| 005E00B0 | FUN_005e00b0 | 6995 |
| 00547CF0 | FUN_00547cf0 | 6967 |
| 006CC390 | SuperClass::Launch | 6835 |


## Busiest callers (fan-out)

| address | calls out | name |
|---|---|---|
| 0066D530 | 719 | RulesClass::Read_General |
| 00712170 | 686 | TechnoType_ReadINI |
| 006BB9A0 | 549 | Application_WinMain |
| 00675210 | 378 | Rules_LoadStateFromStream |
| 005E3D10 | 363 | Lobby_DecodeGameOptions |
| 0064DEA0 | 360 | Network_WriteSyncDump |
| 006691E0 | 305 | RulesClass::Read_AudioVisual |
| 006C6F50 | 270 | Game::SendStatisticsPacket |
| 005A9C40 | 241 | StaticCellPairTables_Initialize |
| 005DC350 | 238 | LAN_JoinGameHandshake |
| 006DD8B0 | 224 | TActionClass::Execute |
| 00672AE0 | 222 | RulesClass::Read_AI |
| 005E00B0 | 211 | FUN_005e00b0 |
| 00532150 | 193 | Commands_RegisterBuiltinsAndDefaultHotkeys |
| 0061F210 | 192 | FUN_0061f210 |

## Busiest callees (fan-in)

| address | called by | name |
|---|---|---|
| 007C8B3D | 3181 | YRMemory::Deallocate |
| 007C8E17 | 1995 | YRMemory::Allocate |
| 004068E0 | 1287 | DebugLog_NoOp |
| 007C5F00 | 1246 | Game::F2I64 |
| 005657A0 | 1124 | Map_GetCellAtCellCoords |
| 00734E60 | 782 | StringTable::LoadString |
| EXTERNAL:000000E7 | 781 | ? |
| 007B46F0 | 662 | Memory_DeleteAndNull |
| EXTERNAL:000000E8 | 649 | ? |
| 00528A10 | 507 | INIClass::ReadString |
| 005276D0 | 488 | INIClass::ReadInteger |
| 006C8C40 | 433 | Timer_Get16msTicks |
| 007C8D20 | 401 | CRT_Stricmp |
| 007C9CC2 | 368 | CRT_Strtok |
| 0065C7E0 | 314 | Randomizer::RandomRanged |

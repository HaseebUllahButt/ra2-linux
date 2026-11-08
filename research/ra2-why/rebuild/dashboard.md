# Rebuild dashboard

Data: decomp index + call edges + symbols.

| metric | value |
|---|---|
| functions | 8584 |
| decompiled ok | 8583
| code bytes | 2590052
| community-named | 1080

## Largest functions

| address | name | bytes |
|---|---|---|
| 0066D530 | RulesClass::Read_General | 18793 |
| 00712170 | TechnoType_ReadINI | 16337 |
| 00675210 | FUN_00675210 | 13883 |
| 0064DEA0 | Network_WriteSyncDump | 11242 |
| 004B6C30 | FUN_004b6c30 | 10849 |
| 006BB9A0 | Application_WinMain | 10256 |
| 006691E0 | RulesClass::Read_AudioVisual | 10016 |
| 006040B0 | FUN_006040b0 | 9871 |
| 005A9C40 | FUN_005a9c40 | 9537 |
| 006DD8B0 | TActionClass::Execute | 9530 |
| 00665650 | RulesClass::RulesClass | 9175 |
| 005E3D10 | FUN_005e3d10 | 8702 |
| 004B2630 | FUN_004b2630 | 8503 |
| 006A1C80 | FUN_006a1c80 | 8470 |
| 00545150 | FUN_00545150 | 7233 |
| 006FDD50 | FUN_006fdd50 | 7167 |
| 00532150 | FUN_00532150 | 7118 |
| 005E00B0 | FUN_005e00b0 | 6995 |
| 00547CF0 | FUN_00547cf0 | 6967 |
| 006CC390 | SuperClass::Launch | 6835 |


## Busiest callers (fan-out)

| address | calls out | name |
|---|---|---|
| 0066D530 | 719 | RulesClass::Read_General |
| 00712170 | 686 | TechnoType_ReadINI |
| 006BB9A0 | 549 | Application_WinMain |
| 00675210 | 378 | FUN_00675210 |
| 005E3D10 | 363 | FUN_005e3d10 |
| 0064DEA0 | 360 | Network_WriteSyncDump |
| 006691E0 | 305 | RulesClass::Read_AudioVisual |
| 006C6F50 | 270 | FUN_006c6f50 |
| 005A9C40 | 241 | FUN_005a9c40 |
| 005DC350 | 238 | FUN_005dc350 |
| 006DD8B0 | 224 | TActionClass::Execute |
| 00672AE0 | 222 | RulesClass::Read_AI |
| 005E00B0 | 211 | FUN_005e00b0 |
| 00532150 | 193 | FUN_00532150 |
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

#include "rules_reader.h"

#include <stdexcept>
#include <set>
#include <utility>

namespace ra2::rules {

const std::vector<GeneralField>& RulesReader::general_schema() {
    // Each row is a direct read in 0x66D530. Offsets and constructor defaults
    // were cross-checked against 0x665650; the README explains omitted cases.
    static const std::vector<GeneralField> fields = {
        {"DominatorFireAtPercentage", RuleKind::Integer, 0x304, std::int32_t{50}, 0x304},
        {"DominatorCaptureRange", RuleKind::Integer, 0x308, std::int32_t{2}, 0x308},
        {"DominatorDamage", RuleKind::Integer, 0x30C, std::int32_t{50}, 0x30C},
        {"TiberiumHeal", RuleKind::Double, 0x28, 0.016666666666666666, 0x28},
        {"SelfHealInfantryFrames", RuleKind::Integer, 0x30, std::int32_t{1000}, 0x30},
        {"SelfHealInfantryAmount", RuleKind::Integer, 0x34, std::int32_t{1}, 0x34},
        {"SelfHealUnitFrames", RuleKind::Integer, 0x38, std::int32_t{1000}, 0x38},
        {"SelfHealUnitAmount", RuleKind::Integer, 0x3C, std::int32_t{1}, 0x3C},
        {"ZoomInFactor", RuleKind::Double, 0x550, 2.0, 0x550},
        {"RevealByHeight", RuleKind::Boolean, 0x17EE, true, 0x17EE},
        {"AllowShroudedSubteranneanMoves", RuleKind::Boolean, 0x17EF, false, 0x17EF},
        {"AircraftFogReveal", RuleKind::Integer, 0xF4, std::int32_t{6}, 0xF4},
        {"MinLowPowerProductionSpeed", RuleKind::Float, 0x570, 0.5, 0x570},
        {"MaxLowPowerProductionSpeed", RuleKind::Float, 0x574, 0.8999999761581421, 0x574},
        {"LowPowerPenaltyModifier", RuleKind::Float, 0x578, 1.0, 0x578},
        {"MultipleFactory", RuleKind::Float, 0x57C, 1.0, 0x57C},
        {"MaximumCheerRate", RuleKind::Integer, 0x580, std::int32_t{1}, 0x580},
        {"TreeFlammability", RuleKind::Double, 0x588, 0.1, 0x588},
        {"MissileROTVar", RuleKind::Double, 0x598, 0.25, 0x598},
        {"MissileSafetyAltitude", RuleKind::Integer, 0x5A0, std::int32_t{500}, 0x5A0},
        {"MissileSpeedVar", RuleKind::Double, 0x590, 0.25, 0x590},
        {"DropPodHeight", RuleKind::Integer, 0x5A8, std::int32_t{1500}, 0x5A8},
        {"DropPodSpeed", RuleKind::Integer, 0x5AC, std::int32_t{40}, 0x5AC},
        {"DropPodAngle", RuleKind::Double, 0x5B0, 1.5707963267948966, 0x5B0},
        {"CrewEscape", RuleKind::Double, 0x5C0, 0.5, 0x5C0},
        {"TunnelSpeed", RuleKind::Double, 0x20, 1.0, 0x20},
        {"HoverDampen", RuleKind::Double, 0x5F0, 0.8, 0x5F0},
        {"HoverBob", RuleKind::Double, 0x5D0, 30.0, 0x5D0},
        {"HoverHeight", RuleKind::Integer, 0x5CC, std::int32_t{120}, 0x5CC},
        {"HoverBoost", RuleKind::Double, 0x5D8, 1.3, 0x5D8},
        {"HoverAcceleration", RuleKind::Double, 0x5E0, 0.03, 0x5E0},
        {"HoverBrake", RuleKind::Double, 0x5E8, 0.03, 0x5E8},
        {"VeteranRatio", RuleKind::Double, 0x668, 10.0, 0x668},
        {"VeteranCombat", RuleKind::Double, 0x670, 1.0, 0x670},
        {"VeteranSpeed", RuleKind::Double, 0x678, 1.0, 0x678},
        {"VeteranSight", RuleKind::Double, 0x680, 1.0, 0x680},
        {"VeteranArmor", RuleKind::Double, 0x688, 1.0, 0x688},
        {"VeteranROF", RuleKind::Double, 0x690, 1.0, 0x690},
        {"VeteranCap", RuleKind::Double, 0x698, 1.0, 0x698},
        {"BridgeVoxelMax", RuleKind::Integer, 0x624, std::int32_t{3}, 0x624},
        {"CloakingStages", RuleKind::Integer, 0x628, std::int32_t{9}, 0x628},
        {"ShipSinkingWeight", RuleKind::Double, 0x630, 3.0, 0x630},
        {"IceCrackingWeight", RuleKind::Double, 0x638, 2.0, 0x638},
        {"IceBreakingWeight", RuleKind::Double, 0x640, 4.0, 0x640},
        {"CliffBackImpassability", RuleKind::Byte, 0x664, std::int32_t{0}, 0x664},
        {"PlacementDelay", RuleKind::Double, 0x5F8, 0.05, 0x5F8},
        {"TrackedUphill", RuleKind::Double, 0x768, 1.0, 0x768},
        {"TrackedDownhill", RuleKind::Double, 0x770, 1.0, 0x770},
        {"WheeledUphill", RuleKind::Double, 0x778, 1.0, 0x778},
        {"WheeledDownhill", RuleKind::Double, 0x780, 1.0, 0x780},
        {"WindDirection", RuleKind::Integer, 0x7AC, std::int32_t{-1}, 0x7AC},
        {"FlightLevel", RuleKind::Integer, 0x7B4, std::int32_t{500}, 0x7B4},
        {"ParachuteMaxFallRate", RuleKind::Integer, 0x7B8, std::int32_t{-3}, 0x7B8},
        {"NoParachuteMaxFallRate", RuleKind::Integer, 0x7BC, std::int32_t{-100}, 0x7BC},
        {"ChronoDelay", RuleKind::Integer, 0xBEC, std::int32_t{60}, 0xBEC},
        {"ChronoReinfDelay", RuleKind::Integer, 0xBF0, std::int32_t{60}, 0xBF0},
        {"ChronoDistanceFactor", RuleKind::Integer, 0xBF4, std::int32_t{32}, 0xBF4},
        {"ChronoTrigger", RuleKind::Boolean, 0xBF8, true, 0xBF8},
        {"ChronoMinimumDelay", RuleKind::Integer, 0xBFC, std::int32_t{0}, 0xBFC},
        {"ChronoRangeMinimum", RuleKind::Integer, 0xC00, std::int32_t{0}, 0xC00},
        {"SpyPowerBlackout", RuleKind::Integer, 0xD64, std::int32_t{0}, 0xD64},
        {"SpyMoneyStealPercent", RuleKind::Float, 0xD68, 0.5, 0xD68},
        {"AttackCursorOnDisguise", RuleKind::Boolean, 0xD6C, false, 0xD6C},
        {"PurifierBonus", RuleKind::Float, 0xF3C, 0.25, 0xF3C},
        {"CurleyShuffle", RuleKind::Boolean, 0x17E1, false, 0x17E1},
        {"FineDiffControl", RuleKind::Boolean, 0x17E4, false, 0x17E4},
        {"TeamDelays", RuleKind::IntegerList, 0x1158, std::vector<std::int32_t>{}, 0x1158},
        {"AIHateDelays", RuleKind::IntegerList, 0x1174, std::vector<std::int32_t>{}, 0x1174},
        {"AIAlternateProductionCreditCutoff", RuleKind::Integer, 0x1300, std::int32_t{1000}, 0x1300},
        {"AIUseTurbineUpgradeProbability", RuleKind::Double, 0x13E8, 1.0, 0x13E8},
        {"NodAIBuildsWalls", RuleKind::Boolean, 0x17F1, true, 0x17F1},
        {"AIBuildsWalls", RuleKind::Boolean, 0x17F2, true, 0x17F2},
        {"FillEarliestTeamProbability", RuleKind::IntegerList, 0x13F0, std::monostate{}, 0x13F0},
        {"MinimumAIDefensiveTeams", RuleKind::IntegerList, 0x1390, std::vector<std::int32_t>{}, 0x1390},
        {"MaximumAIDefensiveTeams", RuleKind::IntegerList, 0x13AC, std::vector<std::int32_t>{}, 0x13AC},
        {"TotalAITeamCap", RuleKind::IntegerList, 0x13C8, std::monostate{}, 0x13C8},
        {"UseMinDefenseRule", RuleKind::Boolean, 0x17F3, true, 0x17F3},
        {"DissolveUnfilledTeamDelay", RuleKind::Integer, 0x1190, std::int32_t{5000}, 0x1190},
        {"AISafeDistance", RuleKind::Integer, 0xD74, std::int32_t{8}, 0xD74},
        {"AIMinorSuperReadyPercent", RuleKind::Float, 0xD70, 0.800000011920929, 0xD70},
        {"HarvesterTooFarDistance", RuleKind::Integer, 0xD78, std::int32_t{5}, 0xD78},
        {"ChronoHarvTooFarDistance", RuleKind::Integer, 0xD7C, std::int32_t{50}, 0xD7C},
        {"AlliedBaseDefenseCounts", RuleKind::IntegerList, 0xD80, std::vector<std::int32_t>{}, 0xD80},
        {"SovietBaseDefenseCounts", RuleKind::IntegerList, 0xD9C, std::vector<std::int32_t>{}, 0xD9C},
        {"ThirdBaseDefenseCounts", RuleKind::IntegerList, 0xDB8, std::vector<std::int32_t>{}, 0xDB8},
        {"AIPickWallDefensePercent", RuleKind::IntegerList, 0xDD4, std::vector<std::int32_t>{}, 0xDD4},
        {"AIRestrictReplaceTime", RuleKind::Integer, 0xDF0, std::int32_t{500}, 0xDF0},
        {"ThreatPerOccupant", RuleKind::Integer, 0xDF4, std::int32_t{5}, 0xDF4},
        {"ApproachTargetResetMultiplier", RuleKind::Integer, 0xDF8, std::int32_t{1}, 0xDF8},
        {"CampaignMoneyDeltaEasy", RuleKind::Integer, 0xDFC, std::int32_t{5000}, 0xDFC},
        {"CampaignMoneyDeltaHard", RuleKind::Integer, 0xE00, std::int32_t{-5000}, 0xE00},
        {"GuardAreaTargetingDelay", RuleKind::Integer, 0xE04, std::int32_t{36}, 0xE04},
        {"NormalTargetingDelay", RuleKind::Integer, 0xE08, std::int32_t{27}, 0xE08},
        {"AINavalYardAdjacency", RuleKind::Integer, 0xE0C, std::int32_t{20}, 0xE0C},
        {"DisabledDisguiseDetectionPercent", RuleKind::IntegerList, 0xE10, std::vector<std::int32_t>{}, 0xE10},
        {"AIAutoDeployFrameDelay", RuleKind::IntegerList, 0xE2C, std::vector<std::int32_t>{}, 0xE2C},
        {"MaximumBuildingPlacementFailures", RuleKind::Integer, 0xE48, std::int32_t{5}, 0xE48},
        {"SlaveMinerKickFrameDelay", RuleKind::Integer, 0x1790, std::int32_t{2147483647}, 0x1790},
        {"AISuperDefenseProbability", RuleKind::IntegerList, 0xEC4, std::vector<std::int32_t>{}, 0xEC4},
        {"AISuperDefenseFrames", RuleKind::Integer, 0xEE0, std::int32_t{25}, 0xEE0},
        {"AICaptureNormal", RuleKind::IntegerList, 0xE4C, std::vector<std::int32_t>{}, 0xE4C},
        {"AICaptureWounded", RuleKind::IntegerList, 0xE68, std::vector<std::int32_t>{}, 0xE68},
        {"AICaptureLowPower", RuleKind::IntegerList, 0xE84, std::vector<std::int32_t>{}, 0xE84},
        {"AICaptureLowMoney", RuleKind::IntegerList, 0xEA0, std::vector<std::int32_t>{}, 0xEA0},
        {"AICaptureLowMoneyMark", RuleKind::Integer, 0xEBC, std::monostate{}, 0xEBC},
        {"AICaptureWoundedMark", RuleKind::Float, 0xEC0, std::monostate{}, 0xEC0},
        {"MultiplayerAICM", RuleKind::IntegerList, 0x1304, std::vector<std::int32_t>{}, 0x1304},
        {"AIVirtualPurifiers", RuleKind::IntegerList, 0x1320, std::vector<std::int32_t>{}, 0x1320},
        {"AISlaveMinerNumber", RuleKind::IntegerList, 0x133C, std::vector<std::int32_t>{}, 0x133C},
        {"HarvestersPerRefinery", RuleKind::IntegerList, 0x1358, std::vector<std::int32_t>{}, 0x1358},
        {"AIExtraRefineries", RuleKind::IntegerList, 0x1374, std::vector<std::int32_t>{}, 0x1374},
        {"AmerParaDropNum", RuleKind::IntegerList, 0xC20, std::vector<std::int32_t>{}, 0xC20},
        {"AllyParaDropNum", RuleKind::IntegerList, 0xC58, std::vector<std::int32_t>{}, 0xC58},
        {"SovParaDropNum", RuleKind::IntegerList, 0xC90, std::vector<std::int32_t>{}, 0xC90},
        {"YuriParaDropNum", RuleKind::IntegerList, 0xCC8, std::vector<std::int32_t>{}, 0xCC8},
        {"AIIonCannonConYardValue", RuleKind::IntegerList, 0x1194, std::vector<std::int32_t>{}, 0x1194},
        {"AIIonCannonWarFactoryValue", RuleKind::IntegerList, 0x11B0, std::vector<std::int32_t>{}, 0x11B0},
        {"AIIonCannonPowerValue", RuleKind::IntegerList, 0x11CC, std::vector<std::int32_t>{}, 0x11CC},
        {"AIIonCannonTechCenterValue", RuleKind::IntegerList, 0x11E8, std::vector<std::int32_t>{}, 0x11E8},
        {"AIIonCannonEngineerValue", RuleKind::IntegerList, 0x1204, std::vector<std::int32_t>{}, 0x1204},
        {"AIIonCannonThiefValue", RuleKind::IntegerList, 0x1220, std::vector<std::int32_t>{}, 0x1220},
        {"AIIonCannonHarvesterValue", RuleKind::IntegerList, 0x123C, std::vector<std::int32_t>{}, 0x123C},
        {"AIIonCannonMCVValue", RuleKind::IntegerList, 0x1258, std::vector<std::int32_t>{}, 0x1258},
        {"AIIonCannonAPCValue", RuleKind::IntegerList, 0x1274, std::vector<std::int32_t>{}, 0x1274},
        {"AIIonCannonBaseDefenseValue", RuleKind::IntegerList, 0x1290, std::vector<std::int32_t>{}, 0x1290},
        {"AIIonCannonPlugValue", RuleKind::IntegerList, 0x12AC, std::vector<std::int32_t>{}, 0x12AC},
        {"AIIonCannonHelipadValue", RuleKind::IntegerList, 0x12C8, std::vector<std::int32_t>{}, 0x12C8},
        {"AIIonCannonTempleValue", RuleKind::IntegerList, 0x12E4, std::vector<std::int32_t>{}, 0x12E4},
        {"CloakDelay", RuleKind::Double, 0x1410, 0.0, 0x1410},
        {"GameSpeedBias", RuleKind::Double, 0x1418, 1.0, 0x1418},
        {"BaseBias", RuleKind::Double, 0x1420, 1.0, 0x1420},
        {"SeparateAircraft", RuleKind::Boolean, 0x17E8, false, 0x17E8},
        {"BaseDefenseDelay", RuleKind::Double, 0x14D8, 0.25, 0x14D8},
        {"SuspendPriority", RuleKind::Integer, 0x14E0, std::int32_t{20}, 0x14E0},
        {"SuspendDelay", RuleKind::Double, 0x14E8, 2.0, 0x14E8},
        {"SurvivorRate", RuleKind::Double, 0x14F0, 0.5, 0x14F0},
        {"AlliedSurvivorDivisor", RuleKind::Integer, 0x14F8, std::int32_t{200}, 0x14F8},
        {"SovietSurvivorDivisor", RuleKind::Integer, 0x14FC, std::int32_t{100}, 0x14FC},
        {"ThirdSurvivorDivisor", RuleKind::Integer, 0x1500, std::int32_t{300}, 0x1500},
        {"ReloadRate", RuleKind::Double, 0x1508, 0.05, 0x1508},
        {"BuildupTime", RuleKind::Double, 0x1518, 0.05, 0x1518},
        {"HarvesterDumpRate", RuleKind::Double, 0x1528, 0.016, 0x1528},
        {"HarvesterLoadRate", RuleKind::Integer, 0x1520, std::int32_t{2}, 0x1520},
        {"BuildSpeed", RuleKind::Double, 0x1748, 1.0, 0x1748},
        {"DamageDelay", RuleKind::Double, 0x16B0, 1.0, 0x16B0},
        {"GrowthRate", RuleKind::Double, 0x1638, 2.0, 0x1638},
        {"RefundPercent", RuleKind::Double, 0x1738, 0.5, 0x1738},
        {"RepairPercent", RuleKind::Double, 0x16D0, 0.25, 0x16D0},
        {"RepairStep", RuleKind::Integer, 0x16CC, std::int32_t{5}, 0x16CC},
        {"IRepairStep", RuleKind::Integer, 0x16D8, std::int32_t{1}, 0x16D8},
        {"RepairRate", RuleKind::Double, 0x16E0, 0.016, 0x16E0},
        {"URepairRate", RuleKind::Double, 0x16E8, 0.016, 0x16E8},
        {"IRepairRate", RuleKind::Double, 0x16F0, 0.016, 0x16F0},
        {"BlendedFog", RuleKind::Boolean, 0x17E2, true, 0x17E2},
        {"AttackingAircraftSightRange", RuleKind::Integer, 0x18, std::int32_t{5}, 0x18},
        {"LeptonsPerSightIncrease", RuleKind::Integer, 0x16BC, std::int32_t{50}, 0x16BC},
        {"TiberiumTransmogrify", RuleKind::Integer, 0x1688, std::monostate{}, 0x1688},
        {"LightningDeferment", RuleKind::Integer, 0x1794, std::int32_t{250}, 0x1794},
        {"LightningDamage", RuleKind::Integer, 0x1798, std::int32_t{200}, 0x1798},
        {"LightningStormDuration", RuleKind::Integer, 0x179C, std::int32_t{900}, 0x179C},
        {"LightningHitDelay", RuleKind::Integer, 0x17A0, std::int32_t{90}, 0x17A0},
        {"LightningScatterDelay", RuleKind::Integer, 0x17A4, std::int32_t{10}, 0x17A4},
        {"LightningCellSpread", RuleKind::Integer, 0x17A8, std::int32_t{10}, 0x17A8},
        {"LightningSeparation", RuleKind::Integer, 0x17AC, std::int32_t{3}, 0x17AC},
        {"LightningPrintText", RuleKind::Boolean, 0x17B0, true, 0x17B0},
        {"ForceShieldRadius", RuleKind::Integer, 0x17B8, std::int32_t{10}, 0x17B8},
        {"ForceShieldDuration", RuleKind::Integer, 0x17BC, std::int32_t{400}, 0x17BC},
        {"ForceShieldBlackoutDuration", RuleKind::Integer, 0x17C0, std::int32_t{800}, 0x17C0},
        {"ForceShieldPlayFadeSoundTime", RuleKind::Integer, 0x17C4, std::int32_t{50}, 0x17C4},
        {"MutateExplosion", RuleKind::Boolean, 0x17C8, false, 0x17C8},
        {"PrismSupportMax", RuleKind::Integer, 0x4A0, std::int32_t{8}, 0x4A0},
        {"PrismSupportDelay", RuleKind::Integer, 0x4A4, std::int32_t{100}, 0x4A4},
        {"PrismSupportDuration", RuleKind::Integer, 0x4A8, std::int32_t{15}, 0x4A8},
        {"PrismSupportHeight", RuleKind::Integer, 0x4AC, std::int32_t{300}, 0x4AC},
        {"V3RocketPauseFrames", RuleKind::Integer, 0x4B0, std::int32_t{0}, 0x4B0},
        {"V3RocketTiltFrames", RuleKind::Integer, 0x4B4, std::int32_t{60}, 0x4B4},
        {"V3RocketPitchInitial", RuleKind::Float, 0x4B8, 0.0, 0x4B8},
        {"V3RocketPitchFinal", RuleKind::Float, 0x4BC, 1.0, 0x4BC},
        {"V3RocketTurnRate", RuleKind::Float, 0x4C0, 0.05000000074505806, 0x4C0},
        {"V3RocketAcceleration", RuleKind::Float, 0x4C8, 0.4000000059604645, 0x4C8},
        {"V3RocketAltitude", RuleKind::Integer, 0x4CC, std::int32_t{768}, 0x4CC},
        {"V3RocketDamage", RuleKind::Integer, 0x4D0, std::int32_t{1000}, 0x4D0},
        {"V3RocketEliteDamage", RuleKind::Integer, 0x4D4, std::int32_t{1000}, 0x4D4},
        {"V3RocketBodyLength", RuleKind::Integer, 0x4D8, std::int32_t{256}, 0x4D8},
        {"V3RocketLazyCurve", RuleKind::Boolean, 0x4DC, true, 0x4DC},
        {"DMislPauseFrames", RuleKind::Integer, 0x4E4, std::int32_t{10}, 0x4E4},
        {"DMislTiltFrames", RuleKind::Integer, 0x4E8, std::int32_t{60}, 0x4E8},
        {"DMislPitchInitial", RuleKind::Float, 0x4EC, 0.0, 0x4EC},
        {"DMislPitchFinal", RuleKind::Float, 0x4F0, 1.0, 0x4F0},
        {"DMislTurnRate", RuleKind::Float, 0x4F4, 0.05000000074505806, 0x4F4},
        {"DMislAcceleration", RuleKind::Float, 0x4FC, 0.4000000059604645, 0x4FC},
        {"DMislAltitude", RuleKind::Integer, 0x500, std::int32_t{768}, 0x500},
        {"DMislDamage", RuleKind::Integer, 0x504, std::int32_t{1000}, 0x504},
        {"DMislEliteDamage", RuleKind::Integer, 0x508, std::int32_t{1000}, 0x508},
        {"DMislBodyLength", RuleKind::Integer, 0x50C, std::int32_t{128}, 0x50C},
        {"DMislLazyCurve", RuleKind::Boolean, 0x510, false, 0x510},
        {"CMislPauseFrames", RuleKind::Integer, 0x518, std::int32_t{10}, 0x518},
        {"CMislTiltFrames", RuleKind::Integer, 0x51C, std::int32_t{30}, 0x51C},
        {"CMislPitchInitial", RuleKind::Float, 0x520, 1.0, 0x520},
        {"CMislPitchFinal", RuleKind::Float, 0x524, 1.0, 0x524},
        {"CMislTurnRate", RuleKind::Float, 0x528, 1.0, 0x528},
        {"CMislAcceleration", RuleKind::Float, 0x530, 0.6000000238418579, 0x4FC},
        {"CMislAltitude", RuleKind::Integer, 0x534, std::int32_t{768}, 0x534},
        {"CMislDamage", RuleKind::Integer, 0x538, std::int32_t{500}, 0x538},
        {"CMislEliteDamage", RuleKind::Integer, 0x53C, std::int32_t{500}, 0x53C},
        {"CMislBodyLength", RuleKind::Integer, 0x540, std::int32_t{128}, 0x540},
        {"CMislLazyCurve", RuleKind::Boolean, 0x544, false, 0x544},
        {"ParadropRadius", RuleKind::Integer, 0x54C, std::int32_t{1024}, 0x54C},
        {"SpotlightMovementRadius", RuleKind::Integer, 0x788, std::int32_t{2000}, 0x788},
        {"SpotlightLocationRadius", RuleKind::Integer, 0x78C, std::int32_t{1000}, 0x78C},
        {"SpotlightSpeed", RuleKind::Double, 0x790, 0.05, 0x790},
        {"SpotlightAcceleration", RuleKind::Double, 0x798, 0.005, 0x798},
        {"SpotlightAngle", RuleKind::Double, 0x7A0, 20.0, 0x7A0},
        {"SpotlightRadius", RuleKind::Integer, 0x7A8, std::int32_t{175}, 0x7A8},
        {"RevealTriggerRadius", RuleKind::Integer, 0x62C, std::int32_t{5}, 0x62C},
        {"ChargeToDrainRatio", RuleKind::Double, 0x760, 3.0, 0x760},
        {"WallBuildSpeedCoefficient", RuleKind::Double, 0x758, 0.5, 0x758},
        {"ConditionYellowSparkingProbability", RuleKind::Double, 0x560, 0.01, 0x560},
        {"ConditionRedSparkingProbability", RuleKind::Double, 0x558, 0.02, 0x558},
        {"AITriggerSuccessWeightDelta", RuleKind::Double, 0xC0, 1.0, 0xC0},
        {"AITriggerFailureWeightDelta", RuleKind::Double, 0xC8, -1.0, 0xC8},
        {"AITriggerTrackRecordCoefficient", RuleKind::Double, 0xD0, 1.0, 0xD0},
        {"WeedCapacity", RuleKind::Integer, 0x17D0, std::int32_t{0}, 0x17D0},
        {"FlashFrameTime", RuleKind::Integer, 0x88, std::int32_t{7}, 0x88},
        {"RadarCombatFlashTime", RuleKind::Integer, 0x8C, std::int32_t{21}, 0x8C},
        {"RadarEventSpeed", RuleKind::Float, 0x80, 1.0, 0x80},
        {"RadarEventRotationSpeed", RuleKind::Float, 0x84, 0.10000000149011612, 0x84},
        {"RadarEventSuppressionDistances", RuleKind::IntegerList, 0x43C, std::vector<std::int32_t>{}, 0x43C},
        {"RadarEventVisibilityDurations", RuleKind::IntegerList, 0x458, std::vector<std::int32_t>{}, 0x458},
        {"RadarEventDurations", RuleKind::IntegerList, 0x474, std::vector<std::int32_t>{}, 0x474},
        {"RadarEventMinRadius", RuleKind::Integer, 0x7C, std::int32_t{5}, 0x7C},
        {"RadarEventColorSpeed", RuleKind::Float, 0x78, 0.05000000074505806, 0x78},
        {"MyEffectivenessCoefficientDefault", RuleKind::Double, 0x1040, 0.0, 0x1040},
        {"TargetEffectivenessCoefficientDefault", RuleKind::Double, 0x1048, 0.0, 0x1048},
        {"TargetSpecialThreatCoefficientDefault", RuleKind::Double, 0x1050, 0.0, 0x1050},
        {"TargetStrengthCoefficientDefault", RuleKind::Double, 0x1058, 0.0, 0x1058},
        {"TargetDistanceCoefficientDefault", RuleKind::Double, 0x1060, 0.0, 0x1060},
        {"DumbMyEffectivenessCoefficient", RuleKind::Double, 0x1068, 0.0, 0x1068},
        {"DumbTargetEffectivenessCoefficient", RuleKind::Double, 0x1070, 0.0, 0x1070},
        {"DumbTargetSpecialThreatCoefficient", RuleKind::Double, 0x1078, 0.0, 0x1078},
        {"DumbTargetStrengthCoefficient", RuleKind::Double, 0x1080, 0.0, 0x1080},
        {"DumbTargetDistanceCoefficient", RuleKind::Double, 0x1088, 0.0, 0x1088},
        {"EnemyHouseThreatBonus", RuleKind::Double, 0x1090, 0.0, 0x1090},
        {"VeinholeMonsterStrength", RuleKind::Integer, 0xD8, std::int32_t{10000}, 0xD8},
        {"MaxVeinholeGrowth", RuleKind::Integer, 0xDC, std::int32_t{1000}, 0xDC},
        {"VeinholeGrowthRate", RuleKind::Integer, 0xE0, std::int32_t{100}, 0xE0},
        {"VeinholeShrinkRate", RuleKind::Integer, 0xE4, std::int32_t{100}, 0xE4},
        {"VeinDamage", RuleKind::Integer, 0xEC, std::int32_t{2}, 0xEC},
        {"InfantryBlinkDisguiseTime", RuleKind::Integer, 0x1014, std::int32_t{0}, 0x1014},
        {"MaximumQueuedObjects", RuleKind::Integer, 0xF0, std::int32_t{5}, 0xF0},
        {"MaxWaypointPathLength", RuleKind::Integer, 0x90, std::int32_t{15}, 0x90},
        {"TreeStrength", RuleKind::Integer, 0x1144, std::int32_t{25}, 0x1144},
        {"EngineerCaptureLevel", RuleKind::Float, 0x17F8, 1.0, 0x17F8},
    };
    return fields;
}

RulesReader::RulesReader() {
    for (const auto& field : general_schema())
        general_.emplace(field.key, field.initial_value);
}

RulesReader& RulesReader::instance() {
    static RulesReader reader;
    return reader;
}

GeneralReadReport RulesReader::init(const IniFile& ini) {
    *this = RulesReader{};
    return read_file(ini);
}

GeneralReadReport RulesReader::read_file(const IniFile& ini) {
    return read_general(ini);
}

GeneralReadReport RulesReader::read_general(const IniFile& ini) {
    GeneralReadReport report;
    const auto* section = ini.find_section("General");
    report.section_present = section != nullptr;
    if (!section) return report; // 0x66D530 returns before changing fields.

    std::set<std::string_view> handled;
    for (const auto& field : general_schema()) {
        handled.emplace(field.key);
        auto& destination = general_.at(field.key);
        RuleValue fallback = destination;
        // Preserve the source's CMislAcceleration fallback to DMislAcceleration
        // (which has already been read), including when the key is absent.
        if (field.fallback_offset != field.original_offset) {
            for (const auto& source : general_schema()) {
                if (source.original_offset == field.fallback_offset) {
                    fallback = general_.at(source.key);
                    break;
                }
            }
        }
        const auto* entry = ini.find("General", field.key);
        if (!entry) {
            destination = std::move(fallback);
            ++report.retained;
            continue;
        }
        RuleValue parsed;
        switch (field.kind) {
        case RuleKind::Integer:
        case RuleKind::Byte:
            if (const auto n = IniFile::integer_value(entry->value)) {
                parsed = field.kind == RuleKind::Byte
                    ? static_cast<std::int32_t>(static_cast<std::uint8_t>(*n)) : *n;
            }
            break;
        case RuleKind::Boolean:
            if (const auto b = IniFile::bool_value(entry->value)) parsed = *b;
            break;
        case RuleKind::Float:
        case RuleKind::Double:
            if (const auto n = IniFile::double_value(entry->value)) {
                parsed = field.kind == RuleKind::Float
                    ? static_cast<double>(static_cast<float>(*n)) : *n;
            }
            break;
        case RuleKind::IntegerList: {
            // 0x475D70 reads into a 512-byte buffer, then uses strtok + atoi.
            // Lists use decimal atoi semantics, rather than ReadInteger's hex.
            std::vector<std::int32_t> numbers;
            bool valid = true;
            for (const auto& token : IniFile::split_list(
                     std::string_view(entry->value).substr(0, 511))) {
                try {
                    const auto n = std::stoll(token, nullptr, 10);
                    if (n < INT32_MIN || n > INT32_MAX) { valid = false; break; }
                    numbers.push_back(static_cast<std::int32_t>(n));
                } catch (const std::invalid_argument&) {
                    numbers.push_back(0);
                } catch (const std::out_of_range&) {
                    valid = false;
                    break;
                }
            }
            if (valid) parsed = std::move(numbers);
            break;
        }
        }
        if (std::holds_alternative<std::monostate>(parsed)) {
            destination = std::move(fallback);
            report.invalid_keys.emplace_back(field.key);
            ++report.retained;
        } else {
            destination = std::move(parsed);
            ++report.loaded;
        }
    }
    for (const auto& entry : section->entries) {
        if (handled.count(entry.key) == 0) report.unhandled_keys.push_back(entry.key);
    }
    return report;
}

} // namespace ra2::rules

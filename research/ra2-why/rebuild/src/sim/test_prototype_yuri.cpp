#include <cmath>
#include "prototype_yuri.h"
#include "../fs/mix_bootstrap.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using ra2::rules::IniFile;
namespace yu = ra2::sim;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

IniFile parse_text(const std::string& text) {
    IniFile file;
    file.parse(text);
    return file;
}

void expect_throws(const IniFile& rules, const IniFile& art, const char* what) {
    bool thrown = false;
    try { (void)yu::PrototypeYuriRules::load(rules, art); }
    catch (const std::runtime_error&) { thrown = true; }
    require(thrown, what);
}

void expect_load_throws(const std::string& rules_text, const char* what) {
    const IniFile art;
    expect_throws(parse_text(rules_text), art, what);
}

// One gattling frame per call, firing for the given frame count.
void fire_for(yu::GattlingState& state, int frames) {
    for (int i = 0; i < frames; ++i) state.tick(true, false);
}
void cool_for(yu::GattlingState& state, int frames) {
    for (int i = 0; i < frames; ++i) state.tick(false, false);
}
} // namespace

int main(int argc, char** argv) try {
    if (argc < 2) throw std::runtime_error("Usage: prototype_yuri_test <rulesmd.ini> [artmd.ini]");

    // --- Real game data through the MIX loader. ---------------------------
    // ra2md.mix is not on every machine: read rulesmd.ini from a path.
    // argv[1] is the rulesmd.ini path; artmd.ini is optional (argv[2]).
    IniFile rules_md, art_md;
    rules_md.load_file(argv[1]);
    if (argc > 2) art_md.load_file(argv[2]);
    const auto yuri = yu::PrototypeYuriRules::load(rules_md, art_md);

    // The Yuri side from [Sides] and [Countries].
    require(yuri.side_name == "ThirdSide", "ThirdSide is not the Yuri side");
    require(yuri.country == "YuriCountry", "ThirdSide country changed");
    require(yuri.side_index == 2, "ThirdSide is not the third combat side");
    require(yuri.country_index == 9, "YuriCountry is not country 9");
    require(yuri.country_suffix == "Soviet", "YuriCountry suffix changed");

    // Yuri infantry: mind controllers and the brute squad.
    const auto* yuri_inf = yuri.find_infantry("YURI");
    require(yuri_inf && yuri_inf->primary == "MindControl" &&
            yuri_inf->cost == 800 && yuri_inf->strength == 100 &&
            yuri_inf->speed == 4 && yuri_inf->armor == "none" &&
            yuri_inf->immune_to_psionics,
            "Basic Yuri rules changed");
    const auto* prime = yuri.find_infantry("YURIPR");
    require(prime && prime->primary == "SuperMindControl" && prime->cost == 1500 &&
            prime->build_limit == 1,
            "Yuri Prime rules changed");
    require(yuri.find_infantry("BRUTE") && yuri.find_infantry("VIRUS") &&
            yuri.find_infantry("INIT"),
            "Brute, Virus or Initiate is missing");
    // Yuri vehicles.
    const auto* mastermind = yuri.find_vehicle("MIND");
    require(mastermind && mastermind->primary == "MultipleMindControlTank" &&
            mastermind->cost == 1750,
            "Mastermind rules changed");
    const auto* disc = yuri.find_vehicle("DISK");
    require(disc && disc->jumpjet == false && disc->cloakable == false &&
            disc->primary == "DiskLaser" && disc->secondary == "DiskDrain" &&
            disc->cost == 1750,
            "Floating Disc rules changed");
    const auto* boomer = yuri.find_vehicle("BSUB");
    require(boomer && boomer->naval && boomer->cloakable &&
            boomer->primary == "BoomerTorpedo" && boomer->secondary == "CruiseLauncher" &&
            boomer->strength == 1200 && boomer->cost == 2000,
            "Boomer rules changed");
    const auto* magnetron = yuri.find_vehicle("TELE");
    require(magnetron && magnetron->primary == "MagneticBeam" &&
            magnetron->secondary == "MagneShake" && magnetron->cost == 1000,
            "Magnetron rules changed");
    require(yuri.find_vehicle("PCV") && yuri.find_vehicle("YHVR") &&
            yuri.find_vehicle("YTNK"),
            "Yuri Slave Miner, SAPC or Gattling Tank is missing");
    // Yuri buildings.
    const auto* tower = yuri.find_building("YAPSYT");
    require(tower && tower->primary == "MultipleMindControlTower" &&
            tower->powered && tower->power == -100 && tower->strength == 455,
            "Psychic Tower rules changed");
    const auto* grinder = yuri.find_building("YAGRND");
    require(grinder && grinder->grinding && grinder->cost == 600,
            "Grinder rules changed");
    require(yuri.find_building("NACLON") && yuri.find_building("YAGNTC") &&
            yuri.find_building("YACNST") && yuri.find_building("YAPOWR") &&
            yuri.find_building("YABRCK") && yuri.find_building("YAWEAP") &&
            yuri.find_building("YAYARD") && yuri.find_building("YATECH") &&
            yuri.find_building("YAGGUN") && yuri.find_building("YAREFN"),
            "A Yuri building is missing");
    const auto* conyard = yuri.find_building("YACNST");
    require(conyard && conyard->undeploys_into == "PCV",
            "Yuri construction yard cannot undeploy");
    // New Allied and Soviet units.
    const auto* ggi = yuri.find("GGI");
    require(ggi && ggi->kind == yu::YuriTechno::Kind::Infantry &&
            ggi->primary == "M60" && ggi->secondary == "MissileLauncher" &&
            ggi->deploy_to_fire == false && ggi->cost == 400,
            "Guardian GI rules changed");
    const auto* robo = yuri.find("ROBO");
    require(robo && robo->kind == yu::YuriTechno::Kind::Unit &&
            robo->primary == "Robogun" && robo->immune_to_psionics &&
            robo->cost == 600 && robo->speed == 10,
            "Robot Tank rules changed");
    const auto* fortress = yuri.find("BFRT");
    require(fortress && fortress->passengers == 5 && fortress->cost == 2000 &&
            fortress->primary == "20mmRapid",
            "Battle Fortress rules changed");
    const auto* chopper = yuri.find("SCHP");
    require(chopper && chopper->kind == yu::YuriTechno::Kind::Unit &&
            chopper->jumpjet && chopper->secondary == "160mm" && chopper->cost == 1100,
            "Siege Chopper rules changed");

    // artmd.ini additions.
    const auto* gattling_tank = yuri.find_vehicle("YTNK");
    // artmd.ini is not on every machine; check its fields only when given.
    require(gattling_tank && gattling_tank->gattling, "Gattling Tank rules changed");
    if (argc > 2)
        require(gattling_tank->voxel && gattling_tank->cameo == "YTNKICON",
                "Gattling Tank art changed");
    const auto* grinder_art = yuri.find_building("YAGRND");
    require(grinder_art, "Grinder is missing");
    if (argc > 2)
        require(grinder_art->foundation == "3x3" && grinder_art->buildup == "YAGRNDMK",
                "Grinder art changed");
    require(ggi->voxel == false, "Guardian GI must stay a sprite");

    // Mind control weapons from rulesmd.ini.
    require(yuri.mind_control.nodes == 1 && !yuri.mind_control.infinite &&
            yuri.mind_control.warhead == "Controller" &&
            !yuri.mind_control.controls_buildings(),
            "Basic mind control weapon changed");
    require(yuri.super_mind_control.nodes == 1 &&
            yuri.super_mind_control.controls_buildings(),
            "Yuri Prime mind control weapon changed");
    require(yuri.multi_tank_control.nodes == 3 && yuri.multi_tank_control.infinite,
            "Mastermind mind control weapon changed");
    require(yuri.multi_tower_control.nodes == 3 &&
            !yuri.multi_tower_control.infinite &&
            !yuri.multi_tower_control.controls_buildings(),
            "Psychic Tower mind control weapon changed");
    require(yuri.mind_control.verse_for("heavy") > 0.0 &&
            yuri.mind_control.verse_for("wood") == 0.0 &&
            yuri.super_mind_control.verse_for("concrete") > 0.0,
            "Mind control warhead verses changed");
    // Overload, drain, magnetron, refund and open-topped values.
    require(yuri.overload.count.size() == 4 && yuri.overload.count[0] == 3 &&
            yuri.overload.count[3] == 50,
            "OverloadCount changed");
    require(yuri.overload.damage.size() == 4 && yuri.overload.damage[1] == 50 &&
            yuri.overload.damage[3] == 500,
            "OverloadDamage changed");
    require(yuri.overload.frames.size() == 4 && yuri.overload.frames[0] == 30,
            "OverloadFrames changed");
    require(yuri.drain_delay == 30 && yuri.drain_amount == 30,
            "Floating Disc drain values changed");
    require(yuri.falling_damage_multiplier == 1.0 && yuri.current_strength_damage,
            "Magnetron drop rules changed");
    require(yuri.refund_percent == 50, "RefundPercent changed");
    require(yuri.open_topped.range_bonus == 2 &&
            std::abs(yuri.open_topped.damage_multiplier - 1.2) < 1e-6 &&
            yuri.open_topped.warp_distance == 7,
            "Battle Fortress open-topped rules changed");
    // Gattling stage pairs.
    require(yuri.gattling_tank.stages.size() == 3 &&
            yuri.gattling_tank.rate_up == 1 && yuri.gattling_tank.rate_down == 50,
            "Gattling Tank stage rules changed");
    require(yuri.gattling_tank.stages[0].ground.name == "AGGattling" &&
            yuri.gattling_tank.stages[0].air.name == "AAGattling" &&
            yuri.gattling_tank.stages[1].ground.name == "AGGattling2" &&
            yuri.gattling_tank.stages[2].air.name == "AAGattling3",
            "Gattling Tank stage weapons changed");
    require(yuri.gattling_tank.elite_stages[0].ground.name == "AGGattlingE" &&
            yuri.gattling_tank.elite_stages[2].air.name == "AAGattling3E",
            "Gattling Tank elite stage weapons changed");
    require(yuri.gattling_tank.stages[0].ground.anti_ground &&
            !yuri.gattling_tank.stages[0].ground.anti_air &&
            yuri.gattling_tank.stages[0].air.anti_air &&
            !yuri.gattling_tank.stages[0].air.anti_ground,
            "Gattling projectile targets changed");
    require(yuri.gattling_cannon.stages.size() == 3 &&
            yuri.gattling_cannon.stages[0].air.name == "AAGattCann",
            "Gattling Cannon stage weapons changed");

    // --- Mind control behaviour (CaptureManager prototype). ---------------
    yu::MindController tower_ctl(yuri.multi_tower_control.nodes,
                                   yuri.multi_tower_control.infinite, yuri.overload);
    require(tower_ctl.full() == false && tower_ctl.overloaded() == false,
            "A fresh controller is not empty");
    require(tower_ctl.capture(1, 10) && tower_ctl.capture(2, 20) && tower_ctl.capture(3, 30),
            "A tower_ctl cannot capture three targets");
    require(tower_ctl.node_count() == 3 && tower_ctl.full(),
            "Tower node count is wrong");
    // The fourth capture drops every link first.
    require(tower_ctl.capture(4, 40), "The tower_ctl cannot capture a fourth target");
    require(tower_ctl.node_count() == 1 && tower_ctl.controls(4) && !tower_ctl.controls(1),
            "The tower_ctl kept its old links");
    require(tower_ctl.overload_tick() == 0,
            "A non-infinite controller takes no overload damage");

    // Yuri Prime swaps its single target.
    yu::MindController prime_control(yuri.mind_control.nodes,
                                           yuri.mind_control.infinite, yuri.overload);
    require(prime_control.capture(7, 70), "Yuri cannot capture");
    require(prime_control.capture(8, 80), "Yuri cannot swap targets");
    require(prime_control.node_count() == 1 && prime_control.controls(8),
            "Yuri did not swap targets");
    require(prime_control.free(8) == 80, "Free lost the original owner");
    require(prime_control.free(8) == 0, "Free returned a ghost owner");

    // The Mastermind overloads past its limit.
    yu::MindController mastermind_control(yuri.multi_tank_control.nodes,
                                                yuri.multi_tank_control.infinite,
                                                yuri.overload);
    for (std::uint32_t t = 1; t <= 3; ++t)
        require(mastermind_control.capture(t, 100 + t),
                "The Mastermind cannot capture three targets");
    require(mastermind_control.capture(4, 104),
            "The Mastermind cannot capture past its limit");
    require(mastermind_control.overloaded(), "The Mastermind is not overloaded");
    std::int32_t damage = 0;
    for (int frame = 0; frame < 60; ++frame) damage += mastermind_control.overload_tick();
    require(damage == 50, "Mastermind overload damage is wrong");
    require(mastermind_control.free(4) == 104, "Mastermind free lost the owner");
    require(!mastermind_control.overloaded(),
            "A Mastermind under its limit still overloads");

    // Capture checks in original order.
    require(tower_ctl.can_capture(true, 1.0, false, false, false) == false,
            "A tower_ctl captured its own kind");
    require(tower_ctl.can_capture(false, 0.0, false, false, false) == false,
            "The warhead hurt a zero-verse armor");
    require(tower_ctl.can_capture(false, 1.0, true, false, false) == false,
            "The tower_ctl captured an immune target");
    require(tower_ctl.can_capture(false, 1.0, false, true, false) == false,
            "The tower_ctl stole a non-permanent link");
    require(tower_ctl.can_capture(false, 1.0, false, true, true),
            "A permanent link cannot steal a target");

    // --- Gattling stages (TechnoClass rate up/down prototype). ------------
    yu::GattlingState tank_gattling(yuri.gattling_tank);
    require(tank_gattling.stage() == 0, "Gattling does not start at stage 0");
    fire_for(tank_gattling, 199);
    require(tank_gattling.stage() == 0, "Gattling left stage 0 too early");
    fire_for(tank_gattling, 1);
    require(tank_gattling.stage() == 1, "Gattling stage 1 is wrong");
    fire_for(tank_gattling, 200);
    require(tank_gattling.stage() == 2, "Gattling stage 2 is wrong");
    const auto* stage_weapon = tank_gattling.weapon(false, false);
    require(stage_weapon && stage_weapon->name == "AGGattling3",
            "Stage 2 ground weapon is wrong");
    require(tank_gattling.weapon_number(true) == 5,
            "Stage 2 air weapon number is wrong");
    fire_for(tank_gattling, 1000); // The value stops at Stage3=600.
    cool_for(tank_gattling, 1);    // 600 -> 550 -> still stage 2.
    require(tank_gattling.stage() == 2, "Gattling cooled too fast");
    cool_for(tank_gattling, 3);    // 400: Stage2 is reached, still stage 2.
    require(tank_gattling.stage() == 2, "Gattling stage 2 threshold is wrong");
    cool_for(tank_gattling, 1);    // 350.
    require(tank_gattling.stage() == 1, "Gattling did not cool down");
    cool_for(tank_gattling, 10);
    require(tank_gattling.stage() == 0, "Gattling did not cool to stage 0");
    // Elite stages use their own thresholds and weapon table.
    yu::GattlingState elite_gattling(yuri.gattling_tank);
    for (int i = 0; i < 100; ++i) elite_gattling.tick(true, true);
    require(elite_gattling.stage() == 1, "Elite stage 1 threshold is wrong");
    const auto* elite_weapon = elite_gattling.weapon(false, true);
    require(elite_weapon && elite_weapon->name == "AGGattling2E",
            "Elite stage 1 weapon is wrong");
    yu::GattlingState cannon_gattling(yuri.gattling_cannon);
    fire_for(cannon_gattling, 200);
    require(cannon_gattling.stage() == 1 &&
            cannon_gattling.weapon(true, false)->name == "AAGattCann2",
            "Gattling Cannon stage 1 air weapon is wrong");

    // --- Grinder, Cloning Vats, Magnetron, Floating Disc. -----------------
    require(yu::grinder_refund(*yuri_inf, yuri.refund_percent) == 400,
            "Grinder refund for a Yuri is wrong");
    require(yu::grinder_refund(*mastermind, yuri.refund_percent) == 875,
            "Grinder refund for a Mastermind is wrong");
    require(yu::grinder_refund(*tower, yuri.refund_percent) == 0,
            "The grinder paid for a building");
    require(yu::cloning_vats_duplicates(*yuri_inf),
            "The Cloning Vats skipped a trained infantry");
    require(!yu::cloning_vats_duplicates(*mastermind),
            "The Cloning Vats cloned a vehicle");

    yu::MagnetronBeam beam;
    require(!beam.active(), "A fresh beam is active");
    beam.start(9, 10);
    require(beam.active() && beam.source() == 9 && beam.target() == 10,
            "The beam did not start");
    ra2map::Cell target_cell{10, 10};
    const ra2map::Cell source_cell{5, 5};
    int drags = 0;
    while (beam.drag(target_cell, source_cell)) ++drags;
    require(drags == 4 && !beam.active() &&
            target_cell.x == source_cell.x && target_cell.y == source_cell.y,
            "The beam did not drag the target to the Magnetron");
    require(beam.drop_damage(300, 300, yuri.falling_damage_multiplier,
                             yuri.current_strength_damage) == 300,
            "Drop damage ignored the current strength");
    require(beam.drop_damage(100, 300, 1.0, false) == 300,
            "Drop damage ignored the maximum strength");
    require(beam.drop_damage(0, 300, 1.0, true) == 0,
            "A dead target takes drop damage");

    yu::DiscDrain drain;
    require(!drain.active(), "A fresh drain is active");
    drain.start(77);
    require(drain.active() && drain.target() == 77, "The drain did not start");
    std::int32_t drained = 0;
    for (int frame = 0; frame < 60; ++frame) drained += drain.tick(yuri.drain_delay, yuri.drain_amount, 1000);
    require(drained == 60, "The disc drained the wrong amount");
    std::int32_t last_pulse = 0;
    for (int frame = 0; frame < 30; ++frame)
        last_pulse += drain.tick(yuri.drain_delay, yuri.drain_amount, 20);
    require(last_pulse == 20,
            "The disc drained more than the building had");
    require(drain.tick(yuri.drain_delay, yuri.drain_amount, 0) == 0,
            "The disc drained an empty building");
    drain.stop();
    require(!drain.active(), "The drain did not stop");

    // --- Boomer, Siege Chopper, Guardian GI, Robot Tank. ------------------
    yu::BoomerState boomer_state;
    require(!boomer_state.submerged(), "A fresh Boomer is submerged");
    boomer_state.submerge();
    require(boomer_state.submerged(), "The Boomer did not submerge");
    boomer_state.surface();
    require(!boomer_state.submerged(), "The Boomer did not surface");
    require(std::string(yu::BoomerState::weapon(true)) == "BoomerTorpedo",
            "The Boomer torpedo choice is wrong");
    require(std::string(yu::BoomerState::weapon(false)) == "CruiseLauncher",
            "The Boomer missile choice is wrong");

    yu::SiegeChopperState chopper_state;
    require(!chopper_state.deployed(), "A fresh chopper is deployed");
    require(chopper_state.weapon(chopper->secondary) == nullptr,
            "A flying chopper fired the artillery");
    chopper_state.deploy();
    require(chopper_state.deployed() &&
            std::string(chopper_state.weapon(chopper->secondary)) == "160mm",
            "The deployed chopper did not use the artillery");
    chopper_state.undeploy();
    require(!chopper_state.deployed(), "The chopper did not undeploy");

    require(std::string(yu::guardian_gi_weapon(false, ggi->primary, ggi->secondary)) == "M60",
            "A standing Guardian GI used the missile launcher");
    require(std::string(yu::guardian_gi_weapon(true, ggi->primary, ggi->secondary)) ==
                "MissileLauncher",
            "A deployed Guardian GI used the rifle");

    const auto* center = yuri.find_building("GAROBO");
    require(center && center->owned_by("YuriCountry"),
            "The Robot Control Center is missing");
    const yu::YuriTechno& robot_center = *center;
    require(yu::robot_tank_needs_center(*robo, robot_center),
            "The Robot Tank does not need a Robot Control Center");
    require(!yu::robot_tank_needs_center(*mastermind, robot_center),
            "The Mastermind needs a Robot Control Center");
    require(yu::robot_tank_immune_to_psionics(*robo),
            "The Robot Tank is not immune to psionics");

    // --- Loader errors with small fixtures. -------------------------------
    expect_load_throws("[InfantryTypes]\n1=E1\n",
                       "A rules file without [Sides] was accepted");
    expect_load_throws("[Sides]\nGDI=Americans\n",
                       "Rules without ThirdSide were accepted");
    expect_load_throws(
        "[Sides]\nThirdSide=YuriCountry\n"
        "[Countries]\n9=YuriCountry\n"
        "[InfantryTypes]\n7=YURI\n"
        "[VehicleTypes]\n74=MIND\n"
        "[BuildingTypes]\n314=YAGGUN\n"
        "[YURI]\nOwner=YuriCountry\n"
        "[MIND]\nOwner=YuriCountry\n"
        "[YAGGUN]\nOwner=YuriCountry\n"
        "[MindControl]\nWarhead=Controller\n"
        "[Controller]\nMindControl=yes\n"
        "[SuperMindControl]\nWarhead=Controller\n"
        "[MultipleMindControlTank]\nWarhead=Controller\n"
        "[MultipleMindControlTower]\nWarhead=Controller\n",
        "A non-mind-control warhead was accepted");
    std::cout << "Yuri's Revenge content passed: side, units, buildings, mind control, "
                 "gattling stages, grinder, cloning vats, magnetron, disc drain, "
                 "Boomer, Siege Chopper, Guardian GI, Robot Tank.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

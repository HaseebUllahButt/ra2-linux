#include "build_time.h"
#include "sim.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        using namespace ra2::sim;
        for (int fixture = 0; fixture < 5; ++fixture) {
            BuildTimeInputs value;
            value.cost = 1000;
            value.type_multiplier = 0.5f;
            value.power_output = value.power_drain = 100;
            if (fixture == 1) value.edition = BuildEdition::YurisRevenge;
            if (fixture == 2) value.power_output = 99;
            if (fixture == 3) value.factories = 2;
            if (fixture == 4) value.wall = true;
            constexpr int original_frames[] = {629,314,786,503,1887};
            constexpr int original_factory_arrival[] = {594,270,756,486,1836};
            const auto frames = original_build_time(value);
            if (frames != original_frames[fixture]) throw std::runtime_error("Original production-time vector changed");
            Factory factory;
            House house;
            house.cash = 1000; // Explicit resolved charge; no cost multiplier binding is inferred.
            factory.begin(1000, frames, 0);
            for (int tick = 0; tick <= original_factory_arrival[fixture]; ++tick) {
                factory.tick(house, tick);
                if (factory.done() != (tick == original_factory_arrival[fixture]))
                    throw std::runtime_error("Original production-time and cash timer diverged");
            }
            if (house.cash != 0 || house.spent != 1000 || factory.remaining_cost)
                throw std::runtime_error("Original production-time replay changed cash accounting");
        }
        std::cout << "Five original build-time vectors and native cash factory replays passed.\n";
        return 0;
    }
    if (argc != 1) throw std::runtime_error("Usage: sim_build_time_test [--self-test]");
    int edition, wall;
    ra2::sim::BuildTimeInputs input;
    while (std::cin >> edition >> input.cost >> input.build_speed >> input.country_multiplier >>
           input.type_multiplier >> input.power_output >> input.power_drain >> input.minimum_low_power >>
           input.maximum_low_power >> input.low_power_penalty >> input.multiple_factory >> input.factories >>
           wall >> input.wall_coefficient) {
        if ((edition != 0 && edition != 1) || (wall != 0 && wall != 1))
            throw std::invalid_argument("Invalid original build-time fixture");
        input.edition = edition ? ra2::sim::BuildEdition::YurisRevenge : ra2::sim::BuildEdition::BaseRa2;
        input.wall = wall != 0;
        std::cout << ra2::sim::original_build_time(input) << '\n';
    }
    if (!std::cin.eof()) throw std::invalid_argument("Invalid original build-time input");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

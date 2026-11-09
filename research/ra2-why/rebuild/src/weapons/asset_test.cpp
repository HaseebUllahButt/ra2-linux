#include <fs/mix_bootstrap.h>
#include <rules/ini_file.h>
#include <stdexcept>
#include <string_view>
#include <iostream>
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Usage: weapons_asset_test <game-dir>");
    ra2::fs::MixFileSystem files(argv[1]);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount("conquer.mix", false);
    auto bytes = files.read_file("rules.ini");
    if (!bytes) throw std::runtime_error("missing base rules.ini");
    ra2::rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char *>(bytes->data()), bytes->size()));
    if (ini.read_string("HTNK", "Primary") != "120mm" ||
        ini.read_integer("120mm", "Damage") != 90 ||
        ini.read_integer("120mm", "ROF") != 65 ||
        ini.read_double("120mm", "Range") != 5.75 ||
        ini.contains("120mm", "Burst") || ini.contains("120mm", "DecloakToFire") ||
        ini.read_integer("HTNK", "Strength") != 400 ||
        ini.read_double("AP", "CellSpread") != static_cast<double>(0.3f) ||
        ini.read_double("AP", "PercentAtMax") != 0.5 ||
        ini.read_integer("120mm", "Speed") != 40 ||
        ini.read_string("120mm", "Projectile") != "Cannon" ||
        ini.read_string("120mm", "Warhead") != "AP" ||
        !ini.read_bool("Cannon", "Arcing") ||
        !ini.read_bool("Cannon", "SubjectToCliffs") ||
        !ini.read_bool("Cannon", "SubjectToElevation") ||
        !ini.read_bool("Cannon", "SubjectToWalls") ||
        ini.read_integer("AudioVisual", "Gravity") != 6)
      throw std::runtime_error("unexpected shipped HTNK rules");
    std::cout << "Original HTNK, 120mm, Cannon and gravity rules match\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

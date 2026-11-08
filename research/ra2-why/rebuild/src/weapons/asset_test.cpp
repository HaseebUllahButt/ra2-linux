#include <fs/mix_bootstrap.h>
#include <rules/ini_file.h>
#include <stdexcept>
#include <string_view>
#include <iostream>
int main() {
  try {
    ra2::fs::MixFileSystem files("/path/to/user/ra2/game");
    files.bootstrap();
    files.mount("conquer.mix", false);
    auto bytes = files.read_file("rules.ini");
    if (!bytes) throw std::runtime_error("missing base rules.ini");
    ra2::rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char *>(bytes->data()), bytes->size()));
    if (ini.read_string("HTNK", "Primary") != "120mm" ||
        ini.read_integer("120mm", "Damage") != 90 ||
        ini.read_integer("120mm", "ROF") != 65 ||
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

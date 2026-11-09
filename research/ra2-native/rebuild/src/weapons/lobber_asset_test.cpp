#include <fs/mix_bootstrap.h>
#include <rules/ini_file.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
int main(int argc, char **argv) {
  try {
    if (argc != 2) throw std::invalid_argument("usage: lobber_asset_test <game-dir>");
    ra2::fs::MixFileSystem files(argv[1]);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount("conquer.mix", false);
    auto bytes = files.read_file("rules.ini");
    if (!bytes) throw std::runtime_error("missing base rules.ini");
    ra2::rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char *>(bytes->data()), bytes->size()));
    if (ini.read_string("HTNK", "Primary") != "120mm" ||
        ini.read_bool("120mm", "Lobber", false))
      throw std::runtime_error("unexpected base HTNK Lobber setting");
    std::cout << "Base HTNK 120mm uses Lobber=false\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

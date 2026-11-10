#include "fs/mix_bootstrap.h"
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: extract_file GAME_DIR FILE\n";
    return 2;
  }
  ra2::fs::MixFileSystem files(argv[1]);
  files.mount_language(ra2::fs::GameVariant::RedAlert2);
  files.bootstrap(ra2::fs::GameVariant::RedAlert2);
  files.mount_overlays(); files.mount_resources();
  auto b = files.read_file(argv[2]);
  if (!b) {
    std::cerr << "file not found: " << argv[2] << "\n";
    return 1;
  }
  std::cout.write(reinterpret_cast<const char*>(b->data()), b->size());
  return 0;
}

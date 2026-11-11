#include "fs/mix_bootstrap.h"
#include <fstream>
#include <iostream>
int main(int argc,char **argv) {
    if(argc!=4) return 2;
    ra2::fs::MixFileSystem fs(argv[1]);
    fs.mount_language(ra2::fs::GameVariant::RedAlert2);
    fs.bootstrap(ra2::fs::GameVariant::RedAlert2);
    fs.mount_overlays(); fs.mount_resources({}); fs.mount("MOVIES01.MIX");
    auto bytes=fs.read_file(argv[2]); if(!bytes) { std::cerr<<"Movie missing\n"; return 1; }
    std::ofstream file(argv[3],std::ios::binary); file.write(reinterpret_cast<const char *>(bytes->data()),bytes->size());
    std::cout<<argv[2]<<": "<<bytes->size()<<" bytes\n"; return file ? 0 : 1;
}

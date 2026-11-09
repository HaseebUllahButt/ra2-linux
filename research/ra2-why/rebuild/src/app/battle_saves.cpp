#include "battle_saves.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ra2::app {
namespace {
constexpr char magic[8]={'R','A','2','N','S','L','O','T'};
constexpr std::uint32_t version=1, max_description=255;
void put32(std::string& s,std::uint32_t v) { for(int i=0;i<4;++i) s.push_back(char((v>>(8*i))&255)); }
std::uint32_t get32(const std::string& s,std::size_t p) {
    std::uint32_t v=0; for(int i=0;i<4;++i) v|=std::uint32_t(std::uint8_t(s[p+i]))<<(8*i); return v;
}
std::optional<std::string> read_all(const std::filesystem::path& p) {
    std::ifstream in(p,std::ios::binary); if(!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)),{});
}
struct Parsed { std::string description, payload; };
std::optional<Parsed> parse(const std::string& s) {
    if(s.size()<16 || !std::equal(magic,magic+8,s.begin()) || get32(s,8)!=version) return std::nullopt;
    const auto n=get32(s,12);
    if(n>max_description || s.size()<16+std::size_t(n)) return std::nullopt;
    return Parsed{s.substr(16,n),s.substr(16+n)};
}
}

BattleSaves::BattleSaves(std::filesystem::path folder) : folder_(std::move(folder)) {}

bool BattleSaves::valid_name(const std::string& f) {
    if(f.size()!=12 || f.compare(0,4,"SAVE")!=0 || f.compare(8,4,".SAV")!=0) return false;
    return std::all_of(f.begin()+4,f.begin()+8,[](char c){return std::isdigit(std::uint8_t(c))||(c>='A'&&c<='F');});
}

std::vector<SaveSlot> BattleSaves::list() const {
    std::vector<SaveSlot> out; std::error_code ec;
    for(const auto& e:std::filesystem::directory_iterator(folder_,ec)) {
        const auto name=e.path().filename().string();
        if(!valid_name(name) || !e.is_regular_file(ec)) continue;
        const auto bytes=read_all(e.path()); if(!bytes) continue;
        if(const auto p=parse(*bytes)) out.push_back({name,p->description});
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.file<b.file;});
    return out;
}

std::optional<std::string> BattleSaves::save(const std::string& description, const std::string& payload) {
    std::error_code ec;
    for(unsigned n=0;n<=0xFFFF;++n) {
        char name[16]; std::snprintf(name,sizeof name,"SAVE%04X.SAV",n);
        if(std::filesystem::exists(folder_/name,ec)) continue;
        if(save_as(name,description,payload)) return std::string(name);
        return std::nullopt;
    }
    return std::nullopt;
}

bool BattleSaves::save_as(const std::string& file, const std::string& description, const std::string& payload) {
    if(!valid_name(file) || description.size()>max_description) return false;
    std::string data(magic,magic+8); put32(data,version); put32(data,std::uint32_t(description.size()));
    data+=description; data+=payload;
    std::error_code ec; std::filesystem::create_directories(folder_,ec);
    // Write a temporary file first so a failed write keeps the old slot.
    const auto target=folder_/file, temp=folder_/(file+".tmp");
    {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        if(!out.write(data.data(),std::streamsize(data.size()))) { std::filesystem::remove(temp,ec); return false; }
    }
    std::filesystem::rename(temp,target,ec);
    if(ec) { std::filesystem::remove(temp,ec); return false; }
    return true;
}

std::optional<std::string> BattleSaves::load(const std::string& file) const {
    if(!valid_name(file)) return std::nullopt;
    const auto bytes=read_all(folder_/file); if(!bytes) return std::nullopt;
    const auto p=parse(*bytes); if(!p) return std::nullopt;
    return p->payload;
}

bool BattleSaves::remove(const std::string& file) {
    if(!valid_name(file)) return false;
    std::error_code ec; return std::filesystem::remove(folder_/file,ec);
}
}

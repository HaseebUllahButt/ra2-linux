#include "mix_bootstrap.h"

#include <algorithm>
#include <functional>
#include <iostream>

using namespace ra2::fs;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void put16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
std::vector<std::uint8_t> fixture(std::string_view name, FilenameHash hash,
                                 const std::vector<std::uint8_t>& payload, bool classic,
                                 std::uint32_t flags = 0) {
    std::vector<std::uint8_t> bytes;
    if (!classic) put32(bytes, flags);
    put16(bytes, 1); put32(bytes, static_cast<std::uint32_t>(payload.size()));
    put32(bytes, filename_hash(name, hash)); put32(bytes, 0);
    put32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (flags & MixArchive::Checksum) bytes.resize(bytes.size() + 20);
    return bytes;
}
void rejects(std::vector<std::uint8_t> bytes) {
    try { (void)MixArchive::from_bytes(std::move(bytes)); }
    catch (const MixError&) { return; }
    throw std::runtime_error("Malformed archive was accepted");
}
void self_tests() {
    // Fixed CRC answers independently computed with Python's zlib.crc32.
    const std::pair<const char*, std::uint32_t> crc_vectors[] = {
        {"", 0}, {"a", 0xeb978531}, {"ab", 0x421faa6e}, {"abc", 0x33aff496},
        {"abcd", 0xdb1720a5}, {"abcde", 0x518e3366}, {"rulesmd.ini", 0x8218f9f4},
        {"local mix database.dat", 0x366e051f}
    };
    for (auto vector : crc_vectors) require(crc_filename_hash(vector.first) == vector.second, "CRC hash vector");
    require(classic_filename_hash("a") == 0x41, "Classic short name");
    require(classic_filename_hash("abcd") == 0x44434241, "Classic word order");
    require(classic_filename_hash("abcde") == 0x888684c7, "Classic rotation");
    const std::vector<std::uint8_t> payload{'t', 'e', 's', 't'};
    for (bool classic : {false, true}) {
        const auto type = classic ? FilenameHash::Classic : FilenameHash::Crc32;
        auto bytes = fixture("a.bin", type, payload, classic);
        auto archive = MixArchive::from_bytes(bytes);
        const auto* entry = archive->find("A.BIN", type);
        require(archive->is_classic() == classic && entry, "Header or case folding");
        require(archive->entries().size() == 1 && archive->read(*entry) == payload, "Entry read");
        require(!archive->find("missing", type), "Missing entry");
        for (std::size_t size = 0; size < bytes.size(); ++size)
            rejects(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + size));
    }
    auto child = fixture("child.bin", FilenameHash::Crc32, payload, false);
    auto parent = MixArchive::from_bytes(fixture("child.mix", FilenameHash::Crc32, child, false));
    auto nested = parent->open_nested(*parent->find("child.mix"), "child.mix");
    parent.reset(); // Child retains the backing data's lifetime.
    require(nested->read(*nested->find("child.bin")) == payload, "Nested archive lifetime/range");
    auto bad_entry = fixture("bad", FilenameHash::Crc32, payload, false);
    bad_entry[14] = 0xff; // Entry offset lies outside declared data section.
    rejects(bad_entry);
    auto bad_size = fixture("bad", FilenameHash::Crc32, payload, false);
    bad_size[6] = 0xff; rejects(bad_size);
    std::vector<std::uint8_t> unsupported(10); unsupported[2] = 4; rejects(unsupported);
    std::vector<std::uint8_t> encrypted(91); encrypted[2] = 2; rejects(encrypted);
    auto checksummed = fixture("a", FilenameHash::Crc32, payload, false, MixArchive::Checksum);
    require(MixArchive::from_bytes(checksummed)->has_checksum(), "Checksum flag");
    checksummed.pop_back(); rejects(checksummed);
    require(MixArchive::from_bytes(std::vector<std::uint8_t>(10))->entries().empty(), "Empty extended MIX");
    std::cout << "Self-tests passed (hash vectors, headers, bounds, nested lifetime).\n";
}
void print_archive(const MixArchive& archive) {
    std::cout << archive.name() << ": " << archive.entries().size() << " entries, flags=0x"
              << std::hex << archive.flags() << std::dec << ", data=" << archive.data_size()
              << ", start=" << archive.data_offset() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        self_tests();
        if (argc > 1 && std::string_view(argv[1]) == "--self-test") return 0;
        if (argc == 3 && std::string_view(argv[1]) == "--archive") {
            print_archive(*MixArchive::open(argv[2])); return 0;
        }
        const std::filesystem::path game = argc > 1 ? argv[1] : "/path/to/user/ra2/game";
        std::vector<std::filesystem::path> paths;
        for (const auto& item : std::filesystem::directory_iterator(game)) {
            auto extension = item.path().extension().string();
            for (char& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
            if (item.is_regular_file() && extension == ".mix") paths.push_back(item.path());
        }
        std::sort(paths.begin(), paths.end());
        require(!paths.empty(), "No real archives found");
        std::size_t encrypted = 0;
        for (const auto& path : paths) {
            auto archive = MixArchive::open(path); print_archive(*archive);
            encrypted += archive->encrypted();
            // Exercise payload positioning without loading large archive members.
            for (const auto& entry : archive->entries())
                if (entry.size && entry.size <= 4096) { require(archive->read(entry).size() == entry.size, "Real payload read"); break; }
        }
        MixFileSystem fs(game);
        fs.bootstrap(); fs.mount_overlays(); fs.mount_resources();
        std::cout << "Mounted search order:\n";
        for (const auto& archive : fs.mixes()) print_archive(*archive);
        const auto rules = fs.read_file("rulesmd.ini");
        require(rules && !rules->empty(), "RULESMD.INI did not resolve through filesystem");
        const std::string rules_text(rules->begin(), rules->end());
        require(rules_text.find("[General]") != std::string::npos, "RULESMD.INI payload is not the expected INI");
        std::cout << "Verified " << paths.size() << " disk archives (" << encrypted << " encrypted), "
                  << fs.mixes().size() << " mounts; RULESMD.INI: " << rules->size() << " bytes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MIX test failed: " << error.what() << '\n'; return 1;
    }
}

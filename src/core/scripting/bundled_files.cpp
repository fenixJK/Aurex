#include "bundled_files.hpp"
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace hive::scripting {
namespace {
std::optional<std::string> read(const std::filesystem::path& path,std::uintmax_t limit=65536) {
    if (std::filesystem::is_symlink(path)) throw std::runtime_error("Bundled script files cannot be symbolic links");
    if (!std::filesystem::exists(path)) return {};
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>limit)
        throw std::runtime_error("Invalid bundled script file");
    std::ifstream input(path,std::ios::binary);
    std::ostringstream content; content<<input.rdbuf();
    auto source=content.str();
    if (!input || input.bad() || source.size()>limit) throw std::runtime_error("Could not read bundled script file");
    return source;
}
std::uint64_t fingerprint(const std::string& source) {
    std::uint64_t hash=14695981039346656037ULL;
    for (unsigned char byte:source) { hash^=byte; hash*=1099511628211ULL; }
    return hash;
}
void write(const std::filesystem::path& file,const std::string& source) {
    auto temporary=file; temporary+=".update";
    if (std::filesystem::is_symlink(temporary)) throw std::runtime_error("Script update cannot be a symbolic link");
    std::ofstream output(temporary,std::ios::binary); output<<source; output.close();
    if (!output) { std::filesystem::remove(temporary); throw std::runtime_error("Could not save bundled script file"); }
    std::filesystem::rename(temporary,file);
}
}
std::vector<std::string> install_bundled_files(const std::filesystem::path& directory,
    const BundledFiles& bundled,const LegacyFiles& legacy) {
    if (std::filesystem::is_symlink(directory)) throw std::runtime_error("Script folder cannot be a symbolic link");
    std::filesystem::create_directories(directory);
    const auto baseline=directory/".defaults";
    const auto saved=read(baseline,16384);
    std::map<std::string,std::uint64_t> previous;
    if (saved) {
        std::istringstream input(*saved); std::string name; std::uint64_t hash;
        // Quoting supports pattern names with spaces and the original unquoted movement manifest.
        while (input>>std::ws && !input.eof()) {
            if (!(input>>std::quoted(name)>>std::hex>>hash) || !previous.emplace(name,hash).second)
                throw std::runtime_error("Invalid bundled script defaults");
        }
    }
    std::vector<std::string> errors;
    std::map<std::string,std::uint64_t> next;
    for (const auto& [name,source]:bundled) {
        const auto file=directory/(name+".lua");
        const auto prior=previous.find(name);
        if (prior!=previous.end()) next.emplace(*prior);
        try {
            const auto installed=read(file);
            bool replace=!installed;
            if (installed && *installed!=source) {
                const auto hash=fingerprint(*installed);
                const auto old=legacy.find(name);
                replace=(prior!=previous.end() && prior->second==hash)
                    || (old!=legacy.end() && old->second.contains(hash));
            }
            if (replace) write(file,source);
            next[name]=fingerprint(source);
        } catch (const std::exception& error) { errors.push_back(name+": "+error.what()); }
    }
    // Record successful upgrades and retain failed files' old baselines for retry.
    std::ostringstream defaults;
    for (const auto& [name,hash]:next) defaults<<std::quoted(name)<<' '<<std::hex<<hash<<'\n';
    if (saved!=defaults.str()) write(baseline,defaults.str());
    return errors;
}
}

#include "scripts.hpp"
#include "core/scripting/program.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include "bundled_patterns.hpp"

namespace hive::features::gathering {
namespace {
std::string read_source(const std::filesystem::path& file) {
    if (std::filesystem::is_symlink(file) || !std::filesystem::is_regular_file(file))
        throw std::runtime_error("Pattern must be a regular file");
    std::ifstream input(file,std::ios::binary); std::array<char,65537> buffer;
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    const auto size=static_cast<std::size_t>(input.gcount());
    if (input.bad() || size==0 || size>65536) throw std::runtime_error("Pattern must contain between 1 byte and 64 KiB");
    return std::string(buffer.data(),size);
}
PatternPreset script_preset(std::string name,const std::string& source,std::filesystem::path file={}) {
    PatternPreset preset; preset.name=std::move(name);
    auto bundled=file.empty()?std::make_shared<const std::string>(source):nullptr;
    preset.configured=[bundled=std::move(bundled),file=std::move(file)](const Settings& parameters) -> automation::ProgramFactory {
        return [bundled,file,parameters] { return scripting::script_pattern({{"main",bundled?*bundled:read_source(file)}},parameters); };
    };
    preset.strategy=[configured=preset.configured](int width) {
        if (width<1 || width>12) throw std::invalid_argument("Pattern width must be between 1 and 12");
        return configured({{"width",width},{"scale",100},{"repetitions",1},{"facing",0},{"invert-fb",false},{"invert-lr",false}});
    };
    preset.strategy(3)();
    return preset;
}
}
PatternControls default_pattern_controls() {
    PatternControls result; result.initial="Square";
    for (const auto& [name,source]:bundled_patterns) result.presets.push_back(script_preset(name,source));
    return result;
}
std::vector<std::string> install_default_patterns(const std::filesystem::path& directory) {
    std::vector<std::string> errors;
    try {
        if (std::filesystem::is_symlink(directory)) throw std::runtime_error("Pattern folder cannot be a symbolic link");
        std::filesystem::create_directories(directory);
        for (const auto& [name,source]:bundled_patterns) {
            const auto path=directory/(name+".lua");
            if (std::filesystem::exists(path) || std::filesystem::is_symlink(path)) continue;
            std::ofstream output(path,std::ios::binary);
            output.write(source.data(),static_cast<std::streamsize>(source.size())); output.close();
            if (!output) {
                std::filesystem::remove(path);
                errors.push_back(name+": Could not create default pattern");
            }
        }
    } catch (const std::exception& error) { errors.push_back(error.what()); }
    return errors;
}
PatternFiles read_pattern_files(const std::filesystem::path& directory) {
    PatternFiles result;
    try {
        if (!std::filesystem::exists(directory)) return result;
        if (std::filesystem::is_symlink(directory) || !std::filesystem::is_directory(directory))
            throw std::runtime_error("Pattern folder must be a regular directory");
        std::vector<std::filesystem::path> files;
        std::size_t entries=0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (++entries>256) throw std::runtime_error("Pattern folder contains too many entries");
            if (entry.path().extension()==".lua") files.push_back(entry.path());
        }
        std::sort(files.begin(),files.end());
        for (const auto& file : files) {
            const auto stem=file.stem().u8string();
            const std::string name(stem.begin(),stem.end());
            try {
                if (name.empty() || name.size()>60 || !std::all_of(name.begin(),name.end(),[](unsigned char c) {
                    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c==' ';
                })) throw std::runtime_error("Use a filename with letters, numbers, spaces, hyphens or underscores");
                if (result.presets.size()>=64) throw std::runtime_error("At most 64 Lua patterns can be loaded");
                result.presets.push_back(script_preset(bundled_patterns.contains(name)?name:"Lua: "+name,{},file));
            } catch (const std::exception& error) {
                result.errors.push_back(name+".lua: "+error.what());
            }
        }
    } catch (const std::exception& error) {
        result.presets.clear(); result.errors.push_back(error.what());
    }
    return result;
}
}

#pragma once
// Unique same-volume staging; retain the old destination on write/close/rename failure.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
namespace AlterEgo::Storage {
inline bool ReplaceFile(const std::filesystem::path& target,const std::string& bytes,std::string& error) {
    namespace fs=std::filesystem;
    struct Stage {
        fs::path dir;
        ~Stage(){if(!dir.empty()){std::error_code ec;fs::remove(dir/"payload",ec);fs::remove(dir,ec);}}
    } stage;
    try {
        if(target.empty()||target.filename().empty())throw std::runtime_error("Missing file destination");
        const auto parent=target.has_parent_path()?target.parent_path():fs::current_path();
        fs::create_directories(parent);
        static std::atomic<uint64_t> seq{0};
        for(unsigned n=0;n<100;++n){
            auto candidate=parent/(".ae-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++seq));
            if(fs::create_directory(candidate)){stage.dir=std::move(candidate);break;}
        }
        if(stage.dir.empty())throw std::runtime_error("Cannot reserve file staging directory");
        const auto tmp=stage.dir/"payload";
        {
            std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
            if(!f||!f.write(bytes.data(),static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot stage file");
            f.flush();f.close();
            if(!f)throw std::runtime_error("Cannot flush or close staged file");
        }
        if(fs::file_size(tmp)!=bytes.size())throw std::runtime_error("Incomplete staged file");
#ifdef _WIN32
        if(!MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace destination file");
#else
        fs::rename(tmp,target);
#endif
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
} // namespace AlterEgo::Storage

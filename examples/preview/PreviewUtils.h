#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ave_preview {

std::filesystem::path Normalize(std::filesystem::path path);
std::string ReadTextFile(std::filesystem::path const& path);
std::vector<uint8_t> ReadBinaryFile(std::filesystem::path const& path);
std::filesystem::path ResolveProjectAsset(std::filesystem::path const& project_dir, std::string const& asset_path);
std::vector<uint8_t> ReadPreviewBinaryAsset(std::filesystem::path const& project_dir, std::string const& asset_path);
std::vector<uint32_t> ReadSpirv(std::filesystem::path const& path);
std::string QuoteCommandArg(std::filesystem::path const& path);
bool CompilePreviewJavaScripts(std::filesystem::path const& project_dir, std::filesystem::path const& class_dir);
std::string ProtocolEscape(std::string const& text);
std::string ProtocolUnescape(std::string const& text);
std::vector<std::string> SplitProtocolLine(std::string const& line);
std::vector<uint8_t> BuildPreviewFontAtlas(uint32_t width, uint32_t height);

template <typename... Args>
std::string MakeProtocolLine(std::string const& command, Args const&... args)
{
    std::ostringstream out;
    out << command;
    ((out << '|' << ProtocolEscape(args)), ...);
    out << '\n';
    return out.str();
}

class FileStampCache {
public:
    bool Refresh(std::filesystem::path const& root);

private:
    bool initialized_ = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> stamps_;
};

} // namespace ave_preview

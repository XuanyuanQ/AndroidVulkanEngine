#include "PreviewUtils.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ave_preview {

std::filesystem::path Normalize(std::filesystem::path path)
{
    return path.lexically_normal();
}

std::string ReadTextFile(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to read text asset: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

namespace {

std::string TrimAssetPath(std::string text)
{
    auto const first = text.find_first_not_of(" \t\r\n\"");
    if (first == std::string::npos) {
        return {};
    }
    auto const last = text.find_last_not_of(" \t\r\n\"");
    return text.substr(first, last - first + 1);
}

bool ShouldSkipWatchDirectory(std::filesystem::path const& root, std::filesystem::path const& path)
{
    std::error_code ec;
    auto const relative = std::filesystem::relative(path, root, ec);
    if (ec) {
        return false;
    }
    if (relative.empty()) {
        return false;
    }
    auto const first = relative.begin();
    if (first == relative.end()) {
        return false;
    }
    auto const name = first->string();
    return name == "build" || name == ".gradle" || name == ".idea" || name == ".vs";
}

} // namespace

std::vector<uint8_t> ReadBinaryFile(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
#if defined(_WIN32)
        HANDLE handle = CreateFileW(path.wstring().c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return {};
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) {
            CloseHandle(handle);
            return {};
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        BOOL const ok = ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        CloseHandle(handle);
        if (!ok || read != bytes.size()) {
            return {};
        }
        return bytes;
#else
        return {};
#endif
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path ResolveProjectAsset(std::filesystem::path const& project_dir, std::string const& asset_path)
{
    std::filesystem::path path(TrimAssetPath(asset_path));
    if (path.is_absolute()) {
        return Normalize(path);
    }
    return Normalize(project_dir / path);
}

std::vector<uint8_t> ReadPreviewBinaryAsset(std::filesystem::path const& project_dir, std::string const& asset_path)
{
    auto const resolved = ResolveProjectAsset(project_dir, asset_path);
    auto bytes = ReadBinaryFile(resolved);
    if (bytes.empty()) {
        std::error_code ec;
        bool const exists = std::filesystem::exists(resolved, ec);
#if defined(_WIN32)
        DWORD const win_error = GetLastError();
#endif
        std::cerr << "[preview] failed to read binary asset: " << asset_path
                  << " -> " << resolved
                  << " exists=" << (exists ? "true" : "false")
                  << " fs_error=" << (ec ? ec.message() : "none")
#if defined(_WIN32)
                  << " win_error=" << win_error
#endif
                  << "\n";
    }
    return bytes;
}

std::vector<uint32_t> ReadSpirv(std::filesystem::path const& path)
{
    auto bytes = ReadBinaryFile(path);
    if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
        return {};
    }
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

std::string QuoteCommandArg(std::filesystem::path const& path)
{
    std::string text = path.string();
    std::string quoted = "\"";
    for (char ch : text) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

bool CompilePreviewJavaScripts(std::filesystem::path const& project_dir, std::filesystem::path const& class_dir)
{
    std::filesystem::path const preview_java_dir = std::filesystem::path("examples") / "preview" / "java";
    std::filesystem::path const common_java_dir = std::filesystem::path("engine") / "runtime" / "java-common";
    std::filesystem::path const script_dir = project_dir / "scripts";
    if (!std::filesystem::exists(script_dir)) {
        return false;
    }

    std::vector<std::filesystem::path> sources;
    for (auto const& root : {common_java_dir, preview_java_dir, script_dir}) {
        if (!std::filesystem::exists(root)) {
            continue;
        }
        for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".java") {
                sources.push_back(entry.path());
            }
        }
    }
    if (sources.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(class_dir, ec);
    ec.clear();
    std::filesystem::create_directories(class_dir, ec);
    if (ec) {
        std::cerr << "[preview] failed to create Java class dir: " << class_dir << "\n";
        return false;
    }

    std::string command = "javac -encoding UTF-8 -d " + QuoteCommandArg(class_dir);
    for (auto const& source : sources) {
        command += " " + QuoteCommandArg(source);
    }
    int const result = std::system(command.c_str());
    if (result != 0) {
        std::cerr << "[preview] javac failed, Java scripts disabled for this reload\n";
        return false;
    }
    std::cout << "[preview] compiled Java scripts: " << class_dir << "\n";
    return true;
}

std::string ProtocolEscape(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '|': out += "\\p"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string ProtocolUnescape(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    bool escaped = false;
    for (char ch : text) {
        if (!escaped) {
            if (ch == '\\') {
                escaped = true;
            } else {
                out += ch;
            }
            continue;
        }
        switch (ch) {
        case 'p': out += '|'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        default: out += ch; break;
        }
        escaped = false;
    }
    if (escaped) {
        out += '\\';
    }
    return out;
}

std::vector<std::string> SplitProtocolLine(std::string const& line)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= line.size()) {
        size_t const end = line.find('|', start);
        parts.push_back(ProtocolUnescape(line.substr(start, end == std::string::npos ? std::string::npos : end - start)));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

std::vector<uint8_t> BuildPreviewFontAtlas(uint32_t width, uint32_t height)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u, 0u);
    uint32_t const cell_w = width / 16u;
    uint32_t const cell_h = height / 8u;

#if defined(_WIN32)
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width);
    bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(height);
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* dib_pixels = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateDIBSection(memory_dc, &bitmap_info, DIB_RGB_COLORS, &dib_pixels, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);

    RECT full_rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    HBRUSH black_brush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FillRect(memory_dc, &full_rect, black_brush);

    int const font_height = -static_cast<int>(std::max(8u, cell_h - 10u));
    HFONT font = CreateFontA(font_height,
                             0,
                             0,
                             0,
                             FW_NORMAL,
                             FALSE,
                             FALSE,
                             FALSE,
                             ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             FF_DONTCARE,
                             "Consolas");
    HGDIOBJ old_font = SelectObject(memory_dc, font);
    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, RGB(255, 255, 255));

    for (uint32_t glyph = 0; glyph < 128u; ++glyph) {
        uint32_t const col = glyph % 16u;
        uint32_t const row = glyph / 16u;
        RECT cell_rect{
            static_cast<LONG>(col * cell_w + 4u),
            static_cast<LONG>(row * cell_h + 4u),
            static_cast<LONG>((col + 1u) * cell_w - 4u),
            static_cast<LONG>((row + 1u) * cell_h - 4u),
        };
        char const ch = glyph >= 32u ? static_cast<char>(glyph) : ' ';
        DrawTextA(memory_dc, &ch, 1, &cell_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    auto const* bgra = static_cast<uint8_t const*>(dib_pixels);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t const idx = (static_cast<size_t>(y) * width + x) * 4u;
            uint8_t const b = bgra[idx + 0];
            uint8_t const g = bgra[idx + 1];
            uint8_t const r = bgra[idx + 2];
            uint8_t const a = std::max({r, g, b});
            pixels[idx + 0] = 255u;
            pixels[idx + 1] = 255u;
            pixels[idx + 2] = 255u;
            pixels[idx + 3] = a;
        }
    }

    SelectObject(memory_dc, old_font);
    DeleteObject(font);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
#else
    for (uint32_t glyph = 0; glyph < 128u; ++glyph) {
        uint32_t const col = glyph % 16u;
        uint32_t const row = glyph / 16u;
        uint32_t const x0 = col * cell_w;
        uint32_t const y0 = row * cell_h;
        for (uint32_t y = cell_h / 3u; y < cell_h * 2u / 3u; ++y) {
            for (uint32_t x = cell_w / 3u; x < cell_w * 2u / 3u; ++x) {
                size_t const idx = (static_cast<size_t>(y0 + y) * width + (x0 + x)) * 4u;
                pixels[idx + 0] = 255u;
                pixels[idx + 1] = 255u;
                pixels[idx + 2] = 255u;
                pixels[idx + 3] = 255u;
            }
        }
    }
#endif

    return pixels;
}

bool FileStampCache::Refresh(std::filesystem::path const& root)
{
    std::unordered_map<std::string, std::filesystem::file_time_type> next;
    if (!std::filesystem::exists(root)) {
        return false;
    }

    std::filesystem::recursive_directory_iterator it(root);
    std::filesystem::recursive_directory_iterator end;
    for (; it != end; ++it) {
        auto const& entry = *it;
        if (entry.is_directory() && ShouldSkipWatchDirectory(root, entry.path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        auto const path = entry.path();
        auto const ext = path.extension().string();
        if (ext != ".xml" && ext != ".spv" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
            ext != ".obj" && ext != ".vert" && ext != ".frag" && ext != ".comp" && ext != ".java") {
            continue;
        }
        next[path.generic_string()] = entry.last_write_time();
    }

    bool const changed = !initialized_ || next != stamps_;
    stamps_ = std::move(next);
    initialized_ = true;
    return changed;
}

} // namespace ave_preview

#include "tasset.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace templide::tasset {
    namespace {
        // 한글 경로도 열 수 있게 Windows에서는 넓은 문자로 연다
        std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
            const std::string narrow(mode);
            return _wfopen(path.c_str(), std::wstring(narrow.begin(), narrow.end()).c_str());
#else
            return std::fopen(path.c_str(), mode);
#endif
        }

        // 읽기용으로 연 묶음. 닫을 때 zip과 파일을 함께 닫는다
        struct Reader {
            std::FILE* file = nullptr;
            mz_zip_archive zip{};
            bool open = false;

            Reader(const std::filesystem::path& bundle, const char* mode, std::string& error) {
                std::error_code code;
                const auto size = std::filesystem::file_size(bundle, code);
                if (code || !std::filesystem::is_regular_file(bundle, code)) {
                    error = "cannot open " + display(bundle);
                    return;
                }
                file = open_file(bundle, mode);
                if (file == nullptr) {
                    error = "cannot open " + display(bundle);
                    return;
                }
                if (!mz_zip_reader_init_cfile(&zip, file, size, 0)) {
                    error = display(bundle) + " is not an asset bundle (.tasset)";
                    return;
                }
                open = true;
            }

            ~Reader() {
                if (open) {
                    if (zip.m_zip_mode == MZ_ZIP_MODE_READING) {
                        mz_zip_reader_end(&zip);
                    } else {
                        mz_zip_writer_end(&zip);
                    }
                }
                if (file != nullptr) {
                    std::fclose(file);
                }
            }

            static std::string display(const std::filesystem::path& path) {
                const std::u8string text = path.u8string();
                return std::string(text.begin(), text.end());
            }

            std::vector<std::string> names() {
                std::vector<std::string> result;
                const mz_uint count = mz_zip_reader_get_num_files(&zip);
                for (mz_uint i = 0; i < count; ++i) {
                    mz_zip_archive_file_stat stat;
                    if (mz_zip_reader_file_stat(&zip, i, &stat) && !stat.m_is_directory) {
                        result.emplace_back(stat.m_filename);
                    }
                }
                return result;
            }

            std::optional<std::string> read(const std::string& name) {
                const int index = mz_zip_reader_locate_file(&zip, name.c_str(), nullptr, 0);
                mz_zip_archive_file_stat stat;
                if (index < 0 || !mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat) || stat.m_is_directory) {
                    return std::nullopt;
                }
                std::string bytes(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
                if (!mz_zip_reader_extract_to_mem(&zip, static_cast<mz_uint>(index), bytes.data(), bytes.size(), 0)) {
                    return std::nullopt;
                }
                return bytes;
            }
        };

        // 묶음 안에서 쓸 이름. 경로 구분자는 '/'이고 앞의 '/'는 뺀다
        std::string entry_name(std::string name) {
            std::replace(name.begin(), name.end(), '\\', '/');
            while (!name.empty() && name.front() == '/') {
                name.erase(name.begin());
            }
            return name.empty() ? "file" : name;
        }

        // logo.png -> logo-2.png
        std::string numbered(const std::string& name, int number) {
            const std::size_t slash = name.find_last_of('/');
            const std::size_t dot = name.find_last_of('.');
            const bool has_extension = dot != std::string::npos && (slash == std::string::npos || dot > slash + 1);
            const std::string stem = has_extension ? name.substr(0, dot) : name;
            const std::string extension = has_extension ? name.substr(dot) : "";
            return stem + "-" + std::to_string(number) + extension;
        }

        bool lower_equals(const std::string& text, const std::string& lower) {
            return text.size() == lower.size() && std::equal(text.begin(), text.end(), lower.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == b;
            });
        }
    }

    std::optional<std::vector<std::string>> list(const std::filesystem::path& bundle, std::string& error) {
        Reader reader(bundle, "rb", error);
        if (!reader.open) {
            return std::nullopt;
        }
        return reader.names();
    }

    std::optional<std::vector<Entry>> entries(const std::filesystem::path& bundle, std::string& error) {
        Reader reader(bundle, "rb", error);
        if (!reader.open) {
            return std::nullopt;
        }
        std::vector<Entry> result;
        const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat;
            if (mz_zip_reader_file_stat(&reader.zip, i, &stat) && !stat.m_is_directory) {
                result.push_back({stat.m_filename, static_cast<std::uint64_t>(stat.m_uncomp_size)});
            }
        }
        return result;
    }

    std::optional<std::string> read(const std::filesystem::path& bundle, const std::string& name) {
        std::string error;
        Reader reader(bundle, "rb", error);
        if (!reader.open) {
            return std::nullopt;
        }
        return reader.read(name);
    }

    std::optional<std::pair<std::filesystem::path, std::string>> split(const std::filesystem::path& path) {
        std::filesystem::path prefix;
        std::string rest;
        bool found = false;
        for (const auto& part : path) {
            if (found) {
                const std::u8string text = part.u8string();
                rest += (rest.empty() ? "" : "/") + std::string(text.begin(), text.end());
                continue;
            }
            prefix /= part;
            const std::u8string extension = part.extension().u8string();
            if (lower_equals(std::string(extension.begin(), extension.end()), ".tasset")) {
                std::error_code code;
                found = std::filesystem::is_regular_file(prefix, code);
            }
        }
        if (!found || rest.empty()) {
            return std::nullopt;
        }
        return std::pair{prefix, rest};
    }

    std::optional<std::string> add(const std::filesystem::path& bundle, const std::string& name, const std::string& bytes, std::string& error) {
        const std::string wanted = entry_name(name);
        std::error_code code;
        if (!std::filesystem::exists(bundle, code)) {
            Writer writer(bundle);
            if (!writer.add(wanted, bytes) || !writer.finish()) {
                error = writer.error();
                return std::nullopt;
            }
            return wanted;
        }
        Reader reader(bundle, "r+b", error);
        if (!reader.open) {
            return std::nullopt;
        }
        const std::vector<std::string> names = reader.names();
        const std::set<std::string> existing(names.begin(), names.end());
        std::string chosen = wanted;
        for (int number = 2; existing.contains(chosen); ++number) {
            if (reader.read(chosen) == bytes) {
                return chosen;
            }
            chosen = numbered(wanted, number);
        }
        // 기존 내용 뒤에 이어 쓴다. 큰 영상이 들어 있어도 묶음 전체를 다시 쓰지 않는다
        if (!mz_zip_writer_init_from_reader_v2(&reader.zip, nullptr, 0)
            || !mz_zip_writer_add_mem(&reader.zip, chosen.c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION)
            || !mz_zip_writer_finalize_archive(&reader.zip)) {
            error = "cannot write " + Reader::display(bundle);
            return std::nullopt;
        }
        return chosen;
    }

    namespace {
        // 묶음을 새로 쓰며 from을 지우거나(to가 비었으면) to로 이름을 바꾼다. 다른 파일은 담긴 내용 그대로 옮긴다
        bool rewrite(const std::filesystem::path& bundle, const std::string& from, const std::string& to, std::string& error) {
            std::filesystem::path temporary = bundle;
            temporary += ".part";
            {
                Reader reader(bundle, "rb", error);
                if (!reader.open) {
                    return false;
                }
                const int index = mz_zip_reader_locate_file(&reader.zip, from.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
                if (index < 0) {
                    error = from + " is not in " + Reader::display(bundle);
                    return false;
                }
                if (!to.empty()) {
                    std::string lower = to;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                    for (const auto& name : reader.names()) {
                        if (name != from && lower_equals(name, lower)) {
                            error = to + " is already in " + Reader::display(bundle);
                            return false;
                        }
                    }
                }
                std::FILE* file = open_file(temporary, "wb");
                if (file == nullptr) {
                    error = "cannot write " + Reader::display(bundle);
                    return false;
                }
                mz_zip_archive zip{};
                bool ok = mz_zip_writer_init_cfile(&zip, file, 0);
                const mz_uint count = mz_zip_reader_get_num_files(&reader.zip);
                for (mz_uint i = 0; ok && i < count; ++i) {
                    if (static_cast<int>(i) != index) {
                        ok = mz_zip_writer_add_from_zip_reader(&zip, &reader.zip, i);
                    } else if (!to.empty()) {
                        const auto bytes = reader.read(from);
                        ok = bytes && mz_zip_writer_add_mem(&zip, to.c_str(), bytes->data(), bytes->size(), MZ_NO_COMPRESSION);
                    }
                }
                ok = ok && mz_zip_writer_finalize_archive(&zip);
                mz_zip_writer_end(&zip);
                ok = std::fclose(file) == 0 && ok;
                if (!ok) {
                    std::error_code code;
                    std::filesystem::remove(temporary, code);
                    error = "cannot write " + Reader::display(bundle);
                    return false;
                }
            }
            // 읽던 묶음을 닫은 뒤에 바꿔 놓는다
            std::error_code code;
            std::filesystem::rename(temporary, bundle, code);
            if (code) {
                std::filesystem::remove(temporary, code);
                error = "cannot write " + Reader::display(bundle);
                return false;
            }
            return true;
        }
    }

    bool remove(const std::filesystem::path& bundle, const std::string& name, std::string& error) {
        return rewrite(bundle, name, "", error);
    }

    bool rename(const std::filesystem::path& bundle, const std::string& from, const std::string& to, std::string& error) {
        const std::string wanted = entry_name(to);
        if (wanted == from) {
            return true;
        }
        return rewrite(bundle, from, wanted, error);
    }

    struct Writer::State {
        std::filesystem::path bundle;
        std::filesystem::path temporary;
        std::FILE* file = nullptr;
        mz_zip_archive zip{};
        bool started = false;
        bool finished = false;
    };

    Writer::Writer(const std::filesystem::path& bundle) : state_(new State) {
        state_->bundle = bundle;
        state_->temporary = bundle;
        state_->temporary += ".part";
        state_->file = open_file(state_->temporary, "wb");
        if (state_->file == nullptr) {
            error_ = "cannot write " + Reader::display(bundle);
            return;
        }
        if (!mz_zip_writer_init_cfile(&state_->zip, state_->file, 0)) {
            error_ = "cannot write " + Reader::display(bundle);
            return;
        }
        state_->started = true;
    }

    Writer::~Writer() {
        if (state_->started && !state_->finished) {
            mz_zip_writer_end(&state_->zip);
        }
        if (state_->file != nullptr) {
            std::fclose(state_->file);
        }
        if (!state_->finished) {
            std::error_code code;
            std::filesystem::remove(state_->temporary, code);
        }
        delete state_;
    }

    bool Writer::add(const std::string& name, const std::string& bytes) {
        if (!ok()) {
            return false;
        }
        if (!mz_zip_writer_add_mem(&state_->zip, entry_name(name).c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION)) {
            error_ = "cannot add " + name + " to " + Reader::display(state_->bundle);
            return false;
        }
        return true;
    }

    bool Writer::finish() {
        if (!ok()) {
            return false;
        }
        const bool written = mz_zip_writer_finalize_archive(&state_->zip) && mz_zip_writer_end(&state_->zip);
        state_->started = false;
        const bool closed = std::fclose(state_->file) == 0;
        state_->file = nullptr;
        if (!written || !closed) {
            error_ = "cannot write " + Reader::display(state_->bundle);
            return false;
        }
        std::error_code code;
        std::filesystem::rename(state_->temporary, state_->bundle, code);
        if (code) {
            error_ = "cannot write " + Reader::display(state_->bundle) + ": " + code.message();
            return false;
        }
        state_->finished = true;
        return true;
    }

    std::optional<std::filesystem::path> extract(const std::filesystem::path& bundle, const std::string& name) {
        std::error_code code;
        const auto absolute = std::filesystem::absolute(bundle, code);
        const auto size = std::filesystem::file_size(bundle, code);
        const auto time = std::filesystem::last_write_time(bundle, code);
        if (code) {
            return std::nullopt;
        }
        std::ostringstream key;
        key << Reader::display(absolute) << '\n' << size << '\n' << static_cast<long long>(time.time_since_epoch().count()) << '\n' << name;
        std::ostringstream folder;
        folder << std::hex << std::hash<std::string>{}(key.str());
        const std::filesystem::path relative = std::filesystem::path(std::u8string(name.begin(), name.end())).filename();
        const std::filesystem::path target = std::filesystem::temp_directory_path(code) / "templide-assets" / folder.str() / relative;
        if (code) {
            return std::nullopt;
        }
        if (std::filesystem::is_regular_file(target, code)) {
            return target;
        }
        const auto bytes = read(bundle, name);
        if (!bytes) {
            return std::nullopt;
        }
        std::filesystem::create_directories(target.parent_path(), code);
        std::filesystem::path temporary = target;
        temporary += ".part";
        {
            std::ofstream output(temporary, std::ios::binary);
            output.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
            if (!output) {
                return std::nullopt;
            }
        }
        std::filesystem::rename(temporary, target, code);
        return code ? std::nullopt : std::optional(target);
    }
}

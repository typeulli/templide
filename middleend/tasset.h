#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/** .tasset 묶음 파일을 읽고 쓰는 기능. 묶음은 zip 형식이다. */
namespace templide::tasset {
    /** 묶음 안의 파일 이름을 반환한다. 실패하면 error에 이유를 기록한다. */
    std::optional<std::vector<std::string>> list(const std::filesystem::path& bundle, std::string& error);

    /** 묶음 안의 파일 이름과 크기(바이트)를 반환한다. */
    struct Entry {
        std::string name;
        std::uint64_t size = 0;
    };
    std::optional<std::vector<Entry>> entries(const std::filesystem::path& bundle, std::string& error);

    /** 묶음 안의 파일을 읽는다. */
    std::optional<std::string> read(const std::filesystem::path& bundle, const std::string& name);

    /** 묶음 내부 경로를 묶음 경로와 내부 이름으로 나눈다. */
    std::optional<std::pair<std::filesystem::path, std::string>> split(const std::filesystem::path& path);

    /** 파일을 묶음에 추가하고 실제 이름을 반환한다. */
    std::optional<std::string> add(const std::filesystem::path& bundle, const std::string& name, const std::string& bytes, std::string& error);

    /** 묶음 안의 파일을 삭제한다. */
    bool remove(const std::filesystem::path& bundle, const std::string& name, std::string& error);

    /** 묶음 안의 파일 이름을 바꾼다. */
    bool rename(const std::filesystem::path& bundle, const std::string& from, const std::string& to, std::string& error);

    /** 새 묶음을 작성한다. 파일을 모두 추가한 뒤 finish()를 호출해야 한다. */
    class Writer {
    public:
        explicit Writer(const std::filesystem::path& bundle);
        ~Writer();
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;

        bool ok() const { return error_.empty(); }
        const std::string& error() const { return error_; }
        /** 같은 이름의 파일을 두 번 추가할 수 없다. */
        bool add(const std::string& name, const std::string& bytes);
        bool finish();

    private:
        struct State;
        State* state_;
        std::string error_;
    };

    /** 묶음 안의 파일을 임시 폴더에 풀고 경로를 반환한다. */
    std::optional<std::filesystem::path> extract(const std::filesystem::path& bundle, const std::string& name);
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// .tasset: 그림, 비디오, 오디오를 담아 두는 묶음 파일. 형식은 zip이고, 그림과 영상은 이미 압축돼 있으므로 압축하지 않고 담는다.
// .tlide는 asset "x.tasset";으로 불러와 asset("이름")으로 쓰거나, file("x.tasset/이름")처럼 경로로 바로 가리킨다
namespace templide::tasset {
    // 묶음 안의 파일 이름들. 열 수 없거나 zip이 아니면 nullopt이고 error에 이유가 있다
    std::optional<std::vector<std::string>> list(const std::filesystem::path& bundle, std::string& error);

    // 묶음 안의 파일 이름과 크기(바이트). 열 수 없으면 nullopt이고 error에 이유가 있다
    struct Entry {
        std::string name;
        std::uint64_t size = 0;
    };
    std::optional<std::vector<Entry>> entries(const std::filesystem::path& bundle, std::string& error);

    // 묶음 안의 파일 하나. 없으면 nullopt
    std::optional<std::string> read(const std::filesystem::path& bundle, const std::string& name);

    // path가 'x.tasset/이름'처럼 묶음 안의 파일을 가리키면 (묶음 경로, 묶음 안의 이름). 묶음은 디스크에 있는 파일이어야 한다
    std::optional<std::pair<std::filesystem::path, std::string>> split(const std::filesystem::path& path);

    // 묶음에 파일을 넣고 넣은 이름을 돌려준다. 묶음이 없으면 만든다.
    // 같은 이름이 있으면 내용이 같을 때 그 이름을 쓰고, 다르면 이름 뒤에 -2, -3을 붙인다. 실패하면 nullopt이고 error에 이유가 있다
    std::optional<std::string> add(const std::filesystem::path& bundle, const std::string& name, const std::string& bytes, std::string& error);

    // 묶음 안의 파일을 지운다. 실패하면 false이고 error에 이유가 있다
    bool remove(const std::filesystem::path& bundle, const std::string& name, std::string& error);

    // 묶음 안의 파일 이름을 바꾼다. 같은 이름(대소문자 무시)이 이미 있으면 실패한다
    bool rename(const std::filesystem::path& bundle, const std::string& from, const std::string& to, std::string& error);

    // 새 묶음을 처음부터 쓴다 (이미 있으면 덮어쓴다). 다 넣은 뒤 finish()를 불러야 파일이 완성된다
    class Writer {
    public:
        explicit Writer(const std::filesystem::path& bundle);
        ~Writer();
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;

        bool ok() const { return error_.empty(); }
        const std::string& error() const { return error_; }
        // 같은 이름을 두 번 넣으면 안 된다
        bool add(const std::string& name, const std::string& bytes);
        bool finish();

    private:
        struct State;
        State* state_;
        std::string error_;
    };

    // 묶음 안의 파일을 임시 폴더에 풀고 그 경로를 돌려준다. 편집기가 비디오와 오디오를 디스크 파일로 재생할 때 쓴다.
    // 같은 묶음(경로, 크기, 수정 시각)과 이름이면 이미 푼 파일을 쓴다
    std::optional<std::filesystem::path> extract(const std::filesystem::path& bundle, const std::string& name);
}

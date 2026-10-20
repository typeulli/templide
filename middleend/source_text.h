#pragma once

#include <optional>
#include <string>

namespace templide::middleend {
    // 디스크에서 읽은 .tlide의 바이트를 UTF-8 글로 바꾼다. 컴파일러가 직접 읽는 인코딩은 UTF-8(BOM은 건너뛴다)과 BOM이 있는 UTF-16(LE, BE)이다.
    // 그 밖의 인코딩이거나 깨진 파일이면 nullopt이고 error에 이유가 있다 (편집기는 다른 인코딩도 열고 UTF-8로 저장할 수 있다)
    std::optional<std::string> decode_source(const std::string& bytes, std::string& error);

    // UTF-8 글을 original(디스크에서 읽은 바이트)과 같은 인코딩으로 바꾼다: UTF-8 BOM이 있었으면 BOM을 붙이고, UTF-16이었으면 같은 BOM의 UTF-16으로 한다
    std::string encode_like(const std::string& original, const std::string& utf8);
}

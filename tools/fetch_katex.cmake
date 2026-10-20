# libs/katex(수식을 그리는 KaTeX)를 만든다. KaTeX는 저장소에 없고(.gitignore) 빌드할 때 CMakeLists.txt가 이 스크립트를 실행한다.
# npm 레지스트리의 katex 패키지를 받아 SHA-256을 확인한 뒤, katex.min.js, katex.min.css, LICENSE, 글꼴(woff2)만 KATEX_DIR에 둔다.
# 이미 같은 버전이 있으면 아무것도 하지 않는다.
#
#   cmake -DKATEX_DIR=<libs/katex 경로> -P tools/fetch_katex.cmake
#
# 버전을 올릴 때는 KATEX_VERSION, KATEX_SHA256과 tools/gen_licenses.py의 KaTeX 버전을 함께 고친다.
# SHA-256은 레지스트리의 .tgz를 받아 구하고, `npm view katex@<버전> dist.integrity`(SHA-512)와 같은 파일인지 확인한다.

set(KATEX_VERSION 0.16.11)
set(KATEX_SHA256 8c5494c5f7d8e8bf73ff3fe64d4bd09a340e4878b7eec9bbd9ef8d8f3e24193f)
set(KATEX_URL https://registry.npmjs.org/katex/-/katex-${KATEX_VERSION}.tgz)

if (NOT KATEX_DIR)
    message(FATAL_ERROR "KATEX_DIR(libs/katex 경로)을 지정해 주세요")
endif ()

# 이 버전의 katex.min.js와 katex.min.css, css가 가리키는 글꼴이 모두 있는지
function(katex_complete result)
    set(${result} FALSE PARENT_SCOPE)
    foreach (name katex.min.js katex.min.css LICENSE)
        if (NOT EXISTS ${KATEX_DIR}/${name})
            return()
        endif ()
    endforeach ()
    file(READ ${KATEX_DIR}/katex.min.js script)
    string(FIND "${script}" "version:\"${KATEX_VERSION}\"" found)
    if (found EQUAL -1)
        return()
    endif ()
    file(READ ${KATEX_DIR}/katex.min.css stylesheet)
    string(REGEX MATCHALL "url\\(fonts/[^)]*\\)" urls "${stylesheet}")
    if (NOT urls)
        return()
    endif ()
    foreach (url IN LISTS urls)
        string(REGEX REPLACE "^url\\(|\\)$" "" path "${url}")
        if (NOT EXISTS ${KATEX_DIR}/${path})
            return()
        endif ()
    endforeach ()
    set(${result} TRUE PARENT_SCOPE)
endfunction ()

katex_complete(complete)
if (complete)
    return()
endif ()

set(work ${KATEX_DIR}.tmp)
file(REMOVE_RECURSE ${work})
file(MAKE_DIRECTORY ${work})

message(STATUS "KaTeX ${KATEX_VERSION}을 받습니다: ${KATEX_URL}")
foreach (attempt RANGE 1 3)
    file(DOWNLOAD ${KATEX_URL} ${work}/katex.tgz EXPECTED_HASH SHA256=${KATEX_SHA256} TLS_VERIFY ON INACTIVITY_TIMEOUT 30 STATUS status)
    list(GET status 0 code)
    if (code EQUAL 0)
        break()
    endif ()
endforeach ()
if (NOT code EQUAL 0)
    file(REMOVE_RECURSE ${work})
    message(FATAL_ERROR "KaTeX를 받지 못했습니다: ${status}\n인터넷에 연결되어 있는지 확인해 주세요 (${KATEX_URL})")
endif ()

file(ARCHIVE_EXTRACT INPUT ${work}/katex.tgz DESTINATION ${work}/extracted
        PATTERNS package/LICENSE package/dist/katex.min.js package/dist/katex.min.css package/dist/fonts/*.woff2)
set(source ${work}/extracted/package)

file(REMOVE_RECURSE ${KATEX_DIR})
file(MAKE_DIRECTORY ${KATEX_DIR}/fonts)
file(COPY ${source}/LICENSE ${source}/dist/katex.min.js DESTINATION ${KATEX_DIR})
file(GLOB fonts ${source}/dist/fonts/*.woff2)
file(COPY ${fonts} DESTINATION ${KATEX_DIR}/fonts)

# 글꼴은 woff2만 쓴다. woff, ttf는 두지 않으므로 css의 src에서도 뺀다
file(READ ${source}/dist/katex.min.css stylesheet)
string(REGEX REPLACE [[,url\(fonts/[^)]*\.woff\) format\("woff"\),url\(fonts/[^)]*\.ttf\) format\("truetype"\)]] "" stylesheet "${stylesheet}")
# file(WRITE)는 Windows에서 줄바꿈을 CRLF로 바꾼다. css는 한 줄이라 끝의 줄바꿈을 빼서 어느 운영체제에서나 같은 파일이 되게 한다
string(STRIP "${stylesheet}" stylesheet)
file(WRITE ${KATEX_DIR}/katex.min.css "${stylesheet}")
file(REMOVE_RECURSE ${work})

katex_complete(complete)
if (NOT complete)
    message(FATAL_ERROR "받은 KaTeX ${KATEX_VERSION}의 구성이 달라 ${KATEX_DIR}을 만들 수 없습니다")
endif ()
message(STATUS "KaTeX ${KATEX_VERSION}: ${KATEX_DIR}")

#pragma once

#include <filesystem>

// templide --serve. 편집기와 stdin/stdout으로 LSP 방식(Content-Length 머리말)의 JSON-RPC를 주고받는다.
//
// LSP: initialize, shutdown, exit, textDocument/didOpen, didChange(문서 전체), didClose, 오류는 textDocument/publishDiagnostics로 보낸다.
// 열린 문서는 저장하지 않았어도 그 내용으로 컴파일한다.
//
// templide/deck {uri, target?}
//   -> {deck, targets, target, width, height, warnings, slides, elements, document}
//   deck은 templide.js가 그리는 JSON이고 element마다 "eid"에 id가 있다. elements는 id -> 출처와 지금 값이다.
//   slides는 slide마다 블록의 출처, 재생 차례대로인 애니메이션, 전환, 속성(background 등), 발표자 메모, 검토 메모이고,
//   document는 고른 target의 title, author, loop와 그 출처다.
//   출처는 {kind: literal | block | locked, uri, range, unit, name, reason, integer, statement}이다. statement면 대입을 지울 수 있다.
// templide/schema {uri} -> object, template, enum, 공통 속성, 전환, 애니메이션 목록 (마지막으로 분석에 성공한 문서의 것)
// templide/edit {uri, target?, edits: [...]}
//   -> {edits: [{uri, range, newText}]} 또는 {error}. 편집기는 받은 수정을 문서에 적용한다
//   {op: "set", id, name, length | number | string | color | enum | bool}  속성 하나를 바꾼다. length는 px
//   {op: "set", id, name, instance: true, length}                      template을 넣은 put의 x, y, width, height
//   {op: "move", id, dx, dy}                                            px만큼 옮긴다
//   {op: "text", id, paragraph, run, text}                             run의 글자를 바꾼다
//   {op: "insert", page, object, properties: [{name, length | ...}]}   slide 끝에 put을 넣는다
//   {op: "style", id, paragraph, run, start?, end?, properties: [...]}   run 일부(UTF-16 위치)를 (style(...) "...")로 감싼다
//   {op: "unstyle", id, paragraph, run}                                run을 감싼 (style(...) "...")를 푼다
//   {op: "order", id, direction: forward | backward | front | back}    그리는 순서(문장 순서)를 바꾼다
//   {op: "slide", action: add | delete | duplicate | up | down, page}  slide 문장. add는 page 뒤에 넣는다 (0이면 맨 앞)
//   {op: "delete", id}                                                 put을 지운다
//   {op: "size", width, height}                                        고른 target의 슬라이드 크기(px)
//   {op: "copy", id, page, dx, dy}                                     put 문장을 page의 slide에 하나 더 넣는다 (x, y는 dx, dy만큼 옮긴다)
//   {op: "paste", page, text}                                          잘라 낸 문장의 원문을 page의 slide에 넣는다
//   {op: "set", id, name, code}                                        값으로 식을 그대로 쓴다 (linear(...), theme.accent1 등)
//   {op: "unset", id, name}                                            속성의 대입을 지워 기본값으로 되돌린다
//   {op: "name", id, name}                                             put(또는 group)에 as 이름을 붙인다
//   {op: "slide_set" | "slide_unset", page, name, 값}                 slide 블록의 속성 (background, hidden, advance_after 등)
//   {op: "layout", page, case}                                         layout = master(...).case의 case를 바꾼다
//   {op: "transition", page, kind?, option?, duration?, remove?}       transition 문장 (duration은 ms)
//   {op: "notes", page, text}                                          comment 문장들을 하나로 바꾼다 (비우면 지운다)
//   {op: "review", page, action: add | update | delete, index?, text, author, x, y}  검토 메모
//   {op: "document_set" | "document_unset", name, 값}                 고른 target의 title, author, loop
//   {op: "animation", page, action: add, id, spec}                     개체의 put 블록에 animate를 넣는다. spec은 {category, effect, option?, path?, start?, duration?, delay?}
//   {op: "animation", page, action: update, index, spec}               index번째(재생 차례) 애니메이션을 바꾼다
//   {op: "animation", page, action: delete, index}                     지운다 (video, audio의 재생이면 start를 when_clicked로)
//   {op: "animation", page, action: move, index, to}                   재생 차례를 옮기고 slide의 모든 애니메이션에 order 1..N을 붙인다
// templide/build {uri, target?} -> {path, errors}  고른 target의 파일을 저장하지 않은 내용으로 만든다
// textDocument/completion                          자동 완성 (server/completion.h)
// textDocument/hover, definition, references, documentHighlight, prepareRename, rename, documentSymbol
//                                                  이름 찾기 (server/navigation.h). 이름 바꾸기는 편집 중인 파일 안의 이름만 된다
namespace templide::server {
    // libs_dir은 html 출력에 넣는 templide.js와 reveal.js가 있는 폴더다
    int serve(const std::filesystem::path& packages_dir, const std::filesystem::path& libs_dir);
}

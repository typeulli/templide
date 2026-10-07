; 설치 화면 배너를 늘릴 때 가로세로 비율을 지킨다. 화면 배율(DPI)과 언어 글꼴에 따라 칸 크기가 달라지기 때문이다
; 배너 이미지는 2배 크기로 넣어 높은 배율에서도 선명하게 한다
!define MUI_WELCOMEFINISHPAGE_BITMAP_STRETCH AspectFitHeight
!define MUI_HEADERIMAGE_BITMAP_STRETCH AspectFitHeight
; 언어 선택 창을 매번 띄운다. 없으면 처음 고른 언어를 레지스트리에 기억해 두고 다음부터 건너뛴다
!define MUI_LANGDLL_ALWAYSSHOW
; NSIS의 한국어 파일에 없는 '설치할 사용자 선택' 페이지 문구. 먼저 정의한 문구를 첫 언어(Korean, tauri.bundle.conf.json의 languages 순서)가 가져가고,
; 다음 언어(English)는 자기 언어 파일의 문구를 쓴다
!define MULTIUSER_TEXT_INSTALLMODE_TITLE "사용자 선택"
!define MULTIUSER_TEXT_INSTALLMODE_SUBTITLE "$(^NameDA)을(를) 설치할 사용자를 선택하세요."
!define MULTIUSER_INNERTEXT_INSTALLMODE_TOP "$(^NameDA)을(를) 나만 쓰도록 설치할지, 이 컴퓨터의 모든 사용자가 쓰도록 설치할지 선택하세요. $(^ClickNext)"
!define MULTIUSER_INNERTEXT_INSTALLMODE_ALLUSERS "이 컴퓨터의 모든 사용자용으로 설치"
!define MULTIUSER_INNERTEXT_INSTALLMODE_CURRENTUSER "나만 쓰도록 설치"

; 탐색기의 '새로 만들기' 메뉴에 .tlide를 넣는다. 메뉴 이름은 파일 연결(tauri.bundle.conf.json)의 설명인 'templide 슬라이드'이고,
; 새 파일은 templates/new.tlide(build.py가 설치 폴더에 넣는다)를 복사해 만든다. 파일 연결 다음에 실행된다
;
; .tlide 파일의 오른쪽 클릭 메뉴에 '내보내기'를 넣는다. 누르면 편집기 대신 target을 골라 만드는 창만 연다 (templide-editor --export).
; 파일 연결의 templide 키 아래에 두므로, 프로그램을 지울 때 tauri가 그 키와 함께 지운다
!macro NSIS_HOOK_POSTINSTALL
  WriteRegStr SHELL_CONTEXT "Software\Classes\.tlide\ShellNew" "FileName" "$INSTDIR\templates\new.tlide"
  ${If} $LANGUAGE == 1042
    WriteRegStr SHELL_CONTEXT "Software\Classes\templide\shell\export" "" "templide 내보내기"
  ${Else}
    WriteRegStr SHELL_CONTEXT "Software\Classes\templide\shell\export" "" "Export with templide"
  ${EndIf}
  WriteRegStr SHELL_CONTEXT "Software\Classes\templide\shell\export" "Icon" "$INSTDIR\${MAINBINARYNAME}.exe,0"
  WriteRegStr SHELL_CONTEXT "Software\Classes\templide\shell\export\command" "" '"$INSTDIR\${MAINBINARYNAME}.exe" --export "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
!macroend

; 프로그램을 지워도 tauri는 .tlide 키를 남기므로 '새로 만들기' 항목은 직접 지운다
!macro NSIS_HOOK_PREUNINSTALL
  DeleteRegKey SHELL_CONTEXT "Software\Classes\.tlide\ShellNew"
!macroend

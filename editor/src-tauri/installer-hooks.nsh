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

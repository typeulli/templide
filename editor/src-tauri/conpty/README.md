# ConPTY

AI 탭의 가상 터미널이 쓰는 ConPTY. Windows 10에 들어 있는 ConPTY는 TUI 프로그램(Codex 등)의 출력을
그대로 넘기지 않고 화면을 다시 그려 보내서 느리고 글자가 섞인다. portable-pty는 실행 파일 옆에
`conpty.dll`이 있으면 그것을 쓰므로, `build.rs`가 이 폴더의 파일을 실행 파일 옆으로 복사한다.

- 출처: NuGet `Microsoft.Windows.Console.ConPTY` 1.25.260930003 (https://github.com/microsoft/terminal)
  - `x64/conpty.dll` ← `runtimes/win-x64/native/conpty.dll`
  - `x64/OpenConsole.exe` ← `build/native/runtimes/x64/OpenConsole.exe`
- 두 파일 모두 Microsoft Corporation의 서명이 있다
- 라이선스: MIT (LICENSE)

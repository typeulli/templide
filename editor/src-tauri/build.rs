fn main() {
    copy_conpty();
    tauri_build::build()
}

// conpty/<arch>의 ConPTY를 실행 파일 옆(target/<profile>)으로 복사한다. portable-pty가 그곳의 conpty.dll을 먼저 쓴다 (conpty/README.md)
fn copy_conpty() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let arch = match std::env::var("CARGO_CFG_TARGET_ARCH").as_deref() {
        Ok("x86_64") => "x64",
        _ => return, // 다른 아키텍처는 아직 넣지 않았다. Windows의 ConPTY를 쓴다
    };
    let source = std::path::Path::new("conpty").join(arch);
    println!("cargo:rerun-if-changed={}", source.display());
    // OUT_DIR은 target/<profile>/build/<패키지>-<해시>/out
    let out = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let Some(target) = out.ancestors().nth(3) else {
        return;
    };
    for name in ["conpty.dll", "OpenConsole.exe"] {
        let from = source.join(name);
        let to = target.join(name);
        if std::fs::read(&from).ok() != std::fs::read(&to).ok() {
            // 편집기가 실행 중이면 파일이 잠겨 있다. 빌드는 계속하고 알리기만 한다
            if let Err(e) = std::fs::copy(&from, &to) {
                println!("cargo:warning=cannot copy {} -> {}: {e}", from.display(), to.display());
            }
        }
    }
}

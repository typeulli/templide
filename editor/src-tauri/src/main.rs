// templide 편집기의 창. 화면은 웹(../src)이고, 여기서는 컴파일러(templide.exe --serve)를 띄워
// LSP 메시지를 화면과 주고받고, 파일을 읽고 쓴다
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::PathBuf;
use std::process::{ChildStdin, Command, Stdio};
use std::sync::Mutex;
use std::time::Instant;
use tauri::{Emitter, Manager, State};

mod mcp;

struct Compiler {
    stdin: Mutex<Option<ChildStdin>>,
}

struct Started(Instant);

// TEMPLIDE_EXE, 설치된 리소스 폴더의 컴파일러, 개발 중이면 저장소의 release 빌드 순서로 찾는다.
// 리소스 폴더는 Windows가 편집기 옆, macOS가 templide.app/Contents/Resources, Linux(deb)가 /usr/lib/templide 이다.
// 컴파일러는 자기 옆의 packages, libs를 쓰므로 셋을 같은 폴더에 설치한다 (build.py)
fn compiler_path(app: &tauri::AppHandle) -> PathBuf {
    const NAME: &str = if cfg!(windows) { "templide.exe" } else { "templide" };
    if let Ok(path) = std::env::var("TEMPLIDE_EXE") {
        return PathBuf::from(path);
    }
    if let Ok(dir) = app.path().resource_dir() {
        let installed = dir.join(NAME);
        if installed.exists() {
            return installed;
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../cmake-build-release").join(NAME)
}

// Content-Length 머리말이 붙은 메시지 하나. 끝나면 None
fn read_message(reader: &mut impl BufRead) -> Option<String> {
    let mut length = None;
    loop {
        let mut line = String::new();
        if reader.read_line(&mut line).ok()? == 0 {
            return None;
        }
        let line = line.trim_end();
        if line.is_empty() {
            if length.is_some() {
                break;
            }
            continue;
        }
        if let Some(value) = line.strip_prefix("Content-Length:") {
            length = value.trim().parse::<usize>().ok();
        }
    }
    let mut body = vec![0; length?];
    reader.read_exact(&mut body).ok()?;
    String::from_utf8(body).ok()
}

#[tauri::command]
fn lsp_send(compiler: State<Compiler>, body: String) -> Result<(), String> {
    let mut stdin = compiler.stdin.lock().map_err(|e| e.to_string())?;
    let stdin = stdin.as_mut().ok_or("the compiler is not running")?;
    write!(stdin, "Content-Length: {}\r\n\r\n{}", body.len(), body).map_err(|e| e.to_string())?;
    stdin.flush().map_err(|e| e.to_string())
}

#[tauri::command]
fn read_text(path: String) -> Result<String, String> {
    std::fs::read_to_string(&path).map_err(|e| format!("{path}: {e}"))
}

#[tauri::command]
fn write_text(path: String, text: String) -> Result<(), String> {
    std::fs::write(&path, text).map_err(|e| format!("{path}: {e}"))
}

// 그림을 문서 폴더의 images 폴더로 복사하고 문서에 적을 상대 경로(images/이름)를 돌려준다.
// 같은 이름의 다른 파일이 있으면 이름 뒤에 -2, -3을 붙이고, 내용이 같은 파일이 있으면 그것을 쓴다
#[tauri::command]
fn import_image(source: String, document: String) -> Result<String, String> {
    import_file(source, document, "images".into())
}

// 파일을 문서 폴더의 folder(images, media, sounds)로 복사하고 문서에 적을 상대 경로(folder/이름)를 돌려준다
#[tauri::command]
fn import_file(source: String, document: String, folder: String) -> Result<String, String> {
    let source = PathBuf::from(source);
    let name = folder.clone();
    let folder = PathBuf::from(&document).parent().ok_or("the document has no folder")?.join(&folder);
    std::fs::create_dir_all(&folder).map_err(|e| format!("{}: {e}", folder.display()))?;
    let bytes = std::fs::read(&source).map_err(|e| format!("{}: {e}", source.display()))?;
    let stem = source.file_stem().and_then(|s| s.to_str()).unwrap_or("image").to_string();
    let extension = source.extension().and_then(|s| s.to_str()).unwrap_or("png").to_lowercase();
    for number in 1.. {
        let file = if number == 1 { format!("{stem}.{extension}") } else { format!("{stem}-{number}.{extension}") };
        let target = folder.join(&file);
        if !target.exists() {
            std::fs::write(&target, &bytes).map_err(|e| format!("{}: {e}", target.display()))?;
            return Ok(format!("{name}/{file}"));
        }
        if std::fs::read(&target).map(|existing| existing == bytes).unwrap_or(false) {
            return Ok(format!("{name}/{file}"));
        }
    }
    unreachable!()
}

// 화면이 만든 그림(data URL의 base64)을 문서 폴더의 folder/이름.png로 저장하고 상대 경로를 돌려준다. 비디오의 표지 그림에 쓴다
#[tauri::command]
fn save_png(document: String, folder: String, stem: String, base64: String) -> Result<String, String> {
    use base64::Engine;
    let bytes = base64::engine::general_purpose::STANDARD.decode(base64.as_bytes()).map_err(|e| e.to_string())?;
    let dir = PathBuf::from(&document).parent().ok_or("the document has no folder")?.join(&folder);
    std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    for number in 1.. {
        let file = if number == 1 { format!("{stem}.png") } else { format!("{stem}-{number}.png") };
        let target = dir.join(&file);
        if !target.exists() || std::fs::read(&target).map(|existing| existing == bytes).unwrap_or(false) {
            std::fs::write(&target, &bytes).map_err(|e| format!("{}: {e}", target.display()))?;
            return Ok(format!("{folder}/{file}"));
        }
    }
    unreachable!()
}

// 그림 파일을 data URL로 (화면에서 크기를 잴 때 쓴다)
#[tauri::command]
fn read_data_url(path: String) -> Result<String, String> {
    use base64::Engine;
    let bytes = std::fs::read(&path).map_err(|e| format!("{path}: {e}"))?;
    let lower = path.to_lowercase();
    let mime = if lower.ends_with(".png") { "image/png" } else if lower.ends_with(".gif") { "image/gif" } else if lower.ends_with(".bmp") { "image/bmp" } else { "image/jpeg" };
    Ok(format!("data:{mime};base64,{}", base64::engine::general_purpose::STANDARD.encode(bytes)))
}

// 만든 파일을 기본 프로그램으로 열거나(reveal이 false), 파일 탐색기에서 그 파일을 보여 준다
#[tauri::command]
fn open_path(path: String, reveal: bool) -> Result<(), String> {
    #[cfg(windows)]
    let mut command = {
        let mut command = Command::new("explorer");
        if reveal {
            command.arg(format!("/select,{path}"));
        } else {
            command.arg(&path);
        }
        command
    };
    #[cfg(target_os = "macos")]
    let mut command = {
        let mut command = Command::new("open");
        if reveal {
            command.arg("-R");
        }
        command.arg(&path);
        command
    };
    #[cfg(all(unix, not(target_os = "macos")))]
    let mut command = {
        let mut command = Command::new("xdg-open");
        command.arg(if reveal { std::path::Path::new(&path).parent().map(|p| p.display().to_string()).unwrap_or(path.clone()) } else { path.clone() });
        command
    };
    command.spawn().map(|_| ()).map_err(|e| format!("{path}: {e}"))
}

// ---- AI 탭의 에이전트. 설치된 프로그램(claude 등)을 고치지 않고 가상 터미널에서 그대로 실행하고,
// 출력은 가공하지 않고 화면의 터미널로 넘긴다. 탭에서 고른 에이전트마다 세션 하나

struct Session {
    run: u64, // 같은 id를 다시 시작해도 이전 실행의 끝 처리가 새 세션을 건드리지 않게
    writer: Box<dyn Write + Send>,
    master: Box<dyn portable_pty::MasterPty + Send>,
    killer: Box<dyn portable_pty::ChildKiller + Send + Sync>,
}

#[derive(Default)]
struct Agents {
    sessions: Mutex<HashMap<String, Session>>,
    runs: std::sync::atomic::AtomicU64,
}

// 실행할 프로그램의 경로. PATH에 있으면 그것을, 없으면 search의 폴더들(%LOCALAPPDATA% 같은 환경 변수를 쓸 수 있다)
// 바로 아래 하위 폴더에서 가장 최근의 program.exe를 쓴다. 앱이 업데이트마다 폴더 이름을 바꾸는 경우(Codex 앱의 bin\<해시>) 때문이다.
// 실행 파일의 이름과 수정 시각만 본다. 에이전트의 로그인 정보 같은 다른 파일은 읽지 않는다
fn locate_program(program: &str, search: &[String]) -> Option<PathBuf> {
    let names: Vec<String> = if cfg!(windows) {
        std::env::var("PATHEXT").unwrap_or(".EXE;.CMD;.BAT".into()).split(';').map(|ext| format!("{program}{}", ext.to_lowercase())).collect()
    } else {
        vec![program.to_string()]
    };
    if let Some(paths) = std::env::var_os("PATH") {
        for dir in std::env::split_paths(&paths) {
            if let Some(found) = names.iter().map(|name| dir.join(name)).find(|file| file.is_file()) {
                return Some(found);
            }
        }
    }
    let exe = if cfg!(windows) { format!("{program}.exe") } else { program.to_string() };
    search.iter()
        .map(|dir| expand_env(dir))
        .filter_map(|dir| std::fs::read_dir(dir).ok())
        .flat_map(|entries| entries.filter_map(|entry| entry.ok()).map(|entry| entry.path().join(&exe)))
        .filter_map(|file| std::fs::metadata(&file).and_then(|meta| meta.modified()).ok().map(|time| (time, file)))
        .max()
        .map(|(_, file)| file)
}

// %NAME%를 환경 변수의 값으로
fn expand_env(text: &str) -> PathBuf {
    let mut out = String::new();
    let mut parts = text.split('%');
    out.push_str(parts.next().unwrap_or(""));
    let mut inside = true;
    for part in parts {
        if inside {
            out.push_str(&std::env::var(part).unwrap_or_default());
        } else {
            out.push_str(part);
        }
        inside = !inside;
    }
    PathBuf::from(out)
}

// program을 cwd(없으면 홈 폴더)에서 실행하고 실행 번호를 돌려준다. 출력은 "agent-output" {run, data}, 끝나면 "agent-exit" {run, code}
#[tauri::command]
fn agent_start(app: tauri::AppHandle, agents: State<Agents>, id: String, program: String, search: Vec<String>, args: Vec<String>, env: HashMap<String, String>, cwd: Option<String>, cols: u16, rows: u16) -> Result<u64, String> {
    use portable_pty::{native_pty_system, CommandBuilder, PtySize};
    agent_stop(agents.clone(), id.clone())?;
    let path = locate_program(&program, &search).ok_or_else(|| format!("{program}: not found"))?;
    let pair = native_pty_system().openpty(PtySize { rows, cols, pixel_width: 0, pixel_height: 0 }).map_err(|e| e.to_string())?;
    let mut command = CommandBuilder::new(&path);
    command.args(&args);
    command.env("TERM", "xterm-256color");
    for (key, value) in &env {
        command.env(key, value);
    }
    match cwd.filter(|dir| std::path::Path::new(dir).is_dir()) {
        Some(dir) => command.cwd(dir),
        None => {
            if let Some(home) = std::env::var_os("USERPROFILE").or_else(|| std::env::var_os("HOME")) {
                command.cwd(home);
            }
        }
    }
    let mut child = pair.slave.spawn_command(command).map_err(|e| format!("{program}: {e}"))?;
    drop(pair.slave);
    let killer = child.clone_killer();
    let mut reader = pair.master.try_clone_reader().map_err(|e| e.to_string())?;
    let writer = pair.master.take_writer().map_err(|e| e.to_string())?;
    let run = agents.runs.fetch_add(1, std::sync::atomic::Ordering::Relaxed) + 1;
    agents.sessions.lock().map_err(|e| e.to_string())?.insert(id.clone(), Session { run, writer, master: pair.master, killer });
    std::thread::spawn(move || {
        // 읽은 조각이 UTF-8 글자 중간에서 끊기면 나머지는 다음 조각과 붙여 보낸다
        let mut buffer = [0u8; 8192];
        let mut pending = Vec::new();
        while let Ok(count) = reader.read(&mut buffer) {
            if count == 0 {
                break;
            }
            pending.extend_from_slice(&buffer[..count]);
            let valid = match std::str::from_utf8(&pending) {
                Ok(_) => pending.len(),
                Err(e) if e.error_len().is_none() => e.valid_up_to(),
                Err(_) => pending.len(),
            };
            let data = String::from_utf8_lossy(&pending[..valid]).into_owned();
            pending.drain(..valid);
            let _ = app.emit("agent-output", serde_json::json!({ "run": run, "data": data }));
        }
        let code = child.wait().ok().map(|status| status.exit_code());
        if let Some(agents) = app.try_state::<Agents>() {
            if let Ok(mut sessions) = agents.sessions.lock() {
                if sessions.get(&id).is_some_and(|session| session.run == run) {
                    sessions.remove(&id);
                }
            }
        }
        let _ = app.emit("agent-exit", serde_json::json!({ "run": run, "code": code }));
    });
    Ok(run)
}

#[tauri::command]
fn agent_write(agents: State<Agents>, id: String, data: String) -> Result<(), String> {
    let mut sessions = agents.sessions.lock().map_err(|e| e.to_string())?;
    let session = sessions.get_mut(&id).ok_or("the agent is not running")?;
    session.writer.write_all(data.as_bytes()).map_err(|e| e.to_string())?;
    session.writer.flush().map_err(|e| e.to_string())
}

#[tauri::command]
fn agent_resize(agents: State<Agents>, id: String, cols: u16, rows: u16) -> Result<(), String> {
    let sessions = agents.sessions.lock().map_err(|e| e.to_string())?;
    if let Some(session) = sessions.get(&id) {
        session.master.resize(portable_pty::PtySize { rows, cols, pixel_width: 0, pixel_height: 0 }).map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[tauri::command]
fn agent_stop(agents: State<Agents>, id: String) -> Result<(), String> {
    let session = agents.sessions.lock().map_err(|e| e.to_string())?.remove(&id);
    if let Some(mut session) = session {
        let _ = session.killer.kill();
    }
    Ok(())
}

// ---- 연 파일 감시. 다른 프로그램(AI 탭의 에이전트 등)이 파일을 고치면 "file-changed" {path}를 보낸다

#[derive(Default)]
struct Watcher(Mutex<Option<notify::RecommendedWatcher>>);

#[tauri::command]
fn watch_file(app: tauri::AppHandle, watcher: State<Watcher>, path: String) -> Result<(), String> {
    use notify::{EventKind, RecursiveMode, Watcher as _};
    let file = PathBuf::from(&path);
    let folder = file.parent().ok_or("the file has no folder")?.to_path_buf();
    // Windows의 경로는 대소문자를 가리지 않는다
    let same = move |other: &std::path::Path| other.to_string_lossy().to_lowercase() == file.to_string_lossy().to_lowercase();
    let mut next = notify::recommended_watcher(move |event: notify::Result<notify::Event>| {
        if let Ok(event) = event {
            if matches!(event.kind, EventKind::Create(_) | EventKind::Modify(_)) && event.paths.iter().any(|each| same(each)) {
                let _ = app.emit("file-changed", path.clone());
            }
        }
    }).map_err(|e| e.to_string())?;
    // 파일을 지우고 새로 쓰는 프로그램도 있어 파일 대신 폴더를 본다
    next.watch(&folder, RecursiveMode::NonRecursive).map_err(|e| e.to_string())?;
    *watcher.0.lock().map_err(|e| e.to_string())? = Some(next);
    Ok(())
}

// 명령줄로 받은 파일
#[tauri::command]
fn startup_file() -> Option<String> {
    std::env::args().nth(1).filter(|arg| !arg.starts_with('-'))
}

// 프로세스를 시작한 뒤 지난 시간(ms). 시작 속도를 재려고 화면이 단계마다 부른다.
// TEMPLIDE_EDITOR_EXIT_AFTER가 그 단계 이름이면 시간을 출력하고 끝낸다
#[tauri::command]
fn mark(app: tauri::AppHandle, started: State<Started>, label: String) -> f64 {
    let elapsed = started.0.elapsed().as_secs_f64() * 1000.0;
    println!("{label}: {elapsed:.1} ms");
    if std::env::var("TEMPLIDE_EDITOR_EXIT_AFTER").ok().as_deref() == Some(label.as_str()) {
        app.exit(0);
    }
    elapsed
}

fn main() {
    let started = Instant::now();
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Started(started))
        .manage(Compiler { stdin: Mutex::new(None) })
        .manage(Agents::default())
        .manage(Watcher::default())
        .setup(|app| {
            app.manage(mcp::start(app.handle().clone())?);
            let compiler = compiler_path(app.handle());
            let mut command = Command::new(&compiler);
            command.arg("--serve").stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::inherit());
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                command.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
            }
            let mut child = command.spawn().map_err(|e| format!("cannot start {}: {e}", compiler.display()))?;
            *app.state::<Compiler>().stdin.lock().unwrap() = child.stdin.take();
            let stdout = child.stdout.take().unwrap();
            let handle = app.handle().clone();
            // 컴파일러의 메시지를 화면에 "lsp" 이벤트로 넘긴다
            std::thread::spawn(move || {
                let mut reader = BufReader::new(stdout);
                while let Some(body) = read_message(&mut reader) {
                    let _ = handle.emit("lsp", body);
                }
                let _ = child.wait();
            });
            // 창은 화면이 그린 뒤에 보여 준다(main.tsx). 화면이 실패해도 창이 숨은 채로 남지 않게 잠시 뒤 보여 준다
            let handle = app.handle().clone();
            std::thread::spawn(move || {
                std::thread::sleep(std::time::Duration::from_secs(3));
                if let Some(window) = handle.get_webview_window("main") {
                    let _ = window.show();
                }
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![lsp_send, read_text, write_text, startup_file, mark, import_image, import_file, save_png, read_data_url, open_path, agent_start, agent_write, agent_resize, agent_stop, watch_file, mcp::mcp_set_tools, mcp::mcp_reply, mcp::mcp_config])
        .run(tauri::generate_context!())
        .expect("error while running templide editor");
}

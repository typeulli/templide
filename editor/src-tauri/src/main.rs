// templide 편집기의 창. 화면은 웹(../src)이고, 여기서는 컴파일러(templide.exe --serve)를 띄워
// LSP 메시지를 화면과 주고받고, 파일을 읽고 쓴다
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::path::PathBuf;
use std::process::{ChildStdin, Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};
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

// AI 탭의 에이전트. 설치된 프로그램(claude 등)을 고치지 않고 가상 터미널에서 그대로 실행하고,
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

// 연 파일 감시. 다른 프로그램(AI 탭의 에이전트 등)이 파일을 고치면 "file-changed" {path}를 보낸다.
// 탭마다 연 파일을 따로 본다. 키는 소문자 경로다

#[derive(Default)]
struct Watcher(Mutex<HashMap<String, notify::RecommendedWatcher>>);

#[tauri::command]
fn watch_file(app: tauri::AppHandle, watcher: State<Watcher>, path: String) -> Result<(), String> {
    use notify::{EventKind, RecursiveMode, Watcher as _};
    let key = path.to_lowercase();
    let mut watchers = watcher.0.lock().map_err(|e| e.to_string())?;
    if watchers.contains_key(&key) {
        return Ok(());
    }
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
    watchers.insert(key, next);
    Ok(())
}

// 탭을 닫으면 그 파일을 그만 본다
#[tauri::command]
fn unwatch_file(watcher: State<Watcher>, path: String) -> Result<(), String> {
    watcher.0.lock().map_err(|e| e.to_string())?.remove(&path.to_lowercase());
    Ok(())
}

// 설치된 폰트. 코드의 font("...") 옆 목록과 서식 막대의 글꼴 메뉴가 쓴다

// 폰트 파일마다 이름 표(name)에서 글꼴 이름(name ID 1)을 읽는다. 한국어 이름이 있으면 그것을, 없으면 영어 이름을 쓴다.
// PowerPoint가 찾는 이름과 같게 하려고 typographic family(16)가 아니라 family(1)를 쓴다.
// 큰 폰트 파일 전체를 읽지 않도록 파일 머리의 표 목록에서 이름 표의 자리만 찾아 읽는다. 처음 한 번 읽고 기억한다
static FONTS: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();

#[tauri::command]
async fn system_fonts() -> Vec<String> {
    tauri::async_runtime::spawn_blocking(|| FONTS.get_or_init(read_fonts).clone()).await.unwrap_or_default()
}

fn font_folders() -> Vec<PathBuf> {
    let mut folders = Vec::new();
    let env = |name: &str| std::env::var_os(name).map(PathBuf::from);
    if cfg!(windows) {
        folders.extend(env("WINDIR").map(|dir| dir.join("Fonts")));
        folders.extend(env("LOCALAPPDATA").map(|dir| dir.join("Microsoft").join("Windows").join("Fonts")));
    } else if cfg!(target_os = "macos") {
        folders.extend(["/System/Library/Fonts", "/Library/Fonts"].map(PathBuf::from));
        folders.extend(env("HOME").map(|dir| dir.join("Library").join("Fonts")));
    } else {
        folders.extend(["/usr/share/fonts", "/usr/local/share/fonts"].map(PathBuf::from));
        folders.extend(env("HOME").map(|dir| dir.join(".local").join("share").join("fonts")));
        folders.extend(env("HOME").map(|dir| dir.join(".fonts")));
    }
    folders
}

fn read_fonts() -> Vec<String> {
    let mut names = std::collections::BTreeSet::new();
    let mut folders = font_folders();
    while let Some(folder) = folders.pop() {
        let Ok(entries) = std::fs::read_dir(&folder) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                folders.push(path);
                continue;
            }
            let extension = path.extension().map(|ext| ext.to_string_lossy().to_lowercase()).unwrap_or_default();
            if matches!(extension.as_str(), "ttf" | "otf" | "ttc" | "otc") {
                names.extend(font_families(&path).unwrap_or_default());
            }
        }
    }
    // @로 시작하는 이름은 세로쓰기용이다
    names.into_iter().filter(|name| !name.starts_with('@')).collect()
}

// 폰트 파일(묶음이면 그 안의 폰트마다)의 글꼴 이름
fn font_families(path: &std::path::Path) -> std::io::Result<Vec<String>> {
    use std::io::{Seek, SeekFrom};
    let mut file = std::fs::File::open(path)?;
    let mut read_at = |offset: u64, length: usize| -> std::io::Result<Vec<u8>> {
        let mut buffer = vec![0; length];
        file.seek(SeekFrom::Start(offset))?;
        file.read_exact(&mut buffer)?;
        Ok(buffer)
    };
    let u16_at = |bytes: &[u8], at: usize| u16::from_be_bytes([bytes[at], bytes[at + 1]]);
    let u32_at = |bytes: &[u8], at: usize| u32::from_be_bytes([bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]]);
    let header = read_at(0, 12)?;
    let offsets: Vec<u64> = if &header[0..4] == b"ttcf" {
        let count = u32_at(&header, 8).min(256) as usize;
        let table = read_at(12, count * 4)?;
        (0..count).map(|i| u32_at(&table, i * 4) as u64).collect()
    } else {
        vec![0]
    };
    let mut families = Vec::new();
    for offset in offsets {
        let directory = read_at(offset, 12)?;
        let tables = u16_at(&directory, 4) as usize;
        let records = read_at(offset + 12, tables * 16)?;
        let Some(record) = (0..tables).map(|i| &records[i * 16..i * 16 + 16]).find(|record| &record[0..4] == b"name") else { continue };
        let data = read_at(u32_at(record, 8) as u64, (u32_at(record, 12) as usize).min(1 << 20))?;
        let Some(table) = ttf_parser::name::Table::parse(&data) else { continue };
        let mut english = None;
        let mut other = None;
        let mut korean = None;
        for name in table.names {
            if name.name_id != ttf_parser::name_id::FAMILY {
                continue;
            }
            let Some(text) = name.to_string() else { continue };
            match (name.platform_id, name.language_id) {
                (ttf_parser::PlatformId::Windows, 0x0412) => korean = Some(text),
                (ttf_parser::PlatformId::Windows, 0x0409) => english = Some(text),
                _ => {
                    other.get_or_insert(text);
                }
            }
        }
        families.extend(korean.or(english).or(other));
    }
    Ok(families)
}

// 명령줄로 받은 파일들 (편집기). 옵션(-로 시작)은 뺀다
fn editor_args() -> Vec<String> {
    std::env::args().skip(1).filter(|arg| !arg.starts_with('-')).collect()
}

// --export <파일>로 실행되면 편집기 대신 그 파일의 내보내기 창만 연다 (탐색기의 'templide 내보내기')
#[tauri::command]
fn export_file() -> Option<String> {
    let mut args = std::env::args().skip(1);
    (args.next().as_deref() == Some("--export")).then(|| args.next()).flatten()
}

// 탐색기의 'templide 불러오기' (--import <pptx>). 편집기 대신 pptx를 골라 .tlide로 바꾸는 창만 연다

// --import 뒤의 파일들. --import로 실행하지 않았으면 None
fn import_args() -> Option<Vec<String>> {
    let mut args = std::env::args().skip(1);
    (args.next().as_deref() == Some("--import")).then(|| args.collect())
}

// 창은 프로세스 하나만 연다. 탐색기에서 파일을 열면 파일마다 프로세스가 뜨므로, 먼저 뜬 프로세스가 창을 열고
// 나머지 프로세스는 자기 파일을 그 프로세스에 보내고 끝난다. 편집기(파일을 탭으로 연다)와 불러오기 창은 따로 뽑는다.
// 받은 파일은 Pending에 넣고 창에 event를 보낸다. 화면은 open_files(import_files)로 가져간다

struct Single {
    lock: &'static str,   // 창을 연 프로세스가 듣는 포트를 적어 두는 파일 (임시 폴더)
    hello: &'static str,  // 다른 프로세스가 보내는 첫 줄. 잠금 파일이 남아 있어 엉뚱한 프로그램에 붙었을 때를 가려낸다
    window: &'static str, // 앞으로 가져올 창
    event: &'static str,  // 받은 파일을 알리는 event
}

static EDITOR: Single = Single { lock: "templide-editor.lock", hello: "templide-editor", window: "main", event: "open-files" };
static IMPORT: Single = Single { lock: "templide-import.lock", hello: "templide-import", window: "import", event: "import-add" };

impl Single {
    fn lock_path(&self) -> PathBuf {
        std::env::temp_dir().join(self.lock)
    }
}

// 화면이 아직 가져가지 않은 파일 경로들. 처음에는 명령줄로 받은 파일이 들어 있다
#[derive(Default)]
struct Pending(Mutex<Vec<String>>);

enum Election {
    Leader(TcpListener), // 이 프로세스가 창을 연다
    Forwarded,           // 파일을 창을 연 프로세스에 보냈다
    Alone,               // 잠금 파일을 쓸 수 없다. 혼자 창을 연다
}

// 잠금 파일을 먼저 만든(create_new) 프로세스가 창을 연다. 이미 있으면 거기 적힌 포트로 파일을 보낸다.
// 보낼 수 없으면 끝나지 않고 남은 잠금 파일이므로 지우고 한 번 더 해 본다
fn elect(single: &Single, files: &[String]) -> Election {
    let lock = single.lock_path();
    for _ in 0..2 {
        match std::fs::OpenOptions::new().write(true).create_new(true).open(&lock) {
            Ok(mut file) => {
                let Ok(listener) = TcpListener::bind("127.0.0.1:0") else {
                    let _ = std::fs::remove_file(&lock);
                    return Election::Alone;
                };
                let port = listener.local_addr().map(|address| address.port()).unwrap_or(0);
                let _ = write!(file, "{port}");
                return Election::Leader(listener);
            }
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
                // 먼저 뜬 프로세스가 포트를 적을 때까지 잠시 기다린다
                let deadline = Instant::now() + Duration::from_secs(3);
                loop {
                    let port = std::fs::read_to_string(&lock).ok().and_then(|text| text.trim().parse::<u16>().ok()).filter(|port| *port > 0);
                    if let Some(port) = port {
                        if send_files(single, port, files) {
                            return Election::Forwarded;
                        }
                        break;
                    }
                    if Instant::now() > deadline {
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(50));
                }
                let _ = std::fs::remove_file(&lock);
            }
            Err(_) => return Election::Alone,
        }
    }
    Election::Alone
}

// 창을 연 프로세스에 파일들을 한 줄에 하나씩 보내고 "ok"를 받는다. 파일이 없으면 창만 앞으로 가져온다
fn send_files(single: &Single, port: u16, files: &[String]) -> bool {
    let address = std::net::SocketAddr::from(([127, 0, 0, 1], port));
    let Ok(mut stream) = TcpStream::connect_timeout(&address, Duration::from_secs(1)) else {
        return false;
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(3)));
    let mut message = format!("{}\n", single.hello);
    for file in files {
        message += &format!("{file}\n");
    }
    if stream.write_all(message.as_bytes()).is_err() || stream.shutdown(std::net::Shutdown::Write).is_err() {
        return false;
    }
    let mut reply = String::new();
    stream.read_to_string(&mut reply).is_ok() && reply.trim() == "ok"
}

// 다른 프로세스가 보낸 파일을 받아 Pending에 넣고 창에 알린 뒤, 창을 앞으로 가져온다
fn accept_files(app: tauri::AppHandle, listener: TcpListener, single: &'static Single) {
    for stream in listener.incoming().flatten() {
        let _ = stream.set_read_timeout(Some(Duration::from_secs(3)));
        let mut reader = BufReader::new(&stream);
        let mut hello = String::new();
        if reader.read_line(&mut hello).is_err() || hello.trim() != single.hello {
            continue;
        }
        let files: Vec<String> = reader.lines().map_while(Result::ok).map(|line| line.trim().to_string()).filter(|line| !line.is_empty()).collect();
        let _ = (&stream).write_all(b"ok\n");
        if !files.is_empty() {
            if let Ok(mut pending) = app.state::<Pending>().0.lock() {
                pending.extend(files.iter().cloned());
            }
            let _ = app.emit_to(single.window, single.event, files);
        }
        if let Some(window) = app.get_webview_window(single.window) {
            let _ = window.unminimize();
            let _ = window.show();
            let _ = window.set_focus();
        }
    }
}

// 아직 가져가지 않은 pptx 경로들을 가져간다. 화면은 처음에 한 번, "import-add"를 받을 때마다 부른다
#[tauri::command]
fn import_files(pending: State<Pending>) -> Vec<String> {
    pending.0.lock().map(|mut pending| std::mem::take(&mut *pending)).unwrap_or_default()
}

// 아직 가져가지 않은, 탭으로 열 파일들을 가져간다. 화면은 처음에 한 번, "open-files"를 받을 때마다 부른다
#[tauri::command]
fn open_files(pending: State<Pending>) -> Vec<String> {
    pending.0.lock().map(|mut pending| std::mem::take(&mut *pending)).unwrap_or_default()
}

// 만든 .tlide를 편집기로 연다. 편집기가 이미 떠 있으면 그 창의 탭으로 열린다
#[tauri::command]
fn open_in_editor(path: String) -> Result<(), String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    Command::new(exe).arg(&path).spawn().map(|_| ()).map_err(|e| format!("{path}: {e}"))
}

#[tauri::command]
fn path_exists(path: String) -> bool {
    std::path::Path::new(&path).exists()
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
    // 편집기나 불러오기 창이 이미 열려 있으면 파일만 보내고 끝난다. 내보내기 창은 파일마다 따로 연다
    let imports = import_args();
    let single: Option<(&'static Single, Vec<String>)> = match &imports {
        Some(files) => Some((&IMPORT, files.clone())),
        None if export_file().is_none() => Some((&EDITOR, editor_args())),
        None => None,
    };
    let mut leader = None;
    if let Some((single, files)) = &single {
        match elect(single, files) {
            Election::Forwarded => return,
            Election::Leader(listener) => leader = Some((*single, listener)),
            Election::Alone => {}
        }
    }
    let leader_lock = leader.as_ref().map(|(single, _)| single.lock_path());
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Started(started))
        .manage(Pending(Mutex::new(single.map(|(_, files)| files).unwrap_or_default())))
        .manage(Compiler { stdin: Mutex::new(None) })
        .manage(Agents::default())
        .manage(Watcher::default())
        .setup(move |app| {
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
            // 편집기 창(main)은 tauri.conf.json에서 만들지 않고(create: false) 여기서 만든다. 내보내기, 불러오기면 그 창만 연다
            let window = if export_file().is_some() {
                tauri::WebviewWindowBuilder::new(app, "export", tauri::WebviewUrl::App("export.html".into()))
                    .title("templide 내보내기")
                    .inner_size(560.0, 480.0)
                    .min_inner_size(420.0, 320.0)
                    .center()
                    .visible(false)
                    .build()?
            } else if imports.is_some() {
                tauri::WebviewWindowBuilder::new(app, "import", tauri::WebviewUrl::App("import.html".into()))
                    .title("templide 불러오기")
                    .inner_size(900.0, 640.0)
                    .min_inner_size(560.0, 420.0)
                    .center()
                    .visible(false)
                    .build()?
            } else {
                tauri::WebviewWindowBuilder::from_config(app.handle(), &app.config().app.windows[0])?.build()?
            };
            if let Some((single, listener)) = leader.take() {
                let handle = app.handle().clone();
                std::thread::spawn(move || accept_files(handle, listener, single));
            }
            // 창은 화면이 그린 뒤에 보여 준다(main.tsx, export.tsx). 화면이 실패해도 창이 숨은 채로 남지 않게 잠시 뒤 보여 준다
            std::thread::spawn(move || {
                std::thread::sleep(std::time::Duration::from_secs(3));
                let _ = window.show();
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![lsp_send, read_text, write_text, open_files, export_file, import_files, open_in_editor, path_exists, mark, read_data_url, open_path, agent_start, agent_write, agent_resize, agent_stop, watch_file, unwatch_file, system_fonts, mcp::mcp_set_tools, mcp::mcp_reply, mcp::mcp_config])
        .build(tauri::generate_context!())
        .expect("error while running templide editor");
    app.run(move |_, event| {
        // 창을 연 프로세스가 끝나면 다음에 뜨는 프로세스가 창을 열 수 있게 잠금 파일을 지운다
        if let (Some(lock), tauri::RunEvent::Exit) = (&leader_lock, &event) {
            let _ = std::fs::remove_file(lock);
        }
    });
}

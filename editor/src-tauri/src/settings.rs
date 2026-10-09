// 편집기 설정. 사용자 설정 폴더(app_config_dir)의 settings.json 하나에 JSON 그대로 둔다.
// 설정의 모양과 기본값은 화면(src/settings.ts)이 정한다. 여기서는 읽고 쓰기만 하고,
// 화면보다 먼저 떠야 하는 컴파일러의 경로(compilerPath)만 직접 읽는다
use serde_json::Value;
use std::path::PathBuf;
use tauri::{AppHandle, Emitter, Manager};

fn file(app: &AppHandle) -> Option<PathBuf> {
    app.path().app_config_dir().ok().map(|dir| dir.join("settings.json"))
}

// 없거나 읽을 수 없으면 빈 객체. 화면이 빠진 값을 기본값으로 채운다
pub fn load(app: &AppHandle) -> Value {
    file(app)
        .and_then(|file| std::fs::read_to_string(file).ok())
        .and_then(|text| serde_json::from_str::<Value>(&text).ok())
        .filter(Value::is_object)
        .unwrap_or_else(|| Value::Object(Default::default()))
}

// 설정에 적힌 컴파일러 경로. 비어 있으면 None (자동)
pub fn compiler_path(app: &AppHandle) -> Option<String> {
    load(app).get("compilerPath").and_then(Value::as_str).map(|path| path.trim().to_string()).filter(|path| !path.is_empty())
}

// 임시 파일에 쓴 뒤 바꿔 넣어, 쓰다가 끊겨도 설정 파일이 깨지지 않게 한다. 열려 있는 모든 창에 "settings-changed"를 보낸다
pub fn save(app: &AppHandle, settings: &Value) -> Result<(), String> {
    let file = file(app).ok_or("the config folder is unknown")?;
    if let Some(dir) = file.parent() {
        std::fs::create_dir_all(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    }
    let temp = file.with_extension("json.tmp");
    let text = serde_json::to_string_pretty(settings).map_err(|e| e.to_string())?;
    std::fs::write(&temp, text).map_err(|e| format!("{}: {e}", temp.display()))?;
    std::fs::rename(&temp, &file).map_err(|e| format!("{}: {e}", file.display()))?;
    let _ = app.emit("settings-changed", settings);
    Ok(())
}

#[tauri::command]
pub fn settings_load(app: AppHandle) -> Value {
    load(&app)
}

// 설정 파일의 경로 (설정 창의 '정보'에서 보여 준다)
#[tauri::command]
pub fn settings_path(app: AppHandle) -> Option<String> {
    file(&app).map(|file| file.display().to_string())
}

// 화면이 보낸 설정을 저장한다. compilerPath는 compiler_use만 바꾸므로 화면이 보낸 값은 버리고 저장돼 있던 값을 그대로 둔다
#[tauri::command]
pub fn settings_save(app: AppHandle, mut settings: Value) -> Result<(), String> {
    let kept = load(&app).get("compilerPath").cloned();
    if let Some(map) = settings.as_object_mut() {
        match kept {
            Some(path) => {
                map.insert("compilerPath".into(), path);
            }
            None => {
                map.remove("compilerPath");
            }
        }
    }
    save(&app, &settings)
}

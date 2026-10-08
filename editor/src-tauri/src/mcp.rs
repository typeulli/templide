// AI 탭의 에이전트가 편집기를 쓰도록 여는 MCP 서버 (Streamable HTTP, 127.0.0.1).
// 다른 프로그램이 부르지 못하게 무작위 토큰을 요구한다. 도구는 화면(src/mcp.ts)에 있어
// tools/call을 "mcp-call" 이벤트로 넘기고 화면이 mcp_reply로 돌려준 결과를 응답한다
use serde_json::{json, Value};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tauri::{AppHandle, Emitter, State};

pub struct Mcp {
    config: String, // 에이전트에 --mcp-config로 넘기는 JSON. 파일로 남기지 않아 토큰이 디스크에 남지 않는다
    token: String,
    tools: Mutex<Value>, // 화면이 mcp_set_tools로 알려 준 도구 목록
    calls: Mutex<HashMap<u64, Sender<Value>>>,
    next: AtomicU64,
}

const INSTRUCTIONS: &str = "Connected to the templide slide editor. The .tlide document open in the editor may have unsaved changes, \
so read it with read_document and change it with edit_document instead of editing the file directly (the user can undo with Ctrl+Z). \
After editing, check the result visually with render_slide.";

fn random_token() -> String {
    use std::hash::{BuildHasher, Hasher};
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    (0..2).map(|i| {
        let mut hasher = std::collections::hash_map::RandomState::new().build_hasher();
        hasher.write_u128(nanos ^ i);
        hasher.write_u32(std::process::id());
        format!("{:016x}", hasher.finish())
    }).collect()
}

pub fn start(app: AppHandle) -> Result<Arc<Mcp>, String> {
    let server = tiny_http::Server::http("127.0.0.1:0").map_err(|e| e.to_string())?;
    let port = server.server_addr().to_ip().ok_or("no port")?.port();
    let token = random_token();
    let config = json!({ "mcpServers": { "templide": {
        "type": "http",
        "url": format!("http://127.0.0.1:{port}/mcp"),
        "headers": { "Authorization": format!("Bearer {token}") },
    } } }).to_string();
    let mcp = Arc::new(Mcp { config, token, tools: Mutex::new(json!([])), calls: Mutex::new(HashMap::new()), next: AtomicU64::new(1) });
    let shared = mcp.clone();
    std::thread::spawn(move || {
        for request in server.incoming_requests() {
            let mcp = shared.clone();
            let app = app.clone();
            // 도구는 화면의 답을 기다리므로 요청마다 따로 처리한다
            std::thread::spawn(move || handle(&app, &mcp, request));
        }
    });
    Ok(mcp)
}

fn respond(request: tiny_http::Request, status: u16, body: Option<Value>) {
    let response = match body {
        Some(body) => tiny_http::Response::from_string(body.to_string())
            .with_header("Content-Type: application/json".parse::<tiny_http::Header>().unwrap()),
        None => tiny_http::Response::from_string(String::new()),
    };
    let _ = request.respond(response.with_status_code(status));
}

fn handle(app: &AppHandle, mcp: &Mcp, mut request: tiny_http::Request) {
    let authorized = request.headers().iter().any(|header| {
        header.field.equiv("Authorization") && header.value.as_str() == format!("Bearer {}", mcp.token)
    });
    // /mcp/<탭>이면 그 탭의 문서에서, /mcp면 보이는 탭의 문서에서 도구를 실행한다
    let Some(session) = request.url().strip_prefix("/mcp").filter(|rest| rest.is_empty() || rest.starts_with('/')).map(|rest| rest.trim_start_matches('/').to_string()) else {
        return respond(request, 404, None);
    };
    if !authorized {
        return respond(request, 401, None);
    }
    if *request.method() != tiny_http::Method::Post {
        return respond(request, 405, None); // 서버가 먼저 보내는 SSE 스트림은 없다
    }
    let mut text = String::new();
    if request.as_reader().read_to_string(&mut text).is_err() {
        return respond(request, 400, None);
    }
    let Ok(message) = serde_json::from_str::<Value>(&text) else {
        return respond(request, 400, Some(json!({ "jsonrpc": "2.0", "id": null, "error": { "code": -32700, "message": "parse error" } })));
    };
    let Some(id) = message.get("id").cloned() else {
        return respond(request, 202, None); // 알림
    };
    let params = message.get("params").cloned().unwrap_or(Value::Null);
    let result = match message.get("method").and_then(Value::as_str).unwrap_or("") {
        "initialize" => Ok(json!({
            "protocolVersion": params.get("protocolVersion").cloned().unwrap_or(json!("2025-06-18")),
            "capabilities": { "tools": {} },
            "serverInfo": { "name": "templide", "version": env!("CARGO_PKG_VERSION") },
            "instructions": INSTRUCTIONS,
        })),
        "ping" => Ok(json!({})),
        "tools/list" => Ok(json!({ "tools": mcp.tools.lock().map(|tools| tools.clone()).unwrap_or(json!([])) })),
        "tools/call" => Ok(call(app, mcp, &session, params)),
        method => Err(json!({ "code": -32601, "message": format!("unknown method {method}") })),
    };
    let body = match result {
        Ok(result) => json!({ "jsonrpc": "2.0", "id": id, "result": result }),
        Err(error) => json!({ "jsonrpc": "2.0", "id": id, "error": error }),
    };
    respond(request, 200, Some(body));
}

// 화면에 도구 실행을 맡기고 답을 기다린다
fn call(app: &AppHandle, mcp: &Mcp, session: &str, params: Value) -> Value {
    let failed = |message: &str| json!({ "content": [{ "type": "text", "text": message }], "isError": true });
    let call = mcp.next.fetch_add(1, Ordering::Relaxed);
    let (sender, receiver) = channel();
    if let Ok(mut calls) = mcp.calls.lock() {
        calls.insert(call, sender);
    }
    let sent = app.emit("mcp-call", json!({
        "call": call,
        "session": session,
        "name": params.get("name").cloned().unwrap_or(Value::Null),
        "arguments": params.get("arguments").cloned().unwrap_or(json!({})),
    }));
    let result = match sent {
        Ok(()) => receiver.recv_timeout(Duration::from_secs(120)).unwrap_or_else(|_| failed("The editor did not respond")),
        Err(e) => failed(&e.to_string()),
    };
    if let Ok(mut calls) = mcp.calls.lock() {
        calls.remove(&call);
    }
    result
}

#[tauri::command]
pub fn mcp_set_tools(mcp: State<Arc<Mcp>>, tools: Value) -> Result<(), String> {
    *mcp.tools.lock().map_err(|e| e.to_string())? = tools;
    Ok(())
}

#[tauri::command]
pub fn mcp_reply(mcp: State<Arc<Mcp>>, call: u64, result: Value) -> Result<(), String> {
    if let Some(sender) = mcp.calls.lock().map_err(|e| e.to_string())?.remove(&call) {
        let _ = sender.send(result);
    }
    Ok(())
}

// 에이전트를 실행할 때 --mcp-config로 넘기는 JSON
#[tauri::command]
pub fn mcp_config(mcp: State<Arc<Mcp>>) -> String {
    mcp.config.clone()
}

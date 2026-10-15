// 텍스트 파일의 인코딩. VS Code처럼 파일은 바이트로 읽어 인코딩을 정한 뒤 글로 바꾸고, 저장할 때 같은 인코딩과 BOM으로 되돌린다.
// 화면과 컴파일러에는 항상 UTF-8 글만 간다. 인코딩 이름은 웹 표준(encoding_rs)의 이름이다 (UTF-8, UTF-16LE, EUC-KR, Shift_JIS ...)
use chardetng::{EncodingDetector, Iso2022JpDetection, Utf8Detection};
use encoding_rs::{Encoding, ISO_2022_JP, REPLACEMENT, UTF_16BE, UTF_16LE, UTF_8, X_USER_DEFINED};
use serde_json::{json, Value};

struct Decoded {
    text: String,
    encoding: &'static Encoding,
    bom: bool,     // 파일 맨 앞에 BOM이 있었다
    guessed: bool, // BOM도 고른 인코딩도 아니고, 설정의 기본 인코딩과 다르게 추측해 정했다
    lossy: bool,   // 이 인코딩으로 읽을 수 없는 바이트가 있어 깨진 글자(U+FFFD)로 바꿨다
}

// 이름이 가리키는 인코딩. 읽을 수는 있어도 저장할 수 없는 것(ISO-2022-JP 등)은 목록에서 뺀다
fn resolve(name: &str) -> Option<&'static Encoding> {
    Encoding::for_label(name.trim().as_bytes()).filter(|encoding| *encoding != ISO_2022_JP && *encoding != REPLACEMENT && *encoding != X_USER_DEFINED)
}

// 맨 앞의 BOM이 가리키는 인코딩과 BOM의 바이트 수
fn bom_of(bytes: &[u8]) -> Option<(&'static Encoding, usize)> {
    if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
        Some((UTF_8, 3))
    } else if bytes.starts_with(&[0xFF, 0xFE]) {
        Some((UTF_16LE, 2))
    } else if bytes.starts_with(&[0xFE, 0xFF]) {
        Some((UTF_16BE, 2))
    } else {
        None
    }
}

// BOM이 없을 때 자동 추측: 영문뿐이면 기본 인코딩, 올바른 UTF-8이면 UTF-8, 아니면 chardetng가 고른 옛 인코딩
fn guess(bytes: &[u8], default: &'static Encoding) -> &'static Encoding {
    if bytes.is_ascii() {
        return default;
    }
    if std::str::from_utf8(bytes).is_ok() {
        return UTF_8;
    }
    let mut detector = EncodingDetector::new(Iso2022JpDetection::Deny);
    detector.feed(bytes, true);
    detector.guess(None, Utf8Detection::Deny)
}

// 인코딩을 정하는 순서: 고른 인코딩, BOM, (자동 추측이 켜져 있으면) 추측, 설정의 기본 인코딩
fn decode(bytes: &[u8], forced: Option<&'static Encoding>, default: &'static Encoding, auto_guess: bool) -> Decoded {
    let bom = bom_of(bytes);
    let (encoding, guessed) = match (forced, bom) {
        (Some(encoding), _) => (encoding, false),
        (None, Some((encoding, _))) => (encoding, false),
        (None, None) if auto_guess => {
            let encoding = guess(bytes, default);
            (encoding, encoding != default)
        }
        (None, None) => (default, false),
    };
    // 고른 인코딩의 BOM만 벗긴다 (EUC-KR로 연 파일의 맨 앞 바이트는 BOM이 아니라 글자다)
    let (body, has_bom) = match bom {
        Some((owner, length)) if owner == encoding => (&bytes[length..], true),
        _ => (bytes, false),
    };
    let (text, lossy) = encoding.decode_without_bom_handling(body);
    Decoded { text: text.into_owned(), encoding, bom: has_bom, guessed, lossy }
}

// 글을 파일에 쓸 바이트로. 그 인코딩으로 쓸 수 없는 글자가 있으면 (글자, 줄, 칸) 목록과 개수를 돌려준다. 줄과 칸은 1부터이고 칸은 UTF-16 단위다
fn encode(text: &str, encoding: &'static Encoding, bom: bool) -> Result<Vec<u8>, (Vec<(char, usize, usize)>, usize)> {
    if encoding == UTF_8 {
        let mut bytes = Vec::with_capacity(text.len() + 3);
        if bom {
            bytes.extend_from_slice(&[0xEF, 0xBB, 0xBF]);
        }
        bytes.extend_from_slice(text.as_bytes());
        return Ok(bytes);
    }
    if encoding == UTF_16LE || encoding == UTF_16BE {
        // 웹 표준에는 UTF-16 인코더가 없다. BOM은 항상 쓴다 (VS Code도 그렇다)
        let little = encoding == UTF_16LE;
        let mut bytes = Vec::with_capacity(text.len() * 2 + 2);
        bytes.extend_from_slice(if little { &[0xFF, 0xFE] } else { &[0xFE, 0xFF] });
        for unit in text.encode_utf16() {
            bytes.extend_from_slice(&if little { unit.to_le_bytes() } else { unit.to_be_bytes() });
        }
        return Ok(bytes);
    }
    let (bytes, _, unmappable) = encoding.encode(text);
    if !unmappable {
        return Ok(bytes.into_owned());
    }
    // 못 쓰는 글자는 조용히 &#128512; 같은 글자로 바뀌어 쓰이므로 막는다. 어느 글자인지 찾는다
    let mut found = Vec::new();
    let mut count = 0;
    let (mut line, mut column) = (1, 1);
    let mut one = [0u8; 4];
    for character in text.chars() {
        if character == '\n' {
            line += 1;
            column = 1;
            continue;
        }
        if !character.is_ascii() && encoding.encode(character.encode_utf8(&mut one)).2 {
            count += 1;
            if found.len() < 8 {
                found.push((character, line, column));
            }
        }
        column += character.len_utf16();
    }
    Err((found, count))
}

#[tauri::command]
pub fn read_text(path: String, encoding: Option<String>, default_encoding: Option<String>, auto_guess: Option<bool>) -> Result<Value, String> {
    let bytes = std::fs::read(&path).map_err(|e| format!("{path}: {e}"))?;
    let forced = encoding.as_deref().and_then(resolve);
    let default = default_encoding.as_deref().and_then(resolve).unwrap_or(UTF_8);
    let decoded = decode(&bytes, forced, default, auto_guess.unwrap_or(false));
    Ok(json!({
        "text": decoded.text,
        "encoding": decoded.encoding.name(),
        "bom": decoded.bom,
        "guessed": decoded.guessed,
        "lossy": decoded.lossy,
    }))
}

// 저장한다. 그 인코딩으로 쓸 수 없는 글자가 있으면 파일은 건드리지 않고 {saved: false, unmappable: [{char, line, column}], total}을 돌려준다
#[tauri::command]
pub fn write_text(path: String, text: String, encoding: String, bom: bool) -> Result<Value, String> {
    let target = resolve(&encoding).ok_or_else(|| format!("unknown encoding: {encoding}"))?;
    match encode(&text, target, bom) {
        Ok(bytes) => {
            std::fs::write(&path, bytes).map_err(|e| format!("{path}: {e}"))?;
            Ok(json!({ "saved": true }))
        }
        Err((found, total)) => Ok(json!({
            "saved": false,
            "unmappable": found.iter().map(|(character, line, column)| json!({ "char": character.to_string(), "line": line, "column": column })).collect::<Vec<_>>(),
            "total": total,
        })),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const KOREAN: &str = "안녕하세요, templide\n두 번째 줄";

    fn enc(name: &str) -> &'static Encoding {
        resolve(name).unwrap()
    }

    #[test]
    fn utf8_with_and_without_bom() {
        let plain = decode(KOREAN.as_bytes(), None, UTF_8, false);
        assert_eq!((plain.text.as_str(), plain.bom, plain.lossy), (KOREAN, false, false));
        let mut bytes = vec![0xEF, 0xBB, 0xBF];
        bytes.extend_from_slice(KOREAN.as_bytes());
        let with_bom = decode(&bytes, None, UTF_8, false);
        assert_eq!((with_bom.text.as_str(), with_bom.bom, with_bom.encoding), (KOREAN, true, UTF_8));
        assert_eq!(encode(&with_bom.text, UTF_8, true).unwrap(), bytes);
    }

    #[test]
    fn utf16_round_trip() {
        for name in ["UTF-16LE", "UTF-16BE"] {
            let bytes = encode(KOREAN, enc(name), true).unwrap();
            let back = decode(&bytes, None, UTF_8, false);
            assert_eq!((back.text.as_str(), back.encoding.name(), back.bom), (KOREAN, name, true));
        }
        // 짝을 이루는 서로게이트(이모지)
        let bytes = encode("a😀b", UTF_16LE, true).unwrap();
        assert_eq!(decode(&bytes, None, UTF_8, false).text, "a😀b");
    }

    #[test]
    fn legacy_encodings_round_trip() {
        for (name, text) in [("EUC-KR", KOREAN), ("Shift_JIS", "こんにちは\n日本語"), ("GBK", "你好,世界"), ("Big5", "你好,世界"), ("windows-1252", "café €"), ("windows-1251", "Привет")] {
            let bytes = encode(text, enc(name), false).unwrap();
            assert!(std::str::from_utf8(&bytes).is_err() || text.is_ascii(), "{name} should not be UTF-8");
            let back = decode(&bytes, Some(enc(name)), UTF_8, false);
            assert_eq!((back.text.as_str(), back.lossy), (text, false), "{name}");
        }
    }

    #[test]
    fn forced_encoding_wins_and_default_applies_without_bom() {
        let euc_kr = encode(KOREAN, enc("EUC-KR"), false).unwrap();
        // 기본 UTF-8로 열면 깨진 글자가 되고 lossy로 알린다
        let as_utf8 = decode(&euc_kr, None, UTF_8, false);
        assert!(as_utf8.lossy && as_utf8.text.contains('\u{FFFD}'));
        // 기본 인코딩을 EUC-KR로 하거나 EUC-KR로 고르면 읽힌다
        assert_eq!(decode(&euc_kr, None, enc("EUC-KR"), false).text, KOREAN);
        assert_eq!(decode(&euc_kr, Some(enc("EUC-KR")), UTF_8, false).text, KOREAN);
        // 고른 인코딩이 BOM보다 먼저다. 이때 UTF-8 BOM은 글자로 남는다
        let mut with_bom = vec![0xEF, 0xBB, 0xBF];
        with_bom.extend_from_slice(KOREAN.as_bytes());
        let forced = decode(&with_bom, Some(enc("EUC-KR")), UTF_8, false);
        assert!(!forced.bom && forced.text != KOREAN);
    }

    #[test]
    fn auto_guess() {
        let euc_kr = encode("안녕하세요. 발표 자료를 만들어 봅시다. 한글 문서입니다.", enc("EUC-KR"), false).unwrap();
        let guessed = decode(&euc_kr, None, UTF_8, true);
        assert_eq!((guessed.encoding.name(), guessed.guessed, guessed.lossy), ("EUC-KR", true, false));
        assert_eq!(guessed.text, "안녕하세요. 발표 자료를 만들어 봅시다. 한글 문서입니다.");
        // 올바른 UTF-8이면 그대로 UTF-8이고 추측한 것이 아니다
        let utf8 = decode(KOREAN.as_bytes(), None, UTF_8, true);
        assert_eq!((utf8.encoding, utf8.guessed), (UTF_8, false));
        // 영문뿐이면 기본 인코딩을 그대로 쓴다
        let ascii = decode(b"slide { }", None, enc("EUC-KR"), true);
        assert_eq!((ascii.encoding.name(), ascii.guessed), ("EUC-KR", false));
    }

    #[test]
    fn unmappable_characters_block_the_write() {
        let (found, total) = encode("가나\n다😀라 😀", enc("EUC-KR"), false).unwrap_err();
        assert_eq!(total, 2);
        assert_eq!(found[0], ('😀', 2, 2)); // 둘째 줄 "다😀라"에서 😀는 2번째 칸
        assert_eq!(found[1], ('😀', 2, 6)); // 😀는 UTF-16으로 두 칸이므로 "라" 다음의 공백 뒤
        // UTF-8과 UTF-16은 모든 글자를 쓸 수 있다
        assert!(encode("😀", UTF_8, false).is_ok() && encode("😀", UTF_16BE, true).is_ok());
    }

    // 화면(src/encodings.ts)의 목록에 있는 이름은 모두 읽고 쓸 수 있고, 이름이 그대로 돌아와야 한다
    #[test]
    fn every_name_in_the_editor_list_works() {
        let source = include_str!("../../src/encodings.ts");
        let names: Vec<&str> = source
            .lines()
            .filter_map(|line| line.strip_prefix("    '")?.split_once("': () =>").map(|(name, _)| name))
            .collect();
        assert!(names.len() > 30, "found only {} names", names.len());
        for name in names {
            let encoding = resolve(name).unwrap_or_else(|| panic!("{name} is not a known encoding"));
            assert_eq!(encoding.name(), name);
            let bytes = encode("abc 123", encoding, false).unwrap();
            assert_eq!(decode(&bytes, Some(encoding), UTF_8, false).text, "abc 123", "{name}");
        }
    }

    #[test]
    fn names_that_cannot_be_written_are_rejected() {
        for name in ["iso-2022-jp", "replacement", "x-user-defined", "nonsense"] {
            assert!(resolve(name).is_none(), "{name}");
        }
        for name in ["utf-8", "UTF-16LE", "euc-kr", "ks_c_5601-1987", "shift_jis", "windows-1252"] {
            assert!(resolve(name).is_some(), "{name}");
        }
    }
}

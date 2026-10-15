// 텍스트 파일을 읽고 쓴다. 인코딩을 정하는 일은 Rust(src-tauri/src/encoding.rs)가 하고, 여기서는 설정(기본 인코딩, 자동 추측)을 실어 부른다.
// 읽은 글에는 BOM이 없다. BOM은 파일의 성질이라 bom으로 따로 알리고, 저장할 때 다시 붙인다
import { invoke } from '@tauri-apps/api/core';
import { getSettings } from './settings';

export type TextFile = {
    text: string;
    encoding: string;  // 웹 표준 이름 (UTF-8, UTF-16LE, EUC-KR ...)
    bom: boolean;      // 파일 맨 앞에 BOM이 있었다
    guessed: boolean;  // 기본 인코딩과 다르게 추측해서 정했다
    lossy: boolean;    // 이 인코딩으로 읽을 수 없는 바이트가 있어 깨진 글자(�)가 되었다
};

export type Unmappable = { char: string; line: number; column: number };

export type WriteResult = { saved: true } | { saved: false; unmappable: Unmappable[]; total: number };

// encoding을 주면 BOM과 추측보다 그 인코딩을 먼저 쓴다 (인코딩으로 다시 열기, 밖에서 바뀐 파일 다시 읽기)
export function readTextFile(path: string, encoding?: string): Promise<TextFile> {
    const { defaultEncoding, autoGuessEncoding } = getSettings().general;
    return invoke<TextFile>('read_text', { path, encoding, defaultEncoding, autoGuess: autoGuessEncoding });
}

// 그 인코딩으로 쓸 수 없는 글자가 있으면 파일은 그대로 두고 어느 글자인지 돌려준다
export function writeTextFile(path: string, text: string, encoding: string, bom: boolean): Promise<WriteResult> {
    return invoke<WriteResult>('write_text', { path, text, encoding, bom });
}

// 에셋 패널(아래 패널의 탭). 문서 폴더의 그림, 비디오, 오디오와 그 폴더의 .tasset 묶음 안의 파일을 보여 주고,
// 누르면 지금 슬라이드에 넣는다. 문서가 쓰는 것과 안 쓰는 것을 표시한다 (컴파일러의 templide/assets)
import { useEffect, useRef, useState, type ReactNode } from 'react';
import { convertFileSrc } from '@tauri-apps/api/core';
import { Film, Image as ImageIcon, Music, Package, RefreshCw } from 'lucide-react';
import { lsp, type AssetKind, type AssetListing, type DeckResult } from './lsp';
import { IconButton } from './Panels';
import { t, useLang } from './i18n';

// 넣을 파일. code는 data에 적을 식(file("경로")), src는 그림의 크기를 재는 데 쓰는 주소다
export type AssetPick = { kind: AssetKind; code: string; src?: string };

const quote = (text: string) => '"' + text.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

const icons = { image: ImageIcon, video: Film, audio: Music };

const maxThumbnails = 60;

export function AssetLibrary({ uri, deck, active, onInsert }: { uri: string | null; deck: DeckResult | null; active: boolean; onInsert(pick: AssetPick): void }) {
    useLang();
    const [listing, setListing] = useState<AssetListing | null>(null);
    const [unusedOnly, setUnusedOnly] = useState(false);
    const [thumbs, setThumbs] = useState<Record<string, string>>({});
    const requested = useRef(new Set<string>());
    const [reload, setReload] = useState(0);

    // 탭이 보일 때 읽고, 문서가 다시 컴파일될 때(쓰는 것이 바뀐다)와 새로 고침 단추를 누를 때 다시 읽는다
    useEffect(() => {
        if (!active || !uri) {
            return;
        }
        let stale = false;
        const timer = window.setTimeout(() => {
            lsp.request<AssetListing>('templide/assets', { uri }).then((result) => {
                if (!stale) {
                    setListing(result);
                }
            }).catch(() => undefined);
        }, 300);
        return () => {
            stale = true;
            window.clearTimeout(timer);
        };
    }, [active, uri, deck, reload]);

    // 그림은 미리 보기를 만든다. 묶음 안의 그림은 임시 폴더에 풀어서 쓴다 (많으면 앞의 일부만)
    useEffect(() => {
        if (!listing) {
            return;
        }
        let count = 0;
        for (const file of listing.files) {
            if (file.kind === 'image' && count++ < maxThumbnails) {
                const key = file.path;
                if (!requested.current.has(key)) {
                    requested.current.add(key);
                    setThumbs((old) => ({ ...old, [key]: convertFileSrc(listing.folder + '/' + file.path) }));
                }
            }
        }
        for (const bundle of listing.bundles) {
            for (const entry of bundle.entries) {
                const key = bundle.path + '/' + entry.name;
                if (entry.kind === 'image' && count++ < maxThumbnails && !requested.current.has(key)) {
                    requested.current.add(key);
                    lsp.request<{ path?: string }>('templide/asset_extract', { bundle: listing.folder + '/' + bundle.path, entry: entry.name }).then((result) => {
                        if (result.path) {
                            setThumbs((old) => ({ ...old, [key]: convertFileSrc(result.path!) }));
                        }
                    }).catch(() => undefined);
                }
            }
        }
    }, [listing]);

    const total = listing ? listing.files.length + listing.bundles.reduce((sum, bundle) => sum + bundle.entries.length, 0) : 0;
    const row = (key: string, name: string, kind: AssetKind, used: boolean, code: string): ReactNode => {
        if (unusedOnly && used) {
            return null;
        }
        const Icon = icons[kind];
        const src = thumbs[key];
        return (
            <div key={key} className="asset-tile" title={t('{0}\n눌러서 지금 슬라이드에 넣기', name)} onClick={() => onInsert({ kind, code, src })}>
                <span className="asset-tile-thumb">{src ? <img src={src} alt="" loading="lazy" /> : <Icon size={20} />}</span>
                <span className="asset-tile-name">
                    <span className={'asset-tile-used ' + (used ? 'used' : 'unused')} title={used ? t('이 문서가 씁니다') : t('이 문서가 쓰지 않습니다')} />
                    <span className="asset-tile-text">{name}</span>
                </span>
            </div>
        );
    };
    return (
        <div className="asset-library">
            <div className="asset-bar">
                <span className="asset-count">{listing ? t('에셋') + ` (${total})` : t('에셋')}</span>
                <span className="asset-tools">
                    <label className="asset-unused" title={t('이 문서가 쓰지 않는 것만 보기')}>
                        <input type="checkbox" checked={unusedOnly} onChange={(event) => setUnusedOnly(event.target.checked)} />{t('안 쓰는 것')}
                    </label>
                    <IconButton icon={RefreshCw} title={t('다시 읽기')} onClick={() => setReload(reload + 1)} />
                </span>
            </div>
            <div className="asset-items">
                {!listing && <div className="muted small asset-empty">{t('읽는 중')}</div>}
                {listing && total === 0 && <div className="muted small asset-empty">{t('문서 폴더에 그림, 비디오, 오디오가 없습니다')}</div>}
                {listing && listing.files.length > 0 && (
                    <div className="asset-grid">
                        {listing.files.map((file) => row(file.path, file.path, file.kind, file.used, `file(${quote(file.path)})`))}
                    </div>
                )}
                {listing?.bundles.map((bundle) => bundle.entries.length === 0 ? null : (
                    <div key={bundle.path}>
                        <div className="asset-bundle"><Package size={12} /> {bundle.path}</div>
                        <div className="asset-grid">
                            {bundle.entries.map((entry) => row(bundle.path + '/' + entry.name, entry.name, entry.kind, entry.used, `file(${quote(bundle.path + '/' + entry.name)})`))}
                        </div>
                    </div>
                ))}
                {listing?.truncated && <div className="muted small asset-empty">{t('파일이 많아 일부만 보입니다')}</div>}
            </div>
        </div>
    );
}

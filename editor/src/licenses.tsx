// 오픈소스 라이선스 창. tools/gen_licenses.py가 만든 목록을 보여 주고, 누르면 라이선스 본문을 펼친다
import { useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { invoke } from '@tauri-apps/api/core';
import { ChevronDown, ChevronRight, Search } from 'lucide-react';
import data from './generated/licenses.json';
import './licenses.css';

type Package = { name: string; version: string; license: string; url: string; texts: string[] };
type Section = { title: string; packages: Package[] };
const sections = data.sections as Section[];
const texts = data.texts as Record<string, string>;

// 사이트는 기본 브라우저로 연다. 창 안에서 이동하지 않게 기본 동작은 막는다
const openSite = (event: React.MouseEvent, url: string) => {
    event.preventDefault();
    if (/^https?:\/\//.test(url)) {
        invoke('open_path', { path: url, reveal: false });
    }
};

function Licenses() {
    const [query, setQuery] = useState('');
    const [open, setOpen] = useState<Set<string>>(new Set());
    const shown = useMemo(() => {
        const words = query.trim().toLowerCase();
        return sections.map((section) => ({
            ...section,
            packages: words ? section.packages.filter((p) => `${p.name} ${p.license}`.toLowerCase().includes(words)) : section.packages,
        })).filter((section) => section.packages.length > 0);
    }, [query]);
    const total = sections.reduce((sum, section) => sum + section.packages.length, 0);
    const toggle = (key: string) => setOpen((old) => {
        const next = new Set(old);
        if (!next.delete(key)) {
            next.add(key);
        }
        return next;
    });
    return (
        <div className="licenses">
            <header className="licenses-header">
                <div>
                    <h1>오픈소스 라이선스</h1>
                    <p>templide는 아래 오픈소스 소프트웨어를 사용합니다. 항목을 누르면 라이선스 전문을 볼 수 있습니다. ({total}개)</p>
                </div>
                <label className="licenses-search">
                    <Search size={14} />
                    <input value={query} placeholder="이름이나 라이선스로 찾기" onChange={(event) => setQuery(event.target.value)} />
                </label>
            </header>
            <main className="licenses-body">
                {shown.map((section) => (
                    <section key={section.title}>
                        <h2>{section.title} <span>{section.packages.length}</span></h2>
                        {section.packages.map((p) => {
                            const key = `${section.title}/${p.name}/${p.version}`;
                            const expanded = open.has(key);
                            return (
                                <div key={key} className={'licenses-item' + (expanded ? ' open' : '')}>
                                    <button className="licenses-row" onClick={() => toggle(key)}>
                                        {expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                                        <span className="licenses-name">{p.name}</span>
                                        {p.version && <span className="licenses-version">{p.version}</span>}
                                        <span className="licenses-license">{p.license}</span>
                                    </button>
                                    {expanded && (
                                        <div className="licenses-detail">
                                            <a className="licenses-url" href={p.url} onClick={(event) => openSite(event, p.url)}>{p.url}</a>
                                            {p.texts.map((id) => <pre key={id}>{texts[id]}</pre>)}
                                        </div>
                                    )}
                                </div>
                            );
                        })}
                    </section>
                ))}
                {shown.length === 0 && <div className="licenses-empty">찾는 항목이 없습니다</div>}
            </main>
        </div>
    );
}

createRoot(document.getElementById('root')!).render(<Licenses />);

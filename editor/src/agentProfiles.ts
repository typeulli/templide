// AI 탭의 에이전트 프로필. 프로필은 에이전트 프로그램(Claude Code, Codex, OpenCode), 모델 제공자, 모델의 조합이다.
// 설정(settings.ts)에 저장하고, 설정 창에서 만들고 고치며, 실행은 Agents.tsx가 한다

export type AgentId = 'claude-code' | 'codex' | 'opencode';
export type ProviderId = 'anthropic' | 'openai' | 'ollama' | 'lmstudio';

export type AgentProfile = {
    id: string;
    name: string;       // 비우면 조합으로 이름을 만든다 (profileLabel)
    agent: AgentId;
    provider: ProviderId;
    model: string;      // 비우면 에이전트의 기본 모델
    host: string;       // Ollama 주소. 비우면 defaultOllamaHost
};

export const agentLabels: Record<AgentId, string> = { 'claude-code': 'Claude Code', codex: 'Codex', opencode: 'OpenCode' };
export const providerLabels: Record<ProviderId, string> = { anthropic: 'Anthropic', openai: 'OpenAI', ollama: 'Ollama', lmstudio: 'LM Studio' };

// 에이전트마다 쓸 수 있는 제공자. Claude Code, Codex는 본래의 제공자만 쓰고, 로그인은 에이전트 안에서 한다.
// 이 컴퓨터에서 모델을 돌리는 제공자(Ollama, LM Studio)는 OpenCode로 돌리며 로그인이 없다. 편집기는 어느 쪽이든 키를 다루지 않는다
export const agentProviders: Record<AgentId, ProviderId[]> = {
    'claude-code': ['anthropic'],
    codex: ['openai'],
    opencode: ['ollama', 'lmstudio'],
};

// 로컬 제공자에는 에이전트의 기본 모델이 없으므로 모델을 꼭 정해야 한다
export const isLocalProvider = (provider: ProviderId) => provider === 'ollama' || provider === 'lmstudio';

export const defaultOllamaHost = 'http://localhost:11434';

// 모델 칸의 추천 목록. Ollama는 설치된 모델을 따로 묻는다 (ollama_models)
export const modelSuggestions: Partial<Record<ProviderId, string[]>> = { anthropic: ['sonnet', 'opus', 'haiku'] };

// 기본 프로필의 id는 예전 설정의 에이전트 id와 같아서, 예전에 고른 기본 에이전트가 그대로 기본 프로필이 된다
export const defaultProfiles: AgentProfile[] = [
    { id: 'claude-code', name: agentLabels['claude-code'], agent: 'claude-code', provider: 'anthropic', model: '', host: '' },
    { id: 'codex', name: agentLabels.codex, agent: 'codex', provider: 'openai', model: '', host: '' },
];

// 제공자가 하나뿐인 에이전트(Claude Code, Codex)는 이름에 제공자를 적지 않는다
export function profileLabel(profile: AgentProfile): string {
    const provider = agentProviders[profile.agent].length > 1 ? providerLabels[profile.provider] : '';
    return profile.name.trim() || [agentLabels[profile.agent], provider, profile.model.trim()].filter(Boolean).join(' · ');
}

// 'localhost:11434'처럼 적어도 되게 http://를 붙이고 끝의 /를 뗀다
export function ollamaHost(profile: AgentProfile): string {
    const host = profile.host.trim().replace(/\/+$/, '') || defaultOllamaHost;
    return /^[a-z]+:\/\//i.test(host) ? host : `http://${host}`;
}

// 설정 파일에서 읽은 값을 거른다. 모양이 틀린 프로필은 버리고, 남는 것이 없으면 기본 프로필로 돌아간다.
// 예전에는 Claude Code, Codex로도 로컬 제공자를 썼다. 그런 프로필은 제공자와 모델을 그대로 두고 OpenCode로 옮긴다
export function parseProfiles(stored: unknown): AgentProfile[] {
    if (!Array.isArray(stored)) {
        return structuredClone(defaultProfiles);
    }
    const text = (value: unknown) => typeof value === 'string' ? value : '';
    const seen = new Set<string>();
    const profiles: AgentProfile[] = [];
    for (const each of stored) {
        const given = (each ?? {}) as Record<string, unknown>;
        const provider = given.provider as ProviderId;
        const agent = (isLocalProvider(provider) && (given.agent === 'claude-code' || given.agent === 'codex') ? 'opencode' : given.agent) as AgentId;
        const id = text(given.id);
        if (!id || seen.has(id) || !Object.hasOwn(agentProviders, agent) || !agentProviders[agent].includes(provider)) {
            continue;
        }
        seen.add(id);
        profiles.push({ id, name: text(given.name), agent, provider, model: text(given.model), host: text(given.host) });
    }
    return profiles.length > 0 ? profiles : structuredClone(defaultProfiles);
}

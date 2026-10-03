// libs/templide.js가 만드는 전역 Templide
declare global {
    interface Window {
        Templide: {
            // 슬라이드 쇼 (reveal.js)
            mount(root: Element, deck: any, options?: object): any;
            // 편집기 미리보기용 슬라이드 하나
            renderSlide(deck: any, index: number): { stage: HTMLElement; fit(): void };
            // 캔버스에서 슬라이드 하나의 전환과 애니메이션을 재생한다. 돌려준 함수를 부르면 멈춘다
            preview(container: HTMLElement, deck: any, index: number, options?: { transition?: boolean; onEnd?(): void }): () => void;
            // 도형 종류의 조정값(adj1 ~)과 기본값(100000분의 1)
            adjustments(kind: string): { name: string; value: number }[];
            // 편집기 덱이 "file:경로"로 가리키는 비디오, 오디오를 읽을 주소로 바꾼다
            mediaUrl?(path: string): string;
        };
    }
}

export {};

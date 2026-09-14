// Headless loader for the Glasswing sandbox oracle (tools/glasswing-sandbox-v1_7_5.html).
//
// Extracts the single <script> block, runs it in a node `vm` context whose browser globals are
// inert stubs, and exposes the target-model pipeline. The page's top-level UI code runs once
// against the stubs; nothing of it is used afterwards.
//
//   const oracle = require('./oracle').loadOracle();
//   oracle.setN(128);
//   const model = oracle.generateTargetModel(seed, { requestedClass, complexity });
//   const sky = oracle.renderTargetAt(model, lambdaMeters, epochDays, nulled);

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const DEFAULT_HTML = path.resolve(__dirname, '..', 'glasswing-sandbox-v1_7_5.html');

// A callable, constructible object that absorbs any property access, assignment or call.
function makeStub() {
    const target = function stub() {};
    let proxy = null;
    const handler = {
        get(_t, prop) {
            if (prop === Symbol.toPrimitive) return (hint) => (hint === 'number' ? 0 : '');
            if (prop === Symbol.iterator) return function* empty() {};
            if (prop === 'then') return undefined;
            if (prop === 'length') return 0;
            if (prop === 'value') return '0';
            if (prop === 'checked') return false;
            return proxy;
        },
        set() { return true; },
        has() { return true; },
        apply() { return proxy; },
        construct() { return proxy; },
    };
    proxy = new Proxy(target, handler);
    return proxy;
}

function extractScript(html) {
    const open = html.indexOf('<script>');
    const close = html.lastIndexOf('</script>');
    if (open < 0 || close < open) throw new Error('oracle HTML: <script> block not found');
    return html.slice(open + '<script>'.length, close);
}

function makeSandbox() {
    const stub = makeStub();
    const storage = new Map();
    const sandbox = {
        console,
        performance: { now: () => 0 },
        document: stub, navigator: stub, AudioContext: stub, webkitAudioContext: stub,
        Blob: stub, URL: stub, Image: stub, ResizeObserver: stub,
        localStorage: {
            getItem: (k) => (storage.has(k) ? storage.get(k) : null),
            setItem: (k, v) => { storage.set(k, String(v)); },
            removeItem: (k) => { storage.delete(k); },
        },
        setTimeout: () => 0, clearTimeout: () => {}, setInterval: () => 0, clearInterval: () => {},
        requestAnimationFrame: () => 0, cancelAnimationFrame: () => {},
        addEventListener: () => {}, removeEventListener: () => {},
        getComputedStyle: () => stub, matchMedia: () => stub,
        devicePixelRatio: 1, innerWidth: 1280, innerHeight: 800,
    };
    sandbox.window = sandbox;
    sandbox.self = sandbox;
    return sandbox;
}

// Appended to the page script so it can reach the script's top-level `let`/`const` bindings.
const EXPORTS = `
;globalThis.__oracle = {
    generateTargetModel, applyTemporal, renderTargetAt, computeTargetFFT,
    evaluateSpectralFlux, scaleComp, applyCompatibleModifiers, mulberry32,
    TargetPrimitives, TargetRecipes, TargetModifiers,
    setN: (v) => { N = v; }, getN: () => N,
};`;

function loadOracle(htmlPath = DEFAULT_HTML) {
    const source = extractScript(fs.readFileSync(htmlPath, 'utf8'));
    const sandbox = makeSandbox();
    vm.createContext(sandbox);
    vm.runInContext(source + EXPORTS, sandbox, { filename: path.basename(htmlPath) + '.js' });
    return sandbox.__oracle;
}

module.exports = { loadOracle, DEFAULT_HTML };

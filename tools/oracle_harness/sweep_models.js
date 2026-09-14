// Generate target models with the oracle over seeds × families × complexities, to check the draw
// order of generateTargetModel beyond the paths the fixture battery exercises.
//
//   node tools/oracle_harness/sweep_models.js <cases.json> <oracle_models.json> [seedCount=120]
//
// Writes the case list (for `dump_cpp_target --models`) and the oracle models, in the same order.
// Seeds: 1..seedCount/2 plus seedCount/2 scattered 32-bit seeds. N = 128 as in the battery.

'use strict';

const fs = require('fs');
const { loadOracle } = require('./oracle');

const COMPLEXITIES = ['simple', 'structured', 'complex', 'free'];
const FAMILY_COUNT = 8;

function sweepSeeds(count) {
    const half = Math.floor(count / 2);
    const seeds = [];
    for (let i = 1; i <= half; i++) seeds.push(i);
    for (let i = 1; i <= count - half; i++) seeds.push(Math.imul(i, 2654435761) >>> 0);
    return seeds;
}

function main() {
    const [casesPath, modelsPath, countArg = '120'] = process.argv.slice(2);
    if (!casesPath || !modelsPath) {
        console.error('usage: sweep_models.js <cases.json> <oracle_models.json> [seedCount]');
        process.exit(2);
    }

    const cases = [];
    for (const seed of sweepSeeds(Number(countArg))) {
        for (let requestedClass = 0; requestedClass < FAMILY_COUNT; requestedClass++) {
            for (const complexity of COMPLEXITIES) cases.push({ seed, requestedClass, complexity });
        }
    }

    const oracle = loadOracle();
    oracle.setN(128);
    const models = cases.map((c) => oracle.generateTargetModel(c.seed, {
        requestedClass: c.requestedClass,
        complexity: c.complexity,
    }));

    fs.writeFileSync(casesPath, JSON.stringify(cases));
    fs.writeFileSync(modelsPath, JSON.stringify(models));
    console.log(`${cases.length} cases written`);
}

main();

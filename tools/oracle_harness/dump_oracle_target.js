// Dump the oracle's target model and rendered sky for one fixture of the battery.
//
//   node tools/oracle_harness/dump_oracle_target.js <fixture-index> <out.json> [battery.json]
//
// Reproduces buildFixtureBattery(): N = gridN, generateTargetModel(seed, {requestedClass,
// complexity}), renderTargetAt(model, lambdaMeters, epochDays, nulling). The output also carries
// the fixture's own thetaFovRad / fluxTotal so the harness can prove it matches the battery.

'use strict';

const fs = require('fs');
const path = require('path');
const { loadOracle } = require('./oracle');

const DEFAULT_BATTERY = path.resolve(__dirname, '..', '..', 'data', 'fixtures', 'glasswing_fixture_battery_v1_3.json');

function main() {
    const [indexArg, outPath, batteryPath = DEFAULT_BATTERY] = process.argv.slice(2);
    if (indexArg === undefined || outPath === undefined) {
        console.error('usage: dump_oracle_target.js <fixture-index> <out.json> [battery.json]');
        process.exit(2);
    }

    const battery = JSON.parse(fs.readFileSync(batteryPath, 'utf8'));
    const fixture = battery.fixtures[Number(indexArg)];
    if (!fixture) throw new Error(`fixture ${indexArg} not found`);

    const oracle = loadOracle();
    oracle.setN(fixture.gridN);
    const model = oracle.generateTargetModel(fixture.seed, {
        requestedClass: fixture.requestedClass,
        complexity: fixture.complexity,
    });
    const sky = oracle.renderTargetAt(model, fixture.lambdaMeters, fixture.epochDays, fixture.nulling);
    const fft = oracle.computeTargetFFT(sky);

    const out = {
        fixture: Number(indexArg),
        scenario: fixture.scenario,
        gridN: fixture.gridN,
        model,
        sky: Array.from(sky),
        flux: fft.flux,
        battery: { thetaFovRad: fixture.thetaFovRad, fluxTotal: fixture.fluxTotal, subtype: fixture.subtype },
        thetaFovRad: model.thetaObj * model.fovMul,
    };
    fs.writeFileSync(outPath, JSON.stringify(out));
    console.log(`fixture ${indexArg} ${fixture.scenario}: subtype ${model.subtype}, ` +
        `${model.components.length} components, modifiers [${model.modifiers.join(', ')}], ` +
        `flux ${fft.flux} (battery ${fixture.fluxTotal}), thetaFov ${out.thetaFovRad} (battery ${fixture.thetaFovRad})`);
}

main();

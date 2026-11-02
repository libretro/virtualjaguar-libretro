const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const workflows = ['c-cpp', 'ir-ab', 'acid-test', 'microbench', 'jaguar-demos', 'regression-test'];
const root = path.resolve(__dirname, '../../..');
const ref = 'refs/pull/1096/merge';
const mapping = JSON.stringify({ refs: [ref], 'ubuntu-latest': 'vj-linux-x64' });

for (const workflow of workflows) {
  const source = fs.readFileSync(path.join(root, '.github/workflows', `${workflow}.yml`), 'utf8');
  const expressions = [...source.matchAll(/^\s*runs-on: \$\{\{ (.+) \}\}$/gm)]
    .map(match => match[1]).filter(expression => expression.includes('VJ_TEMP_RUNNERS'));
  test(`${workflow}: Linux routing preserves ref, trust and architecture boundaries`, () => {
    assert.ok(expressions.length > 0);
    for (const expression of expressions) {
      const run = (overrides = {}) => {
        const github = {
          ref, event_name: 'pull_request', repository: 'libretro/virtualjaguar-libretro',
          event: { pull_request: { head: { repo: { full_name: 'libretro/virtualjaguar-libretro' } } } },
          ...overrides.github,
        };
        return vm.runInNewContext(expression, {
          github, matrix: { config: { os: overrides.os || 'ubuntu-latest' } },
          vars: { VJ_TEMP_RUNNERS: overrides.mapping ?? mapping, VJ_STUDIO_RUNNER: 'off' },
          fromJSON: JSON.parse, contains: (list, value) => list.includes(value),
        });
      };
      assert.equal(run(), 'vj-linux-x64');
      assert.equal(run({ mapping: '' }), 'ubuntu-latest');
      assert.equal(run({ mapping: JSON.stringify({ refs: [ref] }) }), 'ubuntu-latest');
      assert.equal(run({ github: { ref: 'refs/pull/9999/merge' } }), 'ubuntu-latest');
      assert.equal(run({ github: { event_name: 'pull_request_target' } }), 'ubuntu-latest');
      assert.equal(run({ github: { event: { pull_request: { head: { repo: { full_name: 'fork/repo' } } } } } }), 'ubuntu-latest');
      if (expression.includes('matrix.config.os')) {
        for (const os of ['ubuntu-24.04-arm', 'ubuntu-22.04', 'macos-latest', 'windows-latest']) {
          assert.equal(run({ os }), os);
        }
      }
    }
  });
}

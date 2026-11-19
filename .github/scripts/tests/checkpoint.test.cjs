const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync, spawnSync } = require('node:child_process');
const { jobDefinition, sourceKey, inventory } = require('../checkpoint.cjs');
const script = path.resolve(__dirname, '../checkpoint.cjs');
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-checkpoint-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  execFileSync('git', ['init', '-q', dir]);
  fs.writeFileSync(path.join(dir, 'source.c'), 'source');
  fs.writeFileSync(path.join(dir, 'sign.ps1'), 'sign');
  fs.writeFileSync(path.join(dir, 'workflow.yml'), 'name: Test\nenv:\n  CHANNEL: release\njobs:\n  build:\n    runs-on: windows-latest\n    steps:\n      - run: compile-and-test\n  sign:\n    steps:\n      - run: sign\n');
  execFileSync('git', ['-C', dir, 'add', '.']);
  return dir;
}
test('changing signing commands preserves the build key, changing source or compiler commands invalidates it', t => {
  const dir = fixture(t), old = process.cwd(); process.chdir(dir); t.after(() => process.chdir(old));
  const opts = { dependencies: 'source.c', job: 'workflow.yml#build', configuration: 'x64', runner: ['Windows', 'X64', 'image-v1'] };
  const first = sourceKey(opts);
  fs.appendFileSync('sign.ps1', 'changed');
  fs.appendFileSync('workflow.yml', '      - run: another-signing-step\n');
  assert.equal(sourceKey(opts), first);
  fs.writeFileSync('source.c', 'changed');
  assert.notEqual(sourceKey(opts), first);
  fs.writeFileSync('source.c', 'source');
  fs.writeFileSync('workflow.yml', fs.readFileSync('workflow.yml', 'utf8').replace('compile-and-test', 'new-compiler-and-test'));
  assert.notEqual(sourceKey(opts), first);
});
test('configuration, runner image and shared env are inputs', t => {
  const dir = fixture(t), old = process.cwd(); process.chdir(dir); t.after(() => process.chdir(old));
  const opts = { dependencies: 'source.c', job: 'workflow.yml#build', configuration: 'x64', runner: ['Windows', 'image-v1'] };
  const first = sourceKey(opts);
  assert.notEqual(sourceKey({ ...opts, configuration: 'arm64' }), first);
  assert.notEqual(sourceKey({ ...opts, runner: ['Windows', 'image-v2'] }), first);
  fs.writeFileSync('workflow.yml', fs.readFileSync('workflow.yml', 'utf8').replace('CHANNEL: release', 'CHANNEL: nightly'));
  assert.notEqual(sourceKey(opts), first);
  assert.throws(() => sourceKey({ ...opts, dependencies: '' }), /explicit/);
  assert.throws(() => sourceKey({ ...opts, dependencies: 'absent' }), /no files/);
});
test('only successful saves create receipts, restored bytes must match, and original provenance is retained', t => {
  const dir = fixture(t);
  fs.mkdirSync(path.join(dir, 'out'));
  fs.writeFileSync(path.join(dir, 'out/binary'), 'compiled bytes');
  const env = { ...process.env, GITHUB_OUTPUT: path.join(dir, 'outputs'), GITHUB_STEP_SUMMARY: path.join(dir, 'summary'),
    CHECKPOINT_KEY: 'input-key', CHECKPOINT_PATHS: 'out', CHECKPOINT_RECEIPT: '.ci-checkpoints/build.json',
    GITHUB_SHA: 'source-sha', GITHUB_RUN_ID: '123', GITHUB_RUN_ATTEMPT: '2', GITHUB_REPOSITORY: 'owner/repo' };
  const run = (mode, more = {}) => spawnSync(process.execPath, [script, 'receipt'], {
    cwd: dir, env: { ...env, CHECKPOINT_MODE: mode, ...more }, encoding: 'utf8' });
  assert.equal(run('restore', { CHECKPOINT_RESTORED: 'false' }).status, 0);
  assert(!fs.existsSync(path.join(dir, env.CHECKPOINT_RECEIPT)));
  assert.equal(run('save').status, 0);
  assert.equal(run('restore', { CHECKPOINT_RESTORED: 'true', GITHUB_SHA: 'new-sha' }).status, 0);
  assert.match(fs.readFileSync(env.GITHUB_STEP_SUMMARY, 'utf8'), /123, attempt 2, commit source-sha/);
  fs.writeFileSync(path.join(dir, 'out/binary'), 'tampered');
  assert.notEqual(run('restore', { CHECKPOINT_RESTORED: 'true' }).status, 0);
  assert.notEqual(run('save', { CHECKPOINT_PATHS: 'missing' }).status, 0);
});
test('job extraction does not accidentally include later jobs', () => {
  const s = '\njobs:\n  build:\n    steps:\n      - run: build\n  sign:\n    steps:\n      - run: sign\n';
  assert(!jobDefinition(s, 'build').includes('run: sign'));
  assert.equal(jobDefinition(s.replace(/\n/g, '\r\n'), 'build'), jobDefinition(s, 'build'));
  assert(jobDefinition(s, 'sign').includes('run: sign'));
  assert.throws(() => jobDefinition(s, 'missing'), /Missing/);
  assert.throws(() => inventory(['/tmp']), /repository-relative/);
});

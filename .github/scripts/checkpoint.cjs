// A successful checkpoint is tied to source bytes, this job's commands, configuration and runner image.
// Only an exact cache key is restored. Failed jobs never execute the save step.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const os = require('node:os');
const digest = value => crypto.createHash('sha256').update(value).digest('hex');
const lines = value => (value || '').split('\n').map(s => s.trim()).filter(Boolean);
function jobDefinition(text, id) {
  text = text.replace(/\r\n/g, '\n');
  const start = text.indexOf(`\n  ${id}:\n`);
  if (start < 0) throw new Error(`Missing workflow job ${id}`);
  const rest = text.slice(start + 1);
  const next = rest.slice(1).search(/\n  [\w-]+:\n/);
  const job = next < 0 ? rest : rest.slice(0, next + 1);
  // These two marker lines surround checkpoint plumbing only. Everything the job actually executes remains hashed.
  return job.replace(/    +# checkpoint plumbing begin[\s\S]*?    +# checkpoint plumbing end\n/g, '')
    .replace(/steps\.checkpoint\.outputs\.hit != 'true' && /g, '')
    .replace(/        if: \$\{\{ steps\.checkpoint\.outputs\.hit != 'true' \}\}\n/g, '');
}
function sourceKey({ dependencies, job, configuration, runner, outputs }) {
  const specs = lines(dependencies);
  if (!specs.length) throw new Error('Checkpoint dependencies must be explicit');
  const files = execFileSync('git', ['ls-files', '-z', '--', ...specs]).toString().split('\0').filter(Boolean).sort();
  if (!files.length) throw new Error('Checkpoint dependencies matched no files');
  const [workflow, id] = job.split('#');
  if (!id) throw new Error('Expected workflow-file#job-id');
  const workflowText = fs.readFileSync(workflow, 'utf8').replace(/\r\n/g, '\n');
  const inputs = files.map(file => [file, digest(fs.readFileSync(file)), fs.statSync(file).mode & 0o777]);
  // Global env and inputs also affect builds. Hash the header independently of other jobs' commands.
  return digest(JSON.stringify({ schema: 1, inputs, header: workflowText.split('\njobs:\n')[0],
    job: jobDefinition(workflowText, id), configuration, runner, outputs }));
}
function inventory(roots) {
  const files = {};
  function walk(p) {
    const stat = fs.lstatSync(p);
    if (stat.isSymbolicLink()) { files[p.split(path.sep).join('/')] = { link: fs.readlinkSync(p) }; return; }
    if (stat.isDirectory()) {
      for (const child of fs.readdirSync(p).sort()) walk(path.join(p, child));
    } else if (stat.isFile()) files[p.split(path.sep).join('/')] = { sha256: digest(fs.readFileSync(p)), mode: stat.mode & 0o777 };
    else throw new Error(`Unsupported checkpoint output ${p}`);
  }
  for (const root of roots) {
    if (path.isAbsolute(root) || root.split(/[\\/]/).includes('..') || !root || root === '.')
      throw new Error(`Expected a repository-relative output directory: ${root}`);
    if (!fs.existsSync(root)) throw new Error(`Missing checkpoint output ${root}`);
    walk(root);
  }
  if (!Object.keys(files).length) throw new Error('Checkpoint contains no output files');
  return files;
}
function output(name, value) {
  fs.appendFileSync(process.env.GITHUB_OUTPUT, `${name}<<CHECKPOINT_END\n${value}\nCHECKPOINT_END\n`);
}
async function importCheckpoint() {
  const env = process.env;
  const runs = JSON.parse(env.CHECKPOINT_SOURCE_RUNS);
  if (!Array.isArray(runs) || runs.length > 10 || runs.some(id => !Number.isSafeInteger(id) || id <= 0))
    throw new Error('Expected at most ten numeric source run IDs');
  const roots = lines(env.CHECKPOINT_PATHS);
  if (roots.length !== 1 || roots[0] !== 'glq') throw new Error('Cross-run recovery currently restores only GL evidence');
  const base = `${env.GITHUB_API_URL || 'https://api.github.com'}/repos/${env.GITHUB_REPOSITORY}`;
  const headers = { Authorization: `Bearer ${env.GH_TOKEN}`, Accept: 'application/vnd.github+json' };
  async function request(url) {
    for (let attempt = 0; attempt < 3; attempt++) {
      const response = await fetch(url, { headers });
      if (response.ok) return response;
      if (attempt === 2 || (response.status !== 429 && response.status < 500))
        throw new Error(`GitHub returned ${response.status}`);
      await new Promise(resolve => setTimeout(resolve, 1000 * (attempt + 1)));
    }
  }
  async function json(url) {
    return (await request(url)).json();
  }
  async function archive(artifact, file) {
    const response = await request(`${base}/actions/artifacts/${artifact.id}/zip`);
    const bytes = Buffer.from(await response.arrayBuffer());
    if (artifact.digest !== `sha256:${digest(bytes)}`) throw new Error('Artifact archive digest mismatch');
    fs.writeFileSync(file, bytes);
  }
  for (const runId of runs) {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'vj-checkpoint-import-'));
    try {
      const run = await json(`${base}/actions/runs/${runId}`);
      if (run.path !== '.github/workflows/release.yml' || run.head_repository?.full_name !== env.GITHUB_REPOSITORY || !['push', 'workflow_dispatch'].includes(run.event))
        continue;
      const artifacts = [];
      for (let page = 1; page <= 10; page++) {
        const batch = await json(`${base}/actions/runs/${runId}/artifacts?per_page=100&page=${page}`);
        artifacts.push(...batch.artifacts);
        if (batch.artifacts.length < 100) break;
      }
      const usable = name => artifacts.find(a => a.name === name && !a.expired && a.size_in_bytes > 0);
      const origin = usable(`checkpoint-origin-${env.CHECKPOINT_LANE}-save`) || usable(`checkpoint-origin-${env.CHECKPOINT_LANE}-restore`);
      const evidence = usable(env.CHECKPOINT_ARTIFACT);
      if (!origin || !evidence) continue;
      await archive(origin, path.join(temp, 'origin.zip'));
      const python = process.platform === 'win32' ? 'python' : 'python3';
      const receipt = JSON.parse(execFileSync(python, ['-c',
        'import zipfile,sys; z=zipfile.ZipFile(sys.argv[1]); names=[n for n in z.namelist() if not n.endswith("/")]; assert len(names)==1; print(z.read(names[0]).decode())',
        path.join(temp, 'origin.zip')], { encoding: 'utf8' }));
      if (receipt.schema !== 1 || receipt.repository !== env.GITHUB_REPOSITORY || receipt.key !== env.CHECKPOINT_KEY)
        continue;
      await archive(evidence, path.join(temp, 'evidence.zip'));
      fs.writeFileSync(path.join(temp, 'receipt.json'), JSON.stringify(receipt));
      execFileSync(python, ['-c', `import json,pathlib,sys,zipfile
root=pathlib.Path('glq').resolve()
receipt=json.loads(pathlib.Path(sys.argv[2]).read_text())
with zipfile.ZipFile(sys.argv[1]) as z:
 for item in z.infolist():
  if item.is_dir(): continue
  target=(root / item.filename.replace(chr(92), '/')).resolve()
  target.relative_to(root)
  relative=target.relative_to(pathlib.Path.cwd()).as_posix()
  assert relative in receipt['files'], relative
  target.parent.mkdir(parents=True,exist_ok=True)
  target.write_bytes(z.read(item))
`, path.join(temp, 'evidence.zip'), path.join(temp, 'receipt.json')]);
      for (const [file, value] of Object.entries(receipt.files)) {
        if (!file.startsWith('glq/') || value.link) throw new Error('Unexpected recovery output path');
        fs.chmodSync(file, value.mode);
      }
      if (JSON.stringify(receipt.files) !== JSON.stringify(inventory(roots))) throw new Error('Recovery evidence checksum mismatch');
      fs.mkdirSync(path.dirname(env.CHECKPOINT_RECEIPT), { recursive: true });
      fs.writeFileSync(env.CHECKPOINT_RECEIPT, JSON.stringify(receipt));
      output('imported', 'true');
      console.log(`Restored matching GL checkpoint artifacts from run ${runId}.`);
      return;
    } finally {
      fs.rmSync(temp, { recursive: true, force: true });
    }
  }
  console.log('No matching prior artifact checkpoint. Running this lane.');
  output('imported', 'false');
}

async function main(command) {
  const env = process.env;
  if (command === 'import') return importCheckpoint();
  if (command === 'key') {
    const key = sourceKey({ dependencies: env.CHECKPOINT_PROFILE ? JSON.parse(fs.readFileSync('.github/ci/checkpoints.json', 'utf8'))[env.CHECKPOINT_PROFILE]?.join('\n') : env.CHECKPOINT_DEPENDENCIES, job: env.CHECKPOINT_JOB,
      configuration: { requested: env.CHECKPOINT_CONFIGURATION,
        effective: Object.fromEntries(['LABEL', 'SHORT_VERSION', 'DEB_VERSION', 'CHANNEL', 'TV_DEFAULT', 'DISCORD_APP_ID',
          'MIN_MACOS', 'SMOKE_HASH', 'SMOKE_ROM', 'ARCH', 'PRESET_SET', 'REFRESH_GOLDENS', 'CONTENT_ROM', 'SHOT_FRAME',
          'SHOT_EMU_FRAMES', 'QUALIFY', 'LISTS', 'SLOW_LIMIT', 'SHARDS'].map(name => [name, env[name]])) }, outputs: lines(env.CHECKPOINT_PATHS), runner: [env.RUNNER_OS, env.RUNNER_ARCH, env.ImageOS, env.ImageVersion] });
    const lane = env.CHECKPOINT_LANE.replace(/[^a-zA-Z0-9_-]/g, '-');
    const receipt = `.ci-checkpoints/${lane}.json`;
    output('key', `vj-checkpoint-v1-${lane}-${key}`);
    output('receipt', receipt);
    output('paths', [...lines(env.CHECKPOINT_PATHS), receipt].join('\n'));
    fs.appendFileSync(env.GITHUB_ENV, `VJ_CHECKPOINT_KEY=vj-checkpoint-v1-${lane}-${key}\n`);
    return;
  }
  const roots = lines(env.CHECKPOINT_PATHS);
  if (env.CHECKPOINT_MODE === 'save') {
    const receipt = { schema: 1, complete: env.CHECKPOINT_PARTIAL !== 'true' || env.CHECKPOINT_SUCCESS === 'true', key: env.CHECKPOINT_KEY, sha: env.GITHUB_SHA, run: env.GITHUB_RUN_ID,
      attempt: env.GITHUB_RUN_ATTEMPT, repository: env.GITHUB_REPOSITORY, files: inventory(roots) };
    fs.mkdirSync(path.dirname(env.CHECKPOINT_RECEIPT), { recursive: true });
    fs.writeFileSync(env.CHECKPOINT_RECEIPT, JSON.stringify(receipt));
    return;
  }
  if (env.CHECKPOINT_RESTORED !== 'true') { output('hit', 'false'); return; }
  const receipt = JSON.parse(fs.readFileSync(env.CHECKPOINT_RECEIPT));
  if (receipt.schema !== 1 || receipt.key !== env.CHECKPOINT_KEY || receipt.repository !== env.GITHUB_REPOSITORY ||
      JSON.stringify(receipt.files) !== JSON.stringify(inventory(roots)))
    throw new Error('Checkpoint receipt or output checksums do not match');
  output('hit', receipt.complete ? 'true' : 'false');
  output('source_sha', receipt.sha);
  const note = `Reused successful checkpoint from run ${receipt.run}, attempt ${receipt.attempt}, commit ${receipt.sha}.`;
  console.log(note);
  if (env.GITHUB_STEP_SUMMARY) fs.appendFileSync(env.GITHUB_STEP_SUMMARY, note + '\n');
}
if (require.main === module) main(process.argv[2]).catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { digest, jobDefinition, sourceKey, inventory, importCheckpoint };

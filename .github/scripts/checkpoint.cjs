// A successful checkpoint is tied to source bytes, this job's commands, configuration and runner image.
// Only an exact cache key is restored. Failed jobs never execute the save step.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
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
    if (stat.isSymbolicLink()) { files[p] = { link: fs.readlinkSync(p) }; return; }
    if (stat.isDirectory()) {
      for (const child of fs.readdirSync(p).sort()) walk(path.join(p, child));
    } else if (stat.isFile()) files[p] = { sha256: digest(fs.readFileSync(p)), mode: stat.mode & 0o777 };
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
function main(command) {
  const env = process.env;
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
if (require.main === module) main(process.argv[2]);
module.exports = { digest, jobDefinition, sourceKey, inventory };

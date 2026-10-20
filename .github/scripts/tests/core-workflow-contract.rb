# Reject toolchain/architecture drift before omitting any compile-only PR builds.
require 'json'
require 'yaml'
root = File.expand_path('../../..', __dir__)
read_workflow = ->(name) { YAML.safe_load(File.read("#{root}/.github/workflows/#{name}.yml"), aliases: true).fetch('jobs') }
cpp = read_workflow.call('c-cpp')
release = read_workflow.call('release')
matrix = JSON.parse(File.read("#{root}/.github/ci/core-matrix.json"))
rows = release.fetch('build').fetch('strategy').fetch('matrix').fetch('config')
shared = matrix.select { |config| config['release_platform'] }
raise 'Expected eight shared matrix rows' unless shared.length == 8
shared.each do |config|
  peer = rows.find { |row| row['platform'] == config['release_platform'] }
  raise "Missing Release row: #{config['release_platform']}" unless peer
  %w[artifact os cc cxx make_extra emscripten android android_abi android_simd make_platform].each do |key|
    raise "#{config['displayTargetName']}: mismatched #{key}" unless config[key] == peer[key]
  end
end
%w[vita-build switch-build].each do |job|
  raise "#{job}: SDK container differs" unless cpp.fetch(job).fetch('container') == release.fetch(job).fetch('container')
end
steps = ->(job) { job.fetch('steps') }
%w[Set\ up\ Emscripten Set\ up\ Android\ NDK].each do |name|
  a = steps.call(cpp.fetch('build')).find { |step| step['name'] == name }
  b = steps.call(release.fetch('build')).find { |step| step['name'] == name }
  raise "#{name}: SDK setup/version differs" unless a['uses'] == b['uses'] && a['with'] == b['with']
end
%w[build vita-build switch-build].each do |name|
  steps.call(release.fetch(name)).select { |step| step['name']&.match?(/\ABuild(?: \(|\z)/) }.each do |step|
    env = step.fetch('env')
    raise "#{name}: canonical core is not production" unless env['TEST_EXPORTS'] == '0' && env['RELEASE_DEBUG_INFO'] == '1'
  end
end
raise 'Native Clang row removed' unless matrix.any? { |row| row['cc'] == 'clang' && !row['release_platform'] }
%w[msvc-check sanitizers coverage clang-tidy].each { |job| cpp.fetch(job) }
puts 'Core workflow contract: eight matching toolchains, two matching SDK containers; native compiler, sanitizer and ABI coverage retained.'

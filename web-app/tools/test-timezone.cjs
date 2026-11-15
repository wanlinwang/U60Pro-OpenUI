const { test } = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const ts = require('typescript')
const context = { exports: {} }
const source = fs.readFileSync(path.join(__dirname, '../src/data/timezone.ts'), 'utf8')
vm.runInNewContext(ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS } }).outputText, context)
const { parseSystemTimezone } = context.exports
test('timezone status validates offsets and rejects malformed replies', () => {
  assert.equal(parseSystemTimezone({ offset_minutes: 480, timezone: 'UTC-8' }).offset_minutes, 480)
  assert.equal(parseSystemTimezone({ offset_minutes: null, timezone: 'Custom/Zone' }).offset_minutes, null)
  for (const item of [null, {}, { offset_minutes: 481, timezone: 'UTC-8' }, { offset_minutes: 900, timezone: 'UTC-15' }, { offset_minutes: '480', timezone: 'UTC-8' }, { offset_minutes: 0, timezone: '\n' }]) {
    assert.throws(() => parseSystemTimezone(item))
  }
})

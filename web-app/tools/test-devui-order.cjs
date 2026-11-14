const { test } = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const ts = require('typescript')
const source = fs.readFileSync(path.join(__dirname, '../src/features/devui/menuOrder.ts'), 'utf8')
const context = { exports: {} }
vm.runInNewContext(ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS } }).outputText, context)
const { moveMenuItem } = context.exports
const plain = value => JSON.parse(JSON.stringify(value))

test('selected features can move in either direction without changing membership', () => {
  const original = ['overview', 'proxy', 'wifi', 'clash']
  assert.deepEqual(plain(moveMenuItem(original, 'overview', 'wifi')), ['proxy', 'wifi', 'overview', 'clash'])
  assert.deepEqual(plain(moveMenuItem(original, 'clash', 'proxy')), ['overview', 'clash', 'proxy', 'wifi'])
  assert.deepEqual(original, ['overview', 'proxy', 'wifi', 'clash'])
  assert.equal(moveMenuItem(original, 'missing', 'proxy'), original)
  assert.equal(moveMenuItem(original, 'proxy', 'missing'), original)
  assert.equal(moveMenuItem(original, 'proxy', 'proxy'), original)
})

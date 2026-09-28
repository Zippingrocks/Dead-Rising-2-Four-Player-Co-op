import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const parser = fileURLToPath(new URL('../../tools/analyze-minidump-basic.mjs', import.meta.url));
for (const architecture of ['x86', 'x64']) {
  test(`hang snapshot ${architecture} contexts and pointer-width stack hints`, () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'dr2-hang-test-'));
    try {
      const bytes = Buffer.alloc(4096);
      bytes.write('MDMP', 0, 'ascii');
      bytes.writeUInt32LE(2, 8);
      bytes.writeUInt32LE(32, 12);
      bytes.writeUInt32LE(3, 32);
      bytes.writeUInt32LE(52, 36);
      bytes.writeUInt32LE(128, 40);
      bytes.writeUInt32LE(4, 44);
      bytes.writeUInt32LE(112, 48);
      bytes.writeUInt32LE(256, 52);
      bytes.writeUInt32LE(1, 128);
      bytes.writeUInt32LE(42, 132);
      const stack = architecture === 'x86' ? 0x20000n : 0x100020000n;
      const base = architecture === 'x86' ? 0x400000n : 0x140000000n;
      bytes.writeBigUInt64LE(stack, 132 + 24);
      bytes.writeUInt32LE(64, 132 + 32);
      bytes.writeUInt32LE(2048, 132 + 36);
      bytes.writeUInt32LE(architecture === 'x86' ? 716 : 1232, 132 + 40);
      bytes.writeUInt32LE(512, 132 + 44);
      if (architecture === 'x86') {
        bytes.writeUInt32LE(0x10007, 512);
        bytes.writeUInt32LE(Number(base + 0x1000n), 512 + 184);
        bytes.writeUInt32LE(Number(stack + 16n), 512 + 196);
        bytes.writeUInt32LE(Number(base + 0x2000n), 2064);
      } else {
        bytes.writeUInt32LE(0x100007, 512 + 48);
        bytes.writeBigUInt64LE(base + 0x1000n, 512 + 248);
        bytes.writeBigUInt64LE(stack + 16n, 512 + 152);
        bytes.writeBigUInt64LE(base + 0x2000n, 2064);
      }
      bytes.writeUInt32LE(1, 256);
      bytes.writeBigUInt64LE(base, 260);
      bytes.writeUInt32LE(0x10000, 268);
      bytes.writeUInt32LE(2100, 280);
      const name = Buffer.from('deadrising2.exe', 'utf16le');
      bytes.writeUInt32LE(name.length, 2100);
      name.copy(bytes, 2104);
      const input = path.join(directory, 'fixture.dmp');
      const output = path.join(directory, 'report.json');
      fs.writeFileSync(input, bytes);
      for (const enabled of [false, true]) {
        const result = spawnSync(process.execPath, [parser, input, '--json', output,
          ...(enabled ? ['--all-threads'] : [])], { encoding: 'utf8' });
        assert.equal(result.status, 0, result.stderr);
        const report = JSON.parse(fs.readFileSync(output, 'utf8'));
        assert.equal(report.exception, null);
        if (!enabled) { assert.equal(report.threads, undefined); continue; }
        const thread = report.threads[0];
        assert.equal(thread.context.architecture, architecture);
        assert.equal(thread.instruction.offsetHex, '0x1000');
        assert.equal(thread.stackAddressHints.length, 1);
        assert.equal(thread.stackAddressHints[0].offsetHex, '0x2000');
        assert.equal(thread.stackAddressHints[0].stackOffsetHex, '0x10');
      }
    } finally {
      assert.equal(path.dirname(path.resolve(directory)), path.resolve(os.tmpdir()));
      assert.ok(path.basename(directory).startsWith('dr2-hang-test-'));
      fs.rmSync(directory, { recursive: true });
    }
  });
}

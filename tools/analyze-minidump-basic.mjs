#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';

const STREAMS = new Map([
  [3, 'ThreadListStream'],
  [4, 'ModuleListStream'],
  [5, 'MemoryListStream'],
  [6, 'ExceptionStream'],
  [7, 'SystemInfoStream'],
  [9, 'Memory64ListStream'],
  [15, 'MiscInfoStream'],
]);

function usage() {
  console.error('usage: node tools/analyze-minidump-basic.mjs <dump.dmp> [--json <out.json>] [--all-threads] [--memory <address:size>]...');
  process.exit(2);
}

const args = process.argv.slice(2);
if (args.length < 1) usage();
const dumpPath = path.resolve(args[0]);
let jsonOut = null;
let allThreads = false;
const memorySpecs = [];
for (let i = 1; i < args.length; i += 1) {
  if (args[i] === '--json') {
    jsonOut = path.resolve(args[i + 1] ?? '');
    i += 1;
  }
  else if (args[i] === '--memory') {
    memorySpecs.push(args[i + 1] ?? '');
    i += 1;
  }
  else if (args[i] === '--all-threads') allThreads = true;
}

const buf = fs.readFileSync(dumpPath);

function assertRange(offset, size, label) {
  if (offset < 0 || size < 0 || offset + size > buf.length) {
    throw new Error(`${label} outside dump bounds at 0x${offset.toString(16)} size 0x${size.toString(16)}`);
  }
}
function u16(offset) { assertRange(offset, 2, 'u16'); return buf.readUInt16LE(offset); }
function u32(offset) { assertRange(offset, 4, 'u32'); return buf.readUInt32LE(offset); }
function u64(offset) { assertRange(offset, 8, 'u64'); return buf.readBigUInt64LE(offset); }
function hex32(value) { return `0x${Number(value >>> 0).toString(16).padStart(8, '0')}`; }
function hex64(value) { return `0x${value.toString(16).padStart(16, '0')}`; }
function rvaSlice(rva, size, label) {
  assertRange(rva, size, label);
  return buf.subarray(rva, rva + size);
}
function minidumpString(rva) {
  if (!rva) return '';
  const byteLength = u32(rva);
  assertRange(rva + 4, byteLength, 'MINIDUMP_STRING');
  return buf.toString('utf16le', rva + 4, rva + 4 + byteLength).replace(/\0+$/g, '');
}

if (buf.toString('ascii', 0, 4) !== 'MDMP') {
  throw new Error(`not a minidump: ${dumpPath}`);
}

const header = {
  signature: buf.toString('ascii', 0, 4),
  version: hex32(u32(4)),
  numberOfStreams: u32(8),
  streamDirectoryRva: u32(12),
  timeDateStamp: u32(20),
  flags: hex64(u64(24)),
};

const streams = [];
for (let i = 0; i < header.numberOfStreams; i += 1) {
  const off = header.streamDirectoryRva + i * 12;
  const streamType = u32(off);
  const dataSize = u32(off + 4);
  const rva = u32(off + 8);
  streams.push({
    streamType,
    name: STREAMS.get(streamType) ?? `Stream${streamType}`,
    dataSize,
    rva,
  });
}

function stream(type) {
  return streams.find((s) => s.streamType === type) ?? null;
}

function parseMemoryRanges() {
  const ranges = [];
  const list = stream(5);
  if (list) {
    const count = u32(list.rva);
    for (let i = 0; i < count; i += 1) {
      const off = list.rva + 4 + i * 16;
      const start = u64(off);
      const size = u32(off + 8);
      const fileOffset = u32(off + 12);
      assertRange(fileOffset, size, 'MemoryList range');
      ranges.push({ start, end: start + BigInt(size), size, fileOffset, source: 'MemoryListStream' });
    }
  }

  const list64 = stream(9);
  if (list64) {
    const count64 = u64(list64.rva);
    if (count64 > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('Memory64 range count is too large');
    const count = Number(count64);
    let fileOffset64 = u64(list64.rva + 8);
    for (let i = 0; i < count; i += 1) {
      const off = list64.rva + 16 + i * 16;
      const start = u64(off);
      const size64 = u64(off + 8);
      if (size64 > BigInt(Number.MAX_SAFE_INTEGER) || fileOffset64 > BigInt(Number.MAX_SAFE_INTEGER)) {
        throw new Error('Memory64 range exceeds JavaScript safe integer limits');
      }
      const size = Number(size64);
      const fileOffset = Number(fileOffset64);
      assertRange(fileOffset, size, 'Memory64 range');
      ranges.push({ start, end: start + size64, size, fileOffset, source: 'Memory64ListStream' });
      fileOffset64 += size64;
    }
  }

  return ranges.sort((a, b) => (a.start < b.start ? -1 : a.start > b.start ? 1 : 0));
}

function readVirtual(address, size, memoryRanges) {
  let cursor = BigInt(address);
  let remaining = size;
  const chunks = [];
  while (remaining > 0) {
    const range = memoryRanges.find((candidate) => cursor >= candidate.start && cursor < candidate.end);
    if (!range) return null;
    const within = Number(cursor - range.start);
    const available = Math.min(remaining, range.size - within);
    chunks.push(buf.subarray(range.fileOffset + within, range.fileOffset + within + available));
    cursor += BigInt(available);
    remaining -= available;
  }
  return Buffer.concat(chunks, size);
}

function parseMemorySpec(spec) {
  const match = /^((?:0x)?[0-9a-f]+):((?:0x)?[0-9a-f]+)$/i.exec(spec);
  if (!match) throw new Error(`invalid --memory value '${spec}'; expected address:size`);
  const address = BigInt(match[1].startsWith('0x') ? match[1] : `0x${match[1]}`);
  const size = Number(BigInt(match[2].startsWith('0x') ? match[2] : `0x${match[2]}`));
  if (!Number.isSafeInteger(size) || size < 1 || size > 0x100000) {
    throw new Error(`invalid --memory size '${match[2]}'; expected 1..0x100000`);
  }
  return { spec, address, size };
}

function inspectMemory(spec, memoryRanges, modules) {
  const parsed = parseMemorySpec(spec);
  const bytes = readVirtual(parsed.address, parsed.size, memoryRanges);
  if (!bytes) {
    return { spec, addressHex: hex64(parsed.address), size: parsed.size, mapped: false };
  }
  const words = [];
  for (let offset = 0; offset + 4 <= bytes.length; offset += 4) {
    const value = bytes.readUInt32LE(offset);
    const mapped = mapAddress(BigInt(value), modules);
    words.push({
      offsetHex: `0x${offset.toString(16)}`,
      valueHex: hex32(value),
      moduleLeaf: mapped.moduleLeaf ?? null,
      moduleOffsetHex: mapped.offsetHex,
    });
  }
  const strings = [];
  for (let i = 0; i < bytes.length;) {
    let end = i;
    while (end < bytes.length && bytes[end] >= 0x20 && bytes[end] <= 0x7e) end += 1;
    if (end - i >= 4) strings.push({ offsetHex: `0x${i.toString(16)}`, text: bytes.toString('ascii', i, end) });
    i = end > i ? end : i + 1;
  }
  return {
    spec,
    addressHex: hex64(parsed.address),
    size: parsed.size,
    mapped: true,
    hex: bytes.toString('hex'),
    ascii: Array.from(bytes, (value) => (value >= 0x20 && value <= 0x7e ? String.fromCharCode(value) : '.')).join(''),
    strings,
    words,
  };
}

function parseSystemInfo() {
  const s = stream(7);
  if (!s) return null;
  return {
    processorArchitecture: u16(s.rva),
    processorLevel: u16(s.rva + 2),
    processorRevision: u16(s.rva + 4),
    numberOfProcessors: buf.readUInt8(s.rva + 6),
    majorVersion: u32(s.rva + 8),
    minorVersion: u32(s.rva + 12),
    buildNumber: u32(s.rva + 16),
    platformId: u32(s.rva + 20),
    csdVersion: minidumpString(u32(s.rva + 24)),
  };
}

function parseModules() {
  const s = stream(4);
  if (!s) return [];
  const count = u32(s.rva);
  const modules = [];
  for (let i = 0; i < count; i += 1) {
    const off = s.rva + 4 + i * 108;
    const base = u64(off);
    const size = u32(off + 8);
    const timeDateStamp = u32(off + 16);
    const name = minidumpString(u32(off + 20));
    modules.push({
      index: i,
      name,
      leaf: path.basename(name).toLowerCase(),
      base,
      baseHex: hex64(base),
      size,
      end: base + BigInt(size),
      endHex: hex64(base + BigInt(size)),
      timeDateStamp: hex32(timeDateStamp),
    });
  }
  return modules.sort((a, b) => (a.base < b.base ? -1 : a.base > b.base ? 1 : 0));
}

function moduleFor(address, modules) {
  const a = BigInt(address);
  return modules.find((m) => a >= m.base && a < m.end) ?? null;
}

function mapAddress(address, modules) {
  const a = BigInt(address);
  const mod = moduleFor(a, modules);
  if (!mod) return { addressHex: hex64(a), module: null, offsetHex: null };
  return {
    addressHex: hex64(a),
    module: mod.name,
    moduleLeaf: mod.leaf,
    offset: Number(a - mod.base),
    offsetHex: `0x${Number(a - mod.base).toString(16)}`,
  };
}

function parseException(modules) {
  const s = stream(6);
  if (!s) return null;
  const exceptionOffset = s.rva + 8;
  const code = u32(exceptionOffset);
  const flags = u32(exceptionOffset + 4);
  const record = u64(exceptionOffset + 8);
  const address = u64(exceptionOffset + 16);
  const numberParameters = Math.min(u32(exceptionOffset + 24), 15);
  const parameters = [];
  for (let i = 0; i < numberParameters; i += 1) {
    parameters.push(hex64(u64(exceptionOffset + 32 + i * 8)));
  }
  const contextSize = u32(s.rva + 160);
  const contextRva = u32(s.rva + 164);
  return {
    threadId: u32(s.rva),
    code: hex32(code),
    flags: hex32(flags),
    record: hex64(record),
    address: mapAddress(address, modules),
    numberParameters,
    parameters,
    context: parseContext(contextRva, contextSize),
  };
}

function parseContext(rva, size) {
  if (!rva || !size) return null;
  rvaSlice(rva, size, 'context');

  const x86Flags = size >= 204 ? u32(rva) : 0;
  if (size >= 204 && (x86Flags & 0x00010000)) {
    return {
      architecture: 'x86',
      size,
      flags: hex32(x86Flags),
      eax: hex32(u32(rva + 176)),
      ebx: hex32(u32(rva + 164)),
      ecx: hex32(u32(rva + 172)),
      edx: hex32(u32(rva + 168)),
      esi: hex32(u32(rva + 160)),
      edi: hex32(u32(rva + 156)),
      ebp: hex32(u32(rva + 180)),
      esp: hex32(u32(rva + 196)),
      eip: hex32(u32(rva + 184)),
    };
  }

  if (size >= 256) {
    const flags = u32(rva + 48);
    return {
      architecture: 'x64',
      size,
      flags: hex32(flags),
      rax: hex64(u64(rva + 120)),
      rbx: hex64(u64(rva + 144)),
      rcx: hex64(u64(rva + 128)),
      rdx: hex64(u64(rva + 136)),
      rsi: hex64(u64(rva + 168)),
      rdi: hex64(u64(rva + 176)),
      rbp: hex64(u64(rva + 160)),
      rsp: hex64(u64(rva + 152)),
      rip: hex64(u64(rva + 248)),
    };
  }

  return { architecture: 'unknown', size };
}

function parseThreads(exception, modules) {
  const s = stream(3);
  if (!s) return [];
  const count = u32(s.rva);
  const threads = [];
  for (let i = 0; i < count; i += 1) {
    const off = s.rva + 4 + i * 48;
    const threadId = u32(off);
    const stackStart = u64(off + 24);
    const stackSize = u32(off + 32);
    const stackRva = u32(off + 36);
    const thread = {
      index: i,
      threadId,
      priorityClass: u32(off + 8),
      priority: u32(off + 12),
      teb: hex64(u64(off + 16)),
      stackStartHex: hex64(stackStart),
      stackSize,
      stackRva,
      isExceptionThread: exception ? exception.threadId === threadId : false,
    };
    if (thread.isExceptionThread) {
      thread.stackAddressHints = scanStackHints(stackStart, stackSize, stackRva, exception.context, modules);
    }
    if (allThreads) {
      thread.context = parseContext(u32(off + 44), u32(off + 40));
      const instruction = thread.context?.eip ?? thread.context?.rip;
      thread.instruction = instruction ? mapAddress(BigInt(instruction), modules) : null;
      thread.stackAddressHints = scanStackHints(stackStart, stackSize, stackRva, thread.context, modules);
    }
    threads.push(thread);
  }
  return threads;
}

function scanStackHints(stackStart, stackSize, stackRva, context, modules) {
  if (!stackRva || !stackSize || !context) return [];
  let scanOffset = 0;
  const stackPointer = context.architecture === 'x86' ? context.esp : context.rsp;
  if (stackPointer) {
    const pointer = BigInt(stackPointer);
    if (pointer >= stackStart && pointer < stackStart + BigInt(stackSize)) {
      scanOffset = Number(pointer - stackStart);
    }
  }
  const limit = Math.min(stackSize, scanOffset + 8192);
  const hints = [];
  const seen = new Set();
  const wordSize = context.architecture === 'x64' ? 8 : 4;
  for (let off = scanOffset; off + wordSize <= limit; off += wordSize) {
    const value = wordSize === 8 ? u64(stackRva + off) : BigInt(u32(stackRva + off));
    const mod = moduleFor(value, modules);
    if (!mod) continue;
    const mapped = mapAddress(value, modules);
    const key = `${mapped.moduleLeaf}:${mapped.offsetHex}`;
    if (seen.has(key)) continue;
    seen.add(key);
    hints.push({
      stackOffsetHex: `0x${off.toString(16)}`,
      stackAddressHex: hex64(stackStart + BigInt(off)),
      valueHex: hex64(value),
      moduleLeaf: mapped.moduleLeaf,
      offsetHex: mapped.offsetHex,
      module: mapped.module,
    });
    if (hints.length >= 80) break;
  }
  return hints;
}

const modules = parseModules();
const memoryRanges = parseMemoryRanges();
const systemInfo = parseSystemInfo();
const exception = parseException(modules);
const threads = parseThreads(exception, modules);
const exeModule = modules.find((m) => m.leaf === 'deadrising2.exe') ?? null;
const exceptionThread = threads.find((t) => t.isExceptionThread) ?? null;

const result = {
  dumpPath,
  size: buf.length,
  header,
  streams,
  systemInfo,
  moduleCount: modules.length,
  exeModule: exeModule ? {
    name: exeModule.name,
    baseHex: exeModule.baseHex,
    size: exeModule.size,
    timeDateStamp: exeModule.timeDateStamp,
  } : null,
  exception,
  exceptionThread,
  threadCount: threads.length,
  threads: allThreads ? threads : undefined,
  memoryRangeCount: memoryRanges.length,
  memoryBytes: memoryRanges.reduce((sum, range) => sum + range.size, 0),
  memoryQueries: memorySpecs.map((spec) => inspectMemory(spec, memoryRanges, modules)),
  loadedModules: modules.map((m) => ({
    name: m.name,
    baseHex: m.baseHex,
    size: m.size,
    timeDateStamp: m.timeDateStamp,
  })),
};

if (jsonOut) {
  fs.mkdirSync(path.dirname(jsonOut), { recursive: true });
  fs.writeFileSync(jsonOut, JSON.stringify(result, null, 2));
}

console.log(JSON.stringify({
  dumpPath,
  exception: result.exception,
  exceptionThread: result.exceptionThread ? {
    threadId: result.exceptionThread.threadId,
    stackStartHex: result.exceptionThread.stackStartHex,
    stackSize: result.exceptionThread.stackSize,
    stackAddressHints: result.exceptionThread.stackAddressHints?.slice(0, 24) ?? [],
  } : null,
  exeModule: result.exeModule,
  memoryRangeCount: result.memoryRangeCount,
  memoryBytes: result.memoryBytes,
  memoryQueries: result.memoryQueries,
  jsonOut,
}, null, 2));

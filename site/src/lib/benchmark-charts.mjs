// Copyright (C) 2026 Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// Read the same Markdown numbers that the documentation displays.
export function benchmarkCharts(source) {
  const tables = [];
  let lines = [];
  function flush() {
    if (lines.length) {
      tables.push(lines.filter((_,i) => i !== 1).map(line => line.trim().slice(1,-1).split('|').map(cell => cell.trim())));
      lines = [];
    }
  }
  for (const line of source.split('\n')) {
    if (line.startsWith('|')) lines.push(line); else flush();
  }
  flush();
  if (tables.length !== 3) throw new Error('Expected the three documented benchmark tables');
  return tables.map((table,index) => {
    const rows = table.slice(1).map(([name,a,b]) => {
      const before = Number(a), after = Number(b);
      if (!Number.isFinite(before) || !Number.isFinite(after) || before <= 0 || after <= 0) throw new Error('Invalid benchmark row: ' + name);
      return {name,before,after,beforeLabel:a,afterLabel:b};
    });
    const summary = index === 0 ? rows[0] : rows.pop();
    const lower = index === 2;
    const max = Math.max(...rows.flatMap(row => [row.before,row.after]));
    const step = max > 10 ? 10 : 2;
    const ceiling = Math.ceil(max / step) * step;
    return {rows,summary,lower,ceiling,steps:ceiling / step,labels:table[0].slice(1,3),
      gain:lower ? summary.before / summary.after : summary.after / summary.before,
      reduction:Math.round((1-summary.after/summary.before)*100)};
  });
}

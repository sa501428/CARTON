#!/usr/bin/env node

import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const output = join(dirname(fileURLToPath(import.meta.url)), "..", "logo.svg");
const binCount = 20;
const binSize = 9.8;
const inset = 2;
const domains = [
  { start: 0, end: 2, label: "A" },
  { start: 3, end: 16, label: "B" },
  { start: 17, end: 19, label: "A" },
];

function domainFor(index) {
  return domains.find((domain) => index >= domain.start && index <= domain.end);
}

function noiseFor(row, column) {
  const low = Math.min(row, column);
  const high = Math.max(row, column);
  let value = (low + 1) * 0x9e3779b1 ^ (high + 1) * 0x85ebca77 ^ 0x3ac4d1;
  value ^= value >>> 16;
  value = Math.imul(value, 0x7feb352d);
  value ^= value >>> 15;
  value = Math.imul(value, 0x846ca68b);
  value ^= value >>> 16;
  return 0.975 + (value >>> 0) / 0xffffffff * 0.05;
}

function interpolateChannel(start, end, amount) {
  return Math.round(start + (end - start) * amount);
}

function colorFor(score) {
  const amount = Math.max(0, Math.min(1, score));
  const red = interpolateChannel(255, 210, amount);
  const green = interpolateChannel(247, 3, amount);
  const blue = interpolateChannel(246, 16, amount);
  return `#${[red, green, blue].map((channel) => channel.toString(16).padStart(2, "0")).join("")}`;
}

function formatCoordinate(value) {
  return Number(value.toFixed(1)).toString();
}

function contactScore(row, column) {
  const distance = Math.abs(row - column);
  const rowDomain = domainFor(row);
  const columnDomain = domainFor(column);
  let score = 0.008 + 0.86 * Math.exp(-distance / 1.75);

  if (rowDomain === columnDomain) {
    const isLargeCentralDomain = rowDomain.label === "B";
    score += (isLargeCentralDomain ? 0.13 : 0.09)
      * Math.exp(-distance / (isLargeCentralDomain ? 7.2 : 2.6));
  }

  // The two short endpoint A compartments are subtly enriched with each other.
  if (rowDomain.label === "A" && columnDomain.label === "A" && rowDomain !== columnDomain) {
    const rowEdgeDistance = row < 3 ? row : 19 - row;
    const columnEdgeDistance = column < 3 ? column : 19 - column;
    score += 0.06;
  }

  // A mirrored loop sits one bin in from the corners of the broad central domain.
  if ((row === 3 && column === 16) || (row === 16 && column === 3)) {
    score = 0.96;
  }

  return Math.max(0, Math.min(1, score * noiseFor(row, column)));
}

const cells = [];
for (let row = 0; row < binCount; ++row) {
  for (let column = 0; column < binCount; ++column) {
    const score = contactScore(row, column);
    const x = formatCoordinate(inset + column * binSize);
    const y = formatCoordinate(inset + row * binSize);
    const attributes = [
      `x="${x}"`,
      `y="${y}"`,
      `width="${binSize}"`,
      `height="${binSize}"`,
      `fill="${colorFor(score)}"`,
      `data-noise="${noiseFor(row, column).toFixed(5)}"`,
    ];
    if ((row === 4 && column === 15) || (row === 15 && column === 4)) {
      attributes.push('data-loop-corner="true"');
    }
    if (domainFor(row).label === "A" && domainFor(column).label === "A"
        && domainFor(row) !== domainFor(column)) {
      attributes.push('data-compartment-enrichment="A-A"');
    }
    cells.push(`    <rect ${attributes.join(" ")}/>`);
  }
}

const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 200 200" shape-rendering="crispEdges" role="img" aria-labelledby="title desc">
  <title id="title">Hi-C map — three domains with endpoint compartment enrichment</title>
  <desc id="desc">A symmetric 20-by-20 Hi-C matrix with two short endpoint domains, one large central domain, subtle enrichment between the two endpoint A compartments, and a mirrored loop pair set far from the diagonal.</desc>
  <metadata>Compartments: A3 B14 A3. Domain lengths: 3, 14, 3. Loop coordinates: (5,16) and (16,5), one bin inside the large domain corners. Symmetric deterministic noise seed: 3851473. Bin rectangles have no stroke and share exact numeric boundaries.</metadata>
  <rect width="200" height="200" fill="#fff9f8"/>
  <g>
${cells.join("\n")}
  </g>
</svg>
`;

writeFileSync(output, svg);

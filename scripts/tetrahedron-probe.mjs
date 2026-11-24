#!/usr/bin/env node
// Tetrahedron probe (slice 0032): the constants the D4 needs, and the one geometric fact its
// result-reading rule rests on, measured rather than assumed.
//
// Every die before the D4 is centrally symmetric: each face has another face exactly opposed to
// it, which is why "the numeral on the face toward the glass" has been a complete reading rule
// for four slices. A tetrahedron has no opposite face at all. What it has instead is an opposite
// *vertex*: the apex standing over the face the die rests on. That is what this probe measures,
// because the whole D4 reading rule is built on it, and a relation that is only nearly exact
// would show up as a numeral drawn at the wrong corner rather than as an obvious break.
//
// It also produces the numbers that become constants in src/geometry/die_shape.cpp, and the
// facing of the three visible faces, which decides whether the renderer draws their numerals at
// all (src/render/die_view.cpp).
//
// It borrows its measuring tools, and their self-check against solids whose inertia is known in
// closed form, from scripts/die-shape-probe.mjs, and checks its own answer a second time against
// the tetrahedron's own closed form, which is an independent route to the same number.
//
// Usage: node scripts/tetrahedron-probe.mjs
//
// A probe answers its question once and its numbers land in the slice; it is not a verify
// script, so it is not wired into package.json (CLAUDE.md section 3). Everything above main()
// is pure and tested by scripts/tetrahedron-probe.test.mjs.

import { pathToFileURL } from 'node:url';

import {
  atCircumradius,
  dihedralAngles,
  edgeLengths,
  faceNormal,
  GEOMETRY_TOLERANCE,
  inertia,
  inradii,
  selfCheck,
} from './die-shape-probe.mjs';

import { hullFaces } from './dodecahedron-probe.mjs';

/**
 * @brief The inertia per unit mass of a uniform regular tetrahedron, per unit edge length
 * squared: a/20, the same about every axis through the centroid because the solid is Platonic.
 *
 * The closed form, used as a second opinion on the integrator rather than as the source of the
 * firmware's constant: the constant is measured, and this says the measurement is right.
 */
export const INERTIA_PER_MASS_PER_EDGE_SQUARED = 1 / 20;

/** @brief Corners to a tetrahedron's face, and faces to the solid: both 4, and both are checked. */
const CORNERS_PER_FACE = 3;
const FACE_COUNT = 4;

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a) => Math.sqrt(dot(a, a));

/**
 * @returns {number[][]} the 4 vertices of a regular tetrahedron: alternate corners of a cube of
 *   side 2, which puts the centroid at the origin exactly rather than to rounding.
 */
export function tetrahedronVertices() {
  return [
    [1, 1, 1],
    [1, -1, -1],
    [-1, 1, -1],
    [-1, -1, 1],
  ];
}

/** @returns {{vertices: number[][], faces: number[][]}} a tetrahedron at circumradius @p radius. */
export function tetrahedron(radius = 1) {
  const vertices = tetrahedronVertices();
  const faces = hullFaces(vertices, CORNERS_PER_FACE);
  if (faces.length !== FACE_COUNT) throw new Error(`found ${faces.length} faces, not ${FACE_COUNT}`);
  return atCircumradius({ vertices, faces }, radius);
}

/**
 * @brief For each vertex, the face whose outward normal points most nearly opposite to it.
 *
 * This is the D4's reading rule stated as geometry: the die rests on a face, the vertex opposite
 * that face is its apex, and the apex's numeral is the resting face's numeral. The product reads
 * the result from the face, so what has to be exact is that the vertex standing highest is the
 * one belonging to no visible corner of the resting face.
 *
 * @returns {{pairing: number[], worst: number, oneToOne: boolean}} the face opposite each vertex,
 *   the largest angle in radians by which any such pair misses being exactly opposed, and whether
 *   the pairing uses each face exactly once.
 */
export function oppositeVertices(solid) {
  const normals = solid.faces.map((_, i) => faceNormal(solid, i));
  const pairing = [];
  let worst = 0;
  for (const vertex of solid.vertices) {
    const direction = vertex.map((c) => c / norm(vertex));
    let best = 0;
    let bestAlignment = dot(direction, normals[0]);
    for (let i = 1; i < normals.length; i++) {
      const alignment = dot(direction, normals[i]);
      if (alignment < bestAlignment) {
        bestAlignment = alignment;
        best = i;
      }
    }
    pairing.push(best);
    // Exactly opposed is an alignment of -1; rounding can put it a hair past, out of acos's domain.
    worst = Math.max(worst, Math.PI - Math.acos(Math.max(-1, bestAlignment)));
  }
  return { pairing, worst, oneToOne: new Set(pairing).size === solid.vertices.length };
}

/**
 * @brief How squarely the faces that are not the resting face point at the camera.
 *
 * The renderer draws a face's numeral only when its normal's component toward the camera reaches
 * NUMERAL_MIN_FACING (src/render/die_view.cpp). With one face down, every other face of a
 * tetrahedron is at the same tilt, so this one number decides whether a resting D4 shows any
 * numeral at all.
 *
 * @returns {{facing: number, degrees: number}} the component toward the camera, and the angle.
 */
export function restingFaceFacing(solid) {
  // Every other face being at one tilt is a fact about a tetrahedron, so a solid that is not one
  // has no single answer here and must say so rather than return the first face's.
  if (solid.faces.length !== FACE_COUNT) throw new Error(`a tetrahedron has ${FACE_COUNT} faces, not ${solid.faces.length}`);
  const normals = solid.faces.map((_, i) => faceNormal(solid, i));
  // Lay face 0 down by reading every other normal against face 0's own outward direction
  // reversed, which is where the camera is once that face is on the tray floor.
  const towardCamera = normals[0].map((c) => -c);
  const facings = normals.slice(1).map((n) => dot(n, towardCamera));
  const spread = Math.max(...facings) - Math.min(...facings);
  if (spread > GEOMETRY_TOLERANCE) throw new Error(`the three raised faces differ in facing by ${spread}`);
  return { facing: facings[0], degrees: (Math.acos(facings[0]) * 180) / Math.PI };
}

/** @returns {object} every number slice 0032 quotes about the tetrahedron, at circumradius 1. */
export function measure() {
  const solid = tetrahedron(1);
  const { principal, offDiagonal } = inertia(solid);
  const radii = solid.vertices.map(norm);
  const faceDistances = inradii(solid);
  const edges = edgeLengths(solid);
  const mean = principal.reduce((a, b) => a + b, 0) / 3;
  return {
    vertices: solid.vertices.length,
    faces: solid.faces.length,
    corners: Math.max(...solid.faces.map((f) => f.length)),
    principal,
    offDiagonal,
    spread: Math.max(...principal.map((p) => Math.abs(p - mean))) / mean,
    circumradiusSpread: Math.max(...radii) - Math.min(...radii),
    inradius: faceDistances[0],
    inradiusSpread: Math.max(...faceDistances) - Math.min(...faceDistances),
    apex: oppositeVertices(solid),
    raised: restingFaceFacing(solid),
    edges,
    circumradiusPerEdge: 1 / edges[0],
    dihedral: dihedralAngles(solid),
    // The integrated inertia against the tetrahedron's own closed form, which depends on the
    // edge and so is a different expression of the same quantity, not the same arithmetic again.
    closedFormError: Math.abs(principal[0] - INERTIA_PER_MASS_PER_EDGE_SQUARED * edges[0] ** 2),
  };
}

/** @returns {boolean} whether every face is wound counter-clockwise seen from outside. */
export function woundOutward(solid) {
  return solid.faces.every((face, i) => dot(faceNormal(solid, i), solid.vertices[face[0]]) > 0);
}

function main() {
  const checks = selfCheck();
  console.log("Self-check, against inertia known in closed form (scripts/die-shape-probe.mjs's):");
  for (const { name, expected, got, error } of checks) {
    console.log(`  ${name.padEnd(22)} expected ${expected.toFixed(9)}  got ${got.toFixed(9)}  error ${error.toExponential(1)}`);
  }
  const worst = Math.max(...checks.map((c) => c.error));
  if (worst > GEOMETRY_TOLERANCE) {
    console.error(`self-check off by ${worst.toExponential(1)}: the numbers below are not trustworthy`);
    process.exitCode = 1;
    return;
  }

  const m = measure();
  console.log('\nThe tetrahedron, at circumradius 1:');
  console.log(`  ${m.vertices} vertices, ${m.faces} faces, ${m.corners} corners to a face`);
  console.log(`  principal moments per mass: ${m.principal.map((p) => p.toFixed(10)).join('  ')}`);
  console.log(`  largest off-diagonal term:  ${m.offDiagonal.toExponential(2)}  (0 means these axes are principal)`);
  console.log(`  spread about their mean:    ${(m.spread * 100).toExponential(2)}%`);
  console.log(`  against the closed form (edge squared)/20: error ${m.closedFormError.toExponential(2)}`);
  console.log(`  every vertex the same distance from the centre, to ${m.circumradiusSpread.toExponential(2)}`);
  console.log(`  every face the same distance from the centre, to ${m.inradiusSpread.toExponential(2)}`);
  console.log(`  edge ${m.edges.map((e) => e.toFixed(10)).join(' and ')} of the circumradius`);
  console.log(`  circumradius ${m.circumradiusPerEdge.toFixed(10)} of the edge`);
  console.log(`  dihedral angle ${m.dihedral.map((a) => a.toFixed(4)).join(' and ')} degrees`);
  console.log(`  inradius ${m.inradius.toFixed(10)} of the circumradius`);

  console.log('\nThe reading rule, which no die before the D4 needed:');
  console.log(`  face opposite each vertex: ${m.apex.pairing.join(', ')}  each face used once: ${m.apex.oneToOne}`);
  console.log(`  worst miss from exactly opposed: ${m.apex.worst.toExponential(2)} rad`);
  console.log(`  with one face down, the other three face the camera at ${m.raised.facing.toFixed(10)}`);
  console.log(`    which is ${m.raised.degrees.toFixed(4)} degrees off square, against NUMERAL_MIN_FACING of 0.5`);

  // The D20's circumradius, which every die since the D8 has been sized to, so the dice of a set
  // share one bounding sphere (slices 0026, 0027 and 0029).
  const D20_EDGE_MM = 10.5;
  const ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE = 0.9510565;
  const circumradiusMm = D20_EDGE_MM * ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE;
  console.log(`\nAt the D20's circumradius of ${circumradiusMm.toFixed(5)} mm, the constants the firmware needs:`);
  console.log(`  edge length           ${(circumradiusMm / m.circumradiusPerEdge).toFixed(6)} mm`);
  console.log(`  inertiaPerMass        ${(m.principal[0] * circumradiusMm ** 2).toFixed(6)} mm^2`);
  console.log(`  inradius              ${(m.inradius * circumradiusMm).toFixed(6)} mm`);
  console.log(`  height face to apex   ${((1 + m.inradius) * circumradiusMm).toFixed(6)} mm`);
  console.log(`  width across vertices ${(2 * circumradiusMm).toFixed(6)} mm`);
}

// argv[1] is absent when this module is imported by something that was not started from a
// file, such as `node -e`, and converting an absent path throws rather than saying so.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();

#!/usr/bin/env node
// Dodecahedron probe (slice 0029): the constants the D12 needs, measured rather than looked up.
//
// A dodecahedron is Platonic, so unlike the D10 it has no family and nothing to choose: its
// inertia is isotropic whatever size it is built at. What this probe is for is producing the
// numbers that become constants in src/geometry/die_shape.cpp, and proving the body is the one
// the fairness argument assumes: one circumradius, one inradius, and every face's normal exactly
// opposed by another's so that "the opposite face" means something.
//
// It borrows its measuring tools from scripts/die-shape-probe.mjs, including that probe's
// self-check against a cube and an icosahedron whose inertia is known in closed form, and it
// checks its own answer a second time against the dodecahedron's own closed form, which is an
// independent route to the same number.
//
// Usage: node scripts/dodecahedron-probe.mjs
//
// A probe answers its question once and its numbers land in the slice; it is not a verify
// script, so it is not wired into package.json (CLAUDE.md section 3). Everything above main()
// is pure and tested by scripts/dodecahedron-probe.test.mjs.

import { pathToFileURL } from 'node:url';

import {
  atCircumradius,
  dihedralAngles,
  edgeLengths,
  faceNormal,
  GEOMETRY_TOLERANCE,
  inertia,
  inradii,
  oppositeFaces,
  selfCheck,
} from './die-shape-probe.mjs';

export const PHI = (1 + Math.sqrt(5)) / 2;

/**
 * @brief The inertia per unit mass of a uniform dodecahedron, per unit edge length squared.
 *
 * The closed form, used as a second opinion on the integrator rather than as the source of the
 * firmware's constant: the constant is measured, and this says the measurement is right.
 */
export const INERTIA_PER_MASS_PER_EDGE_SQUARED = (39 * PHI + 28) / 150;

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const norm = (a) => Math.sqrt(dot(a, a));
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];

/**
 * @returns {number[][]} the 20 vertices of a dodecahedron: a cube of side 2 and the three
 *   mutually perpendicular golden rectangles that meet it at its faces. Exact in terms of φ.
 */
export function dodecahedronVertices() {
  const vertices = [];
  for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) vertices.push([x, y, z]);
  for (const s1 of [-1, 1]) {
    for (const s2 of [-1, 1]) {
      vertices.push([0, s1 / PHI, s2 * PHI], [s1 / PHI, s2 * PHI, 0], [s1 * PHI, 0, s2 / PHI]);
    }
  }
  return vertices;
}

/**
 * @brief The faces of the convex hull of @p vertices, found from the solid itself.
 *
 * Every vertex triple spans a plane; a plane with all the other vertices on one side of it is a
 * face, and the vertices on it are its corners. Slower than naming the faces by hand and the
 * reason to prefer it: a hand-written face list can be wrong in a way that still looks like a
 * solid, and a dodecahedron's dual alignment is easy to get backwards.
 *
 * The solid must enclose the origin, as every die body does, since that is what tells an
 * outward plane from an inward one here.
 *
 * @returns {number[][]} corner indices per face, wound counter-clockwise seen from outside.
 * @throws when a face does not have @p cornersPerFace corners, rather than returning a solid
 *   that is not the one asked for.
 */
export function hullFaces(vertices, cornersPerFace) {
  const normals = [];
  for (let i = 0; i < vertices.length; i++) {
    for (let j = i + 1; j < vertices.length; j++) {
      for (let k = j + 1; k < vertices.length; k++) {
        const raw = cross(sub(vertices[j], vertices[i]), sub(vertices[k], vertices[i]));
        const length = norm(raw);
        if (length < GEOMETRY_TOLERANCE) continue;
        let normal = raw.map((c) => c / length);
        let offset = dot(vertices[i], normal);
        // Turn the plane outward rather than discard it. The triple is taken in ascending index
        // order, and for a convex polygon every such triple winds the same way, so an ordering
        // exists in which all ten of a pentagon's triples wind inward and the face is never seen
        // at all: the half-plus-mirror order die_shape.cpp lists the D12's vertices in loses
        // three of its twelve faces that way.
        if (offset < 0) {
          normal = normal.map((c) => -c);
          offset = -offset;
        }
        // A plane through the centre of a solid that encloses it bounds nothing.
        if (offset <= GEOMETRY_TOLERANCE) continue;
        if (vertices.some((v) => dot(v, normal) > offset + GEOMETRY_TOLERANCE)) continue;
        if (normals.some((n) => norm(sub(n, normal)) < GEOMETRY_TOLERANCE)) continue;
        normals.push(normal);
      }
    }
  }
  return normals.map((normal) => {
    const offset = Math.max(...vertices.map((v) => dot(v, normal)));
    const on = vertices
      .map((v, index) => ({ v, index }))
      .filter(({ v }) => Math.abs(dot(v, normal) - offset) < GEOMETRY_TOLERANCE);
    if (on.length !== cornersPerFace) {
      throw new Error(`a face has ${on.length} corners, not ${cornersPerFace}`);
    }
    // Counter-clockwise seen from outside: by angle about the face's own centre, in a frame
    // whose first axis points at the first corner and whose second completes a right-handed set
    // with the outward normal.
    const centre = normal.map((c) => c * offset);
    const first = sub(on[0].v, centre);
    const u = first.map((c) => c / norm(first));
    const w = cross(normal, u);
    return on
      .map(({ v, index }) => {
        const p = sub(v, centre);
        return { angle: Math.atan2(dot(p, w), dot(p, u)), index };
      })
      .sort((a, b) => a.angle - b.angle)
      .map(({ index }) => index);
  });
}

/** @returns {{vertices: number[][], faces: number[][]}} a dodecahedron at circumradius @p radius. */
export function dodecahedron(radius = 1) {
  const vertices = dodecahedronVertices();
  const CORNERS_PER_FACE = 5;
  const FACE_COUNT = 12;
  const faces = hullFaces(vertices, CORNERS_PER_FACE);
  if (faces.length !== FACE_COUNT) throw new Error(`found ${faces.length} faces, not ${FACE_COUNT}`);
  return atCircumradius({ vertices, faces }, radius);
}

/** @returns {object} every number slice 0029 quotes about the dodecahedron, at circumradius 1. */
export function measure() {
  const solid = dodecahedron(1);
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
    opposite: oppositeFaces(solid),
    edges,
    circumradiusPerEdge: 1 / edges[0],
    dihedral: dihedralAngles(solid),
    // The integrated inertia against the dodecahedron's own closed form, which depends on the
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
  console.log('\nThe dodecahedron, at circumradius 1:');
  console.log(`  ${m.vertices} vertices, ${m.faces} faces, ${m.corners} corners to a face`);
  console.log(`  principal moments per mass: ${m.principal.map((p) => p.toFixed(10)).join('  ')}`);
  console.log(`  largest off-diagonal term:  ${m.offDiagonal.toExponential(2)}  (0 means these axes are principal)`);
  console.log(`  spread about their mean:    ${(m.spread * 100).toExponential(2)}%`);
  console.log(`  against the closed form (39φ+28)/150 of the edge squared: error ${m.closedFormError.toExponential(2)}`);
  console.log(`  every vertex the same distance from the centre, to ${m.circumradiusSpread.toExponential(2)}`);
  console.log(`  every face the same distance from the centre, to ${m.inradiusSpread.toExponential(2)}`);
  console.log(`  opposite faces antiparallel to ${m.opposite.worst.toExponential(2)}, pairing is an involution: ${m.opposite.involution}`);
  console.log(`  edge ${m.edges.map((e) => e.toFixed(10)).join(' and ')} of the circumradius`);
  console.log(`  circumradius ${m.circumradiusPerEdge.toFixed(10)} of the edge`);
  console.log(`  dihedral angle ${m.dihedral.map((a) => a.toFixed(4)).join(' and ')} degrees`);
  console.log(`  inradius ${m.inradius.toFixed(10)} of the circumradius`);

  // The D20's circumradius, which the D8 and the D10 were sized to as well, so the dice of a set
  // share one bounding sphere (slices 0026 and 0027).
  const D20_EDGE_MM = 10.5;
  const ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE = 0.9510565;
  const circumradiusMm = D20_EDGE_MM * ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE;
  console.log(`\nAt the D20's circumradius of ${circumradiusMm.toFixed(5)} mm, the constants the firmware needs:`);
  console.log(`  edge length           ${(circumradiusMm / m.circumradiusPerEdge).toFixed(6)} mm`);
  console.log(`  inertiaPerMass        ${(m.principal[0] * circumradiusMm ** 2).toFixed(6)} mm^2`);
  console.log(`  inradius              ${(m.inradius * circumradiusMm).toFixed(6)} mm`);
  console.log(`  height across faces   ${(2 * m.inradius * circumradiusMm).toFixed(6)} mm`);
  console.log(`  width across vertices ${(2 * circumradiusMm).toFixed(6)} mm`);
}

// argv[1] is absent when this module is imported by something that was not started from a
// file, such as `node -e`, and converting an absent path throws rather than saying so.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();

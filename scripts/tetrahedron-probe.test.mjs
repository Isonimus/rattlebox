// Tests for the pure half of scripts/tetrahedron-probe.mjs (slice 0032). The probe's output
// becomes constants in the firmware and, unlike every die before it, a reading rule as well, so
// these check the solid it builds, the arithmetic it measures with, and the apex relation the
// D4's result depends on.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  INERTIA_PER_MASS_PER_EDGE_SQUARED,
  measure,
  oppositeVertices,
  restingFaceFacing,
  tetrahedron,
  tetrahedronVertices,
  woundOutward,
} from './tetrahedron-probe.mjs';
import { faceNormal, GEOMETRY_TOLERANCE, inertia, selfCheck } from './die-shape-probe.mjs';

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const norm = (a) => Math.sqrt(dot(a, a));

test('the borrowed self-check reproduces inertia known in closed form, which is what makes the rest believable', () => {
  for (const { name, error } of selfCheck()) {
    assert.ok(error < GEOMETRY_TOLERANCE, `${name} off by ${error}`);
  }
});

test('the solid is a tetrahedron: 4 vertices, 4 faces, 3 corners to a face', () => {
  const solid = tetrahedron(1);
  assert.equal(solid.vertices.length, 4);
  assert.equal(solid.faces.length, 4);
  for (const face of solid.faces) assert.equal(face.length, 3);
});

test('every vertex belongs to exactly three faces, so the solid is closed and has no spare vertex', () => {
  const solid = tetrahedron(1);
  const uses = new Array(solid.vertices.length).fill(0);
  for (const face of solid.faces) for (const corner of face) uses[corner] += 1;
  assert.deepEqual([...new Set(uses)], [3]);
});

test('every edge is shared by exactly two faces, which a solid with a hole in it would not be', () => {
  const solid = tetrahedron(1);
  const shared = new Map();
  for (const face of solid.faces) {
    for (let i = 0; i < face.length; i++) {
      const [a, b] = [face[i], face[(i + 1) % face.length]];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      shared.set(key, (shared.get(key) ?? 0) + 1);
    }
  }
  assert.equal(shared.size, 6);
  assert.deepEqual([...new Set(shared.values())], [2]);
});

test('every face is wound counter-clockwise seen from outside, which is what DieShape stores', () => {
  assert.ok(woundOutward(tetrahedron(1)));
});

test('the centroid is the origin, which is what makes the inertia and the apex directions mean anything', () => {
  const sum = tetrahedronVertices().reduce((a, v) => [a[0] + v[0], a[1] + v[1], a[2] + v[2]], [0, 0, 0]);
  for (const component of sum) assert.equal(component, 0);
});

test('the inertia is isotropic, so the die has no axis it prefers to spin about', () => {
  const { principal, offDiagonal } = inertia(tetrahedron(1));
  assert.ok(offDiagonal < GEOMETRY_TOLERANCE, `off-diagonal ${offDiagonal}`);
  for (const moment of principal) assert.ok(Math.abs(moment - principal[0]) < GEOMETRY_TOLERANCE);
});

test("the measured inertia matches the tetrahedron's own closed form, an independent route to it", () => {
  assert.ok(measure().closedFormError < GEOMETRY_TOLERANCE);
  const unitEdge = tetrahedron(Math.sqrt(3 / 8));
  const { principal } = inertia(unitEdge);
  assert.ok(Math.abs(principal[0] - INERTIA_PER_MASS_PER_EDGE_SQUARED) < GEOMETRY_TOLERANCE);
});

test('each vertex is opposite exactly one face, which is the rule the D4 reads its result by', () => {
  const solid = tetrahedron(1);
  const { pairing, worst, oneToOne } = oppositeVertices(solid);
  assert.ok(oneToOne, `pairing ${pairing} reuses a face`);
  assert.ok(worst < GEOMETRY_TOLERANCE, `worst miss ${worst} rad`);
  // The apex belongs to no corner of the face it stands over: that is what makes the resting
  // face's numeral readable at the apex of all three faces that are visible.
  for (let vertex = 0; vertex < solid.vertices.length; vertex++) {
    assert.ok(!solid.faces[pairing[vertex]].includes(vertex));
  }
});

test('a die resting on a face holds its apex straight up, so apex tilt and resting tilt are one measurement', () => {
  const solid = tetrahedron(1);
  const { pairing } = oppositeVertices(solid);
  for (let vertex = 0; vertex < solid.vertices.length; vertex++) {
    const apex = solid.vertices[vertex];
    const down = faceNormal(solid, pairing[vertex]);
    const sum = [apex[0] / norm(apex) + down[0], apex[1] / norm(apex) + down[1], apex[2] / norm(apex) + down[2]];
    assert.ok(norm(sum) < GEOMETRY_TOLERANCE, `vertex ${vertex} is not antipodal to face ${pairing[vertex]}`);
  }
});

test('the three raised faces face the camera at a third, below the renderer\'s numeral threshold of 0.5', () => {
  const { facing, degrees } = restingFaceFacing(tetrahedron(1));
  assert.ok(Math.abs(facing - 1 / 3) < GEOMETRY_TOLERANCE, `facing ${facing}`);
  assert.ok(Math.abs(degrees - 70.5288) < 1e-3, `${degrees} degrees`);
  assert.ok(facing < 0.5, 'a resting D4 would draw no numeral at the shipped threshold');
});

test('every face is flat, so each triangle is a plane and not a dish', () => {
  const solid = tetrahedron(1);
  solid.faces.forEach((face, i) => {
    const normal = faceNormal(solid, i);
    const offset = dot(solid.vertices[face[0]], normal);
    for (const corner of face) {
      assert.ok(Math.abs(dot(solid.vertices[corner], normal) - offset) < GEOMETRY_TOLERANCE);
    }
  });
});

test('every vertex sits at the circumradius asked for, so the bounding sphere is the one the physics uses', () => {
  const radius = 9.98609;
  for (const vertex of tetrahedron(radius).vertices) {
    assert.ok(Math.abs(norm(vertex) - radius) < GEOMETRY_TOLERANCE * radius);
  }
});

test('the circumradius is the square root of three eighths of the edge, the tetrahedron it must be', () => {
  const m = measure();
  assert.ok(Math.abs(m.circumradiusPerEdge - Math.sqrt(3 / 8)) < GEOMETRY_TOLERANCE);
  assert.ok(Math.abs(m.inradius - 1 / 3) < GEOMETRY_TOLERANCE);
  assert.ok(Math.abs(m.dihedral[0] - (Math.acos(1 / 3) * 180) / Math.PI) < 1e-9);
});

test('a solid that is not a tetrahedron throws rather than reporting one face as though it spoke for three', () => {
  const solid = tetrahedron(1);
  assert.throws(() => restingFaceFacing({ vertices: solid.vertices, faces: [solid.faces[0]] }));
  // An irregular tetrahedron has four faces but no single raised tilt, which is the case the
  // comparison exists for: the shape is the right size and still the wrong answer.
  const stretched = solid.vertices.map((v, i) => (i === 0 ? v.map((c) => c * 1.5) : v));
  assert.throws(() => restingFaceFacing({ vertices: stretched, faces: solid.faces }));
});

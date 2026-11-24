// Tests for the pure half of scripts/dodecahedron-probe.mjs (slice 0029). The probe's output
// becomes constants in the firmware, so these check the solid it builds and the arithmetic it
// measures with, not just that it returns numbers.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  dodecahedron,
  dodecahedronVertices,
  hullFaces,
  INERTIA_PER_MASS_PER_EDGE_SQUARED,
  measure,
  PHI,
  woundOutward,
} from './dodecahedron-probe.mjs';
import { faceNormal, GEOMETRY_TOLERANCE, inertia, selfCheck } from './die-shape-probe.mjs';

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const norm = (a) => Math.sqrt(dot(a, a));

test('the borrowed self-check reproduces inertia known in closed form, which is what makes the rest believable', () => {
  for (const { name, error } of selfCheck()) {
    assert.ok(error < GEOMETRY_TOLERANCE, `${name} off by ${error}`);
  }
});

test('the solid is a dodecahedron: 20 vertices, 12 faces, 5 corners to a face', () => {
  const solid = dodecahedron(1);
  assert.equal(solid.vertices.length, 20);
  assert.equal(solid.faces.length, 12);
  for (const face of solid.faces) assert.equal(face.length, 5);
});

test('every vertex belongs to exactly three faces, so the solid is closed and has no spare vertex', () => {
  const solid = dodecahedron(1);
  const uses = new Array(solid.vertices.length).fill(0);
  for (const face of solid.faces) for (const corner of face) uses[corner] += 1;
  assert.deepEqual([...new Set(uses)], [3]);
});

test('every edge is shared by exactly two faces, which a solid with a hole in it would not be', () => {
  const solid = dodecahedron(1);
  const shared = new Map();
  for (const face of solid.faces) {
    for (let i = 0; i < face.length; i++) {
      const [a, b] = [face[i], face[(i + 1) % face.length]];
      const key = a < b ? `${a},${b}` : `${b},${a}`;
      shared.set(key, (shared.get(key) ?? 0) + 1);
    }
  }
  assert.equal(shared.size, 30);
  assert.deepEqual([...new Set(shared.values())], [2]);
});

test('every face is wound counter-clockwise seen from outside, which is what DieShape stores', () => {
  assert.ok(woundOutward(dodecahedron(1)));
});

test('every face is flat, so each pentagon is a plane and not a dish', () => {
  const solid = dodecahedron(1);
  solid.faces.forEach((face, i) => {
    const normal = faceNormal(solid, i);
    const offset = dot(normal, solid.vertices[face[0]]);
    for (const corner of face) {
      assert.ok(Math.abs(dot(normal, solid.vertices[corner]) - offset) < GEOMETRY_TOLERANCE);
    }
  });
});

test('the inertia is isotropic, which is what lets DieShape carry one scalar', () => {
  const { principal, offDiagonal, spread } = measure();
  assert.ok(offDiagonal < GEOMETRY_TOLERANCE, `off-diagonal ${offDiagonal}`);
  assert.ok(spread < GEOMETRY_TOLERANCE, `spread ${spread}`);
  for (const moment of principal) assert.ok(Math.abs(moment - principal[0]) < GEOMETRY_TOLERANCE);
});

test('the integrated inertia agrees with the dodecahedron\'s own closed form, by an independent route', () => {
  const { closedFormError, principal, edges } = measure();
  assert.ok(closedFormError < GEOMETRY_TOLERANCE, `closed form off by ${closedFormError}`);
  // Stated the other way round as well, so a change to either expression is caught.
  assert.ok(Math.abs(principal[0] / edges[0] ** 2 - INERTIA_PER_MASS_PER_EDGE_SQUARED) < GEOMETRY_TOLERANCE);
});

test('every face is the same distance from the centre and every vertex too, which is why it is fair', () => {
  const { inradiusSpread, circumradiusSpread } = measure();
  assert.ok(inradiusSpread < GEOMETRY_TOLERANCE, `inradius spread ${inradiusSpread}`);
  assert.ok(circumradiusSpread < GEOMETRY_TOLERANCE, `circumradius spread ${circumradiusSpread}`);
});

test('opposite faces are exactly opposed and the pairing is an involution, so a numeral has an opposite', () => {
  const { opposite } = measure();
  assert.ok(opposite.worst < GEOMETRY_TOLERANCE, `worst ${opposite.worst}`);
  assert.ok(opposite.involution);
});

test('there is one edge length and one dihedral angle, as a regular solid has', () => {
  const { edges, dihedral, circumradiusPerEdge } = measure();
  assert.equal(edges.length, 1);
  assert.equal(dihedral.length, 1);
  // Known values: the edge is 2/(sqrt(3)*phi) of the circumradius, and the dihedral is
  // 2*atan(phi), which is what the firmware's constant is derived from.
  assert.ok(Math.abs(edges[0] - 2 / (Math.sqrt(3) * PHI)) < GEOMETRY_TOLERANCE);
  assert.ok(Math.abs(dihedral[0] - (2 * Math.atan(PHI) * 180) / Math.PI) < 1e-9);
  assert.ok(Math.abs(circumradiusPerEdge - (Math.sqrt(3) * PHI) / 2) < GEOMETRY_TOLERANCE);
});

test('the solid scales to the circumradius it is asked for', () => {
  const radius = 9.98609325;
  const solid = dodecahedron(radius);
  for (const vertex of solid.vertices) assert.ok(Math.abs(norm(vertex) - radius) < 1e-12 * radius);
});

test('hullFaces finds every face whatever order the vertices come in', () => {
  // A face is found through triples of its corners taken in ascending index order, and for a
  // convex polygon every such triple winds the same way. So an ordering can exist in which all
  // ten of a pentagon's triples wind inward, and that face is invisible to a search that only
  // takes outward-wound triples. This ordering, the half-plus-mirror one die_shape.cpp lists the
  // D12's vertices in, does exactly that to three of the twelve faces.
  const PHI_ = (1 + Math.sqrt(5)) / 2;
  const half = [
    [1, 1, 1], [1, 1, -1], [1, -1, 1], [-1, 1, 1],
    [0, 1 / PHI_, PHI_], [0, -1 / PHI_, PHI_],
    [1 / PHI_, PHI_, 0], [-1 / PHI_, PHI_, 0],
    [PHI_, 0, 1 / PHI_], [PHI_, 0, -1 / PHI_],
  ];
  const mirrored = [...half, ...half.map((v) => v.map((c) => -c))];
  assert.equal(hullFaces(mirrored, 5).length, 12);
  assert.ok(woundOutward({ vertices: mirrored, faces: hullFaces(mirrored, 5) }));

  // And the face set itself does not depend on the order: the same corners, as sets of points.
  const asPoints = (vertices, faces) =>
    new Set(faces.map((f) => f.map((c) => vertices[c].map((x) => x.toFixed(9)).join(',')).sort().join(' | ')));
  const built = dodecahedronVertices();
  assert.deepEqual(
    asPoints(mirrored, hullFaces(mirrored, 5)),
    asPoints(built, hullFaces(built, 5)),
  );
});

test('hullFaces refuses a solid whose faces do not have the corner count asked for, rather than inventing one', () => {
  assert.throws(() => hullFaces(dodecahedronVertices(), 4), /has 5 corners, not 4/);
});

test('hullFaces finds a cube from its vertices alone, so it is not tuned to one solid', () => {
  const vertices = [];
  for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) vertices.push([x, y, z]);
  const faces = hullFaces(vertices, 4);
  assert.equal(faces.length, 6);
  const cube = { vertices, faces };
  assert.ok(woundOutward(cube));
  // A cube of side 2 has inertia 4/6 per unit mass about every axis.
  assert.ok(Math.abs(inertia(cube).principal[0] - 4 / 6) < GEOMETRY_TOLERANCE);
});

test('an interior point is inside every face plane, which a wrongly wound or inside-out hull would fail', () => {
  const solid = dodecahedron(1);
  const inside = [0.01, -0.02, 0.03];
  solid.faces.forEach((face, i) => {
    const normal = faceNormal(solid, i);
    assert.ok(dot(normal, inside) < dot(normal, solid.vertices[face[0]]));
  });
});

test('the vertices are the cube and the three golden rectangles, as the construction claims', () => {
  const vertices = dodecahedronVertices();
  assert.equal(vertices.length, 20);
  const cubeCorners = vertices.filter((v) => v.every((c) => Math.abs(Math.abs(c) - 1) < GEOMETRY_TOLERANCE));
  assert.equal(cubeCorners.length, 8);
  const rectangleCorners = vertices.filter((v) => v.some((c) => Math.abs(c) < GEOMETRY_TOLERANCE));
  assert.equal(rectangleCorners.length, 12);
  for (const corner of rectangleCorners) {
    const sides = corner.map(Math.abs).sort((a, b) => a - b);
    assert.ok(Math.abs(sides[0]) < GEOMETRY_TOLERANCE);
    assert.ok(Math.abs(sides[1] - 1 / PHI) < GEOMETRY_TOLERANCE);
    assert.ok(Math.abs(sides[2] - PHI) < GEOMETRY_TOLERANCE);
  }
  // No vertex repeated: a duplicate would make the hull search find a face twice.
  for (let i = 0; i < vertices.length; i++) {
    for (let j = i + 1; j < vertices.length; j++) {
      assert.ok(norm(sub(vertices[i], vertices[j])) > GEOMETRY_TOLERANCE);
    }
  }
});
